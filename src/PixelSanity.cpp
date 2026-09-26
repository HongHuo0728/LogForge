#include "logforge/PixelSanity.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <thread>
namespace logforge {
PixelSanity::PixelSanity(const MediaInfo& in, int64_t frames) : input_(in) {
    std::vector<int64_t> samples{0, frames / 2, frames - 1};
    std::sort(samples.begin(), samples.end());
    samples.erase(std::unique(samples.begin(), samples.end()), samples.end());
    const int w = std::min(16, in.width) & ~1, h = std::min(16, in.height);
    for (auto n : samples) {
        Frame frame{n, {}};
        for (int y = 1; y <= 3; ++y)
            for (int x = 1; x <= 3; ++x) {
                Patch p{((in.width - w) * x / 4) & ~1, (in.height - h) * y / 4, w, h, {}};
                p.rgb.resize(static_cast<size_t>(w) * h * 3);
                frame.patches.push_back(std::move(p));
            }
        frames_.push_back(std::move(frame));
    }
}
void PixelSanity::Observe(std::span<const float> decoded) {
    const uint64_t plane = uint64_t(input_.width) * input_.height;
    for (auto& frame : frames_)
        for (auto& patch : frame.patches)
            for (int c = 0; c < 3; ++c)
                for (int row = 0; row < patch.h; ++row) {
                    const auto start = frame.number * plane * 3 + c * plane +
                                       uint64_t(patch.y + row) * input_.width + patch.x;
                    const auto first = std::max(offset_, start),
                               last = std::min(offset_ + decoded.size(), start + patch.w);
                    if (first < last)
                        std::copy_n(decoded.data() + first - offset_, last - first,
                                    patch.rgb.data() + c * patch.w * patch.h + row * patch.w + first - start);
                }
    offset_ += decoded.size();
}
Json PixelSanity::Validate(const fs::path& ffmpeg, const fs::path& output, const std::atomic_bool& cancel,
                           double exposure, const ToneAdjustments& tone) {
    const uint64_t plane = uint64_t(input_.width) * input_.height, frameSamples = plane * 2;
    Json results = Json::array();
    double peak = 0, total = 0;
    uint64_t count = 0;
    for (const auto& frame : frames_) {
        if (cancel)
            throw AppError(TextId::Cancelled);
        std::wostringstream seek;
        seek << std::fixed << std::setprecision(12)
             << std::max(0.0, (frame.number - .25) / input_.fps.Value());
        FFmpegProcess decode(ffmpeg,
                             {L"-v",        L"error",   L"-nostdin",      L"-threads",    L"1",
                              L"-ss",       seek.str(), L"-noautorotate", L"-i",          output.wstring(),
                              L"-map",      L"0:v:0",   L"-an",           L"-sn",         L"-dn",
                              L"-frames:v", L"1",       L"-pix_fmt",      L"yuv422p10le", L"-f",
                              L"rawvideo",  L"pipe:1"});
        std::string error;
        std::exception_ptr readError;
        std::jthread errors, stop;
        const auto start = GetTickCount64();
        std::atomic_bool timedOut = false;
        struct Cleanup {
            FFmpegProcess& p;
            std::jthread& e;
            std::jthread& s;
            ~Cleanup() {
                p.Terminate();
                s.request_stop();
                if (e.joinable())
                    e.join();
                if (s.joinable())
                    s.join();
            }
        } cleanup{decode, errors, stop};
        errors = std::jthread([&] {
            try {
                ReadLines(decode.ErrorPipe(), [&](const auto& s) {
                    if (error.size() < 8192)
                        error += s;
                });
            } catch (...) {
                readError = std::current_exception();
                decode.Terminate();
            }
        });
        stop = std::jthread([&](std::stop_token token) {
            while (!token.stop_requested()) {
                if (cancel || GetTickCount64() - start > 60000) {
                    timedOut = !cancel;
                    decode.Terminate();
                    return;
                }
                Sleep(20);
            }
        });
        std::vector<std::array<double, 3>> sums(frame.patches.size());
        std::array<uint16_t, 32768> buffer{};
        uint64_t position = 0;
        size_t carry = 0;
        for (;;) {
            const auto n =
                decode.Read(reinterpret_cast<char*>(buffer.data()) + carry, sizeof(buffer) - carry);
            if (!n) {
                if (carry)
                    throw AppError(TextId::IncompleteFrame);
                break;
            }
            const auto bytes = n + carry, samples = bytes / 2;
            for (size_t p = 0; p < frame.patches.size(); ++p) {
                const auto& patch = frame.patches[p];
                for (int c = 0; c < 3; ++c)
                    for (int row = 0; row < patch.h; ++row) {
                        const uint64_t width = c ? input_.width / 2 : input_.width,
                                       base = c ? plane + (c - 1) * plane / 2 : 0;
                        const auto at = base + (patch.y + row) * width + (c ? patch.x / 2 : patch.x);
                        const uint64_t length = c ? patch.w / 2 : patch.w;
                        const auto first = std::max(position, at),
                                   last = std::min(position + samples, at + length);
                        for (auto i = first; i < last; ++i) {
                            if (buffer[i - position] > 1023)
                                throw AppError(TextId::PixelSanityFailed);
                            sums[p][c] += buffer[i - position];
                        }
                    }
            }
            position += samples;
            carry = bytes % 2;
            if (carry)
                reinterpret_cast<char*>(buffer.data())[0] = reinterpret_cast<char*>(buffer.data())[bytes - 1];
        }
        const int exit = decode.Wait();
        decode.Terminate();
        errors.join();
        stop.request_stop();
        stop.join();
        if (cancel)
            throw AppError(TextId::Cancelled);
        if (timedOut || readError || exit || position != frameSamples)
            throw AppError(Message(TextId::PixelDecodeFailed, {error}));
        double framePeak = 0, frameSum = 0;
        for (size_t p = 0; p < frame.patches.size(); ++p) {
            const auto& patch = frame.patches[p];
            auto reference = patch.rgb;
            TransformHLGToAppleLog(reference, exposure,
                                   tone); // Independent scalar CPU reference from decoded INPUT.
            const size_t n = reference.size() / 3;
            std::array<double, 3> expected{};
            for (size_t i = 0; i < n; ++i) {
                const double g = reference[i], b = reference[n + i], r = reference[2 * n + i],
                             y = .2627 * r + .678 * g + .0593 * b;
                expected[0] += std::clamp(64 + 876 * y, 0., 1023.);
                expected[1] += std::clamp(512 + 896 * (b - y) / 1.8814, 0., 1023.);
                expected[2] += std::clamp(512 + 896 * (r - y) / 1.4746, 0., 1023.);
            }
            for (size_t c = 0; c < 3; ++c) {
                const double errorCode = std::abs(expected[c] / n - sums[p][c] / (c ? n / 2 : n));
                framePeak = std::max(framePeak, errorCode);
                frameSum += errorCode;
                ++count;
            }
        }
        peak = std::max(peak, framePeak);
        total += frameSum;
        results.push_back({{"frame", frame.number},
                           {"max_patch_mean_code_error", framePeak},
                           {"mean_code_error", frameSum / (frame.patches.size() * 3)}});
    }
    // This gate detects gross regressions on real, textured, lossy ProRes media.
    // Patch borders may straddle chroma reconstruction lobes. It is separate from
    // the existing <=2-code flat-patch qualification, whose tolerance is unchanged.
    const double mean = total / count;
    const bool passed = peak <= 32 && mean <= 8;
    return {
        {"passed", passed},
        {"scope", "first/middle/last, 9 patches per frame; scalar input reference vs decoded output YCbCr"},
        {"max_patch_mean_code_error", peak},
        {"mean_code_error", mean},
        {"peak_limit_codes", 32},
        {"mean_limit_codes", 8},
        {"frames", results}};
}
} // namespace logforge
