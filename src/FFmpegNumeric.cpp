#include "logforge/FFmpeg.h"
#include <array>
#include <cmath>

namespace logforge {
// A tiny independent integer YCbCr stimulus. No LUT or FFmpeg-generated colors
// are used as the oracle. Matrix/range are checked before and after ProRes HQ.
Json VerifyFFmpegNumerics(const fs::path& ff, const std::atomic_bool& cancel, Logger& log) {
    const auto dir =
        DataDirectory() / L"cache" /
        (L"numeric-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()));
    fs::create_directories(dir);
    const std::array<fs::path, 5> files{dir / L"signal.yuv", dir / L"decoded.f32", dir / L"apple.f32",
                                        dir / L"encoded.mov", dir / L"roundtrip.yuv"};
    struct Cleanup {
        fs::path dir;
        std::array<fs::path, 5> files;
        ~Cleanup() {
            std::error_code ec;
            for (const auto& p : files)
                fs::remove(p, ec);
            fs::remove(dir, ec);
        }
    } cleanup{dir, files};
    constexpr int width = 256, height = 64, pixels = width * height;
    const std::array<std::array<uint16_t, 3>, 16> patches{{{64, 512, 512},
                                                           {65, 512, 512},
                                                           {70, 512, 512},
                                                           {96, 512, 512},
                                                           {128, 512, 512},
                                                           {256, 512, 512},
                                                           {394, 512, 512},
                                                           {502, 512, 512},
                                                           {721, 512, 512},
                                                           {800, 512, 512},
                                                           {940, 512, 512},
                                                           {976, 512, 512},
                                                           {512, 576, 512},
                                                           {512, 512, 576},
                                                           {512, 448, 480},
                                                           {512, 560, 560}}};
    std::vector<uint16_t> stimulus(pixels * 2);
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x) {
            auto p = patches[x / 16];
            stimulus[y * width + x] = p[0];
            if (!(x % 2)) {
                stimulus[pixels + y * width / 2 + x / 2] = p[1];
                stimulus[pixels * 3 / 2 + y * width / 2 + x / 2] = p[2];
            }
        }
    {
        std::ofstream f(files[0], std::ios::binary);
        f.write(reinterpret_cast<const char*>(stimulus.data()), stimulus.size() * 2);
        if (!f)
            throw AppError(TextId::FFmpegSmokeFailed);
    }
    auto run = [&](std::vector<std::wstring> a) {
        log.Write("Numeric reference command: " + Utf8(CommandLine(ff, a)));
        const auto r = RunProcess(ff, a, &cancel, 30);
        if (r.exitCode)
            throw AppError(Message(TextId::FFmpegNumericFailed, {r.error}));
    };
    auto rawInput = [&](const fs::path& path, const std::wstring& fmt) {
        return std::vector<std::wstring>{
            L"-v",          L"error",  L"-nostdin",   L"-y", L"-f", L"rawvideo",    L"-pixel_format", fmt,
            L"-video_size", L"256x64", L"-framerate", L"24", L"-i", path.wstring(), L"-frames:v",     L"1"};
    };
    auto decode = rawInput(files[0], L"yuv422p10le");
    decode.insert(decode.end(), {L"-vf",
                                 L"zscale=matrixin=2020_ncl:matrix=gbr:rangein=limited:range=full:"
                                 L"transferin=arib-std-b67:transfer=arib-std-b67:primariesin=2020:primaries="
                                 L"2020:chromalin=left:filter=spline36,format=gbrpf32le",
                                 L"-f", L"rawvideo", files[1].wstring()});
    run(decode);
    if (fs::file_size(files[1]) != pixels * 3 * sizeof(float))
        throw AppError(TextId::FFmpegSmokeFailed);
    std::vector<float> rgb(pixels * 3);
    {
        std::ifstream f(files[1], std::ios::binary);
        f.read(reinterpret_cast<char*>(rgb.data()), rgb.size() * 4);
    }
    double matrixError = 0;
    std::array<std::array<double, 3>, 16> expected{};
    for (size_t patch = 0; patch < patches.size(); ++patch) {
        const auto p = patches[patch];
        const double y = (p[0] - 64.) / 876., cb = (p[1] - 512.) / 896., cr = (p[2] - 512.) / 896.;
        const double red = y + 1.4746 * cr, blue = y + 1.8814 * cb,
                     green = (y - .2627 * red - .0593 * blue) / .678;
        const std::array<double, 3> gbr{green, blue, red};
        for (size_t c = 0; c < 3; ++c) {
            const auto sample = rgb[c * pixels + 32 * width + patch * 16 + 8];
            if (!std::isfinite(sample))
                throw AppError(TextId::NonFiniteInput);
            matrixError = std::max(matrixError, std::abs(sample - gbr[c]));
            expected[patch][c] = HLGToAppleLog(gbr[c]);
        }
    }
    if (matrixError > 2e-6)
        throw AppError(Message(TextId::FFmpegNumericFailed,
                               {"BT.2020 matrix/range error=" + std::to_string(matrixError)}));
    // A linear chroma ramp distinguishes half-pixel siting. Spline36 reproduces
    // this ramp away from its clamped endpoints; a wrong siting shifts it by
    // four 10-bit chroma codes, well outside the float tolerance.
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x) {
            stimulus[y * width + x] = 502;
            if (!(x % 2)) {
                stimulus[pixels + y * width / 2 + x / 2] =
                    static_cast<uint16_t>(std::clamp(512 + 8 * (x - 128), 128, 896));
                stimulus[pixels * 3 / 2 + y * width / 2 + x / 2] = 512;
            }
        }
    {
        std::ofstream f(files[0], std::ios::binary | std::ios::trunc);
        f.write(reinterpret_cast<const char*>(stimulus.data()), stimulus.size() * 2);
    }
    double chromaError = 0;
    for (const auto* siting : {L"left", L"center"}) {
        auto command = rawInput(files[0], L"yuv422p10le");
        command.insert(command.end(),
                       {L"-vf",
                        std::wstring(L"zscale=matrixin=2020_ncl:matrix=gbr:rangein=limited:range=full:") +
                            L"transferin=linear:transfer=linear:primariesin=2020:primaries=2020:chromalin=" +
                            siting + L":filter=spline36,format=gbrpf32le",
                        L"-f", L"rawvideo", files[1].wstring()});
        run(command);
        if (fs::file_size(files[1]) != rgb.size() * 4)
            throw AppError(TextId::FFmpegSmokeFailed);
        {
            std::ifstream f(files[1], std::ios::binary);
            f.read(reinterpret_cast<char*>(rgb.data()), rgb.size() * 4);
        }
        for (int x : {124, 128, 132}) {
            const double cb = 8 * (x - 128 - (std::wstring(siting) == L"center" ? .5 : 0)) / 896.;
            const double b = .5 + 1.8814 * cb, g = (.5 - .2627 * .5 - .0593 * b) / .678;
            const std::array<double, 3> expectedGbr{g, b, .5};
            for (size_t c = 0; c < 3; ++c)
                chromaError =
                    std::max(chromaError, std::abs(rgb[c * pixels + 32 * width + x] - expectedGbr[c]));
        }
    }
    if (chromaError > 2e-6)
        throw AppError(Message(TextId::FFmpegNumericFailed,
                               {"Input left/center siting error=" + std::to_string(chromaError)}));
    // Uniform independently computed Apple Log patches isolate the output matrix
    // from input resampling at patch boundaries.
    for (size_t c = 0; c < 3; ++c)
        for (int y = 0; y < height; ++y)
            for (int x = 0; x < width; ++x)
                rgb[c * pixels + y * width + x] = static_cast<float>(expected[x / 16][c]);
    {
        std::ofstream f(files[2], std::ios::binary);
        f.write(reinterpret_cast<const char*>(rgb.data()), rgb.size() * 4);
    }
    auto encode = rawInput(files[2], L"gbrpf32le");
    encode.insert(encode.end(),
                  {L"-vf",
                   L"zscale=matrixin=gbr:matrix=2020_ncl:rangein=full:range=limited:"
                   L"transferin=linear:transfer=linear:primariesin=2020:primaries=2020:chromal=left:filter="
                   L"spline36:dither=error_diffusion,format=yuv422p10le",
                   L"-c:v", L"prores_ks", L"-profile:v", L"3", L"-threads:v", L"2", L"-color_primaries",
                   L"bt2020", L"-color_trc", L"2", L"-colorspace", L"bt2020nc", L"-color_range", L"tv",
                   L"-chroma_sample_location", L"left", files[3].wstring()});
    run(encode);
    const auto media = Probe(ff.parent_path() / L"ffprobe.exe", files[3], &cancel);
    if (media.codec != "prores" || media.profile != "HQ" || media.pixelFormat != "yuv422p10le")
        throw AppError(TextId::FFmpegSmokeFailed);
    run({L"-v", L"error", L"-nostdin", L"-y", L"-i", files[3].wstring(), L"-frames:v", L"1", L"-f",
         L"rawvideo", L"-pix_fmt", L"yuv422p10le", files[4].wstring()});
    if (fs::file_size(files[4]) != stimulus.size() * 2)
        throw AppError(TextId::FFmpegSmokeFailed);
    {
        std::ifstream f(files[4], std::ios::binary);
        f.read(reinterpret_cast<char*>(stimulus.data()), stimulus.size() * 2);
    }
    double encodedError = 0;
    for (size_t p = 0; p < patches.size(); ++p) {
        const auto& gbr = expected[p];
        const double y = .2627 * gbr[2] + .678 * gbr[0] + .0593 * gbr[1];
        const std::array<double, 3> codes{64 + 876 * y, 512 + 896 * (gbr[1] - y) / 1.8814,
                                          512 + 896 * (gbr[2] - y) / 1.4746};
        const size_t x = p * 16 + 8;
        const std::array<size_t, 3> index{32 * width + x, pixels + 32 * width / 2 + x / 2,
                                          pixels * 3 / 2 + 32 * width / 2 + x / 2};
        for (size_t c = 0; c < 3; ++c)
            encodedError = std::max(encodedError, std::abs(stimulus[index[c]] - codes[c]));
    }
    if (encodedError > 2.)
        throw AppError(Message(TextId::FFmpegNumericFailed,
                               {"ProRes round-trip error=" + std::to_string(encodedError) + " code values"}));
    // Test actual post-encode chroma phase, not just a command or metadata tag.
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x) {
            const double cb = (std::clamp(512 + 8 * (x - 128), 128, 896) - 512.) / 896.;
            const double b = .5 + 1.8814 * cb, g = (.5 - .2627 * .5 - .0593 * b) / .678;
            rgb[y * width + x] = static_cast<float>(g);
            rgb[pixels + y * width + x] = static_cast<float>(b);
            rgb[2 * pixels + y * width + x] = .5f;
        }
    {
        std::ofstream f(files[2], std::ios::binary | std::ios::trunc);
        f.write(reinterpret_cast<const char*>(rgb.data()), rgb.size() * 4);
    }
    run(encode);
    run({L"-v", L"error", L"-nostdin", L"-y", L"-i", files[3].wstring(), L"-frames:v", L"1", L"-f",
         L"rawvideo", L"-pix_fmt", L"yuv422p10le", files[4].wstring()});
    if (fs::file_size(files[4]) != stimulus.size() * 2)
        throw AppError(TextId::FFmpegSmokeFailed);
    {
        std::ifstream f(files[4], std::ios::binary);
        f.read(reinterpret_cast<char*>(stimulus.data()), stimulus.size() * 2);
    }
    double outputChromaError = 0;
    for (int x : {124, 128, 132})
        outputChromaError = std::max(
            outputChromaError, std::abs(stimulus[pixels + 32 * width / 2 + x / 2] - (512. + 8 * (x - 128))));
    if (outputChromaError > 2.)
        throw AppError(Message(TextId::FFmpegNumericFailed,
                               {"Post-encode chroma phase error=" + std::to_string(outputChromaError)}));
    return {{"passed", true},
            {"test", "BT2020-range-float-ProResHQ-v1"},
            {"patches", 16},
            {"max_float_matrix_error", matrixError},
            {"float_tolerance", 2e-6},
            {"post_encode_max_code_error", encodedError},
            {"post_encode_tolerance_codes", 2.0},
            {"output_chroma_command", "left"},
            {"output_chroma_ffprobe", media.chromaLocation},
            {"chroma_reference_passed", true},
            {"input_left_center_max_float_error", chromaError},
            {"post_encode_chroma_max_code_error", outputChromaError}};
}
} // namespace logforge
