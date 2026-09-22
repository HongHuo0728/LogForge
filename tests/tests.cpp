#include "logforge/Color.h"
#include "logforge/FFmpeg.h"
#include <chrono>
#include <cmath>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <thread>

void check(bool ok, const char* msg) {
    if (!ok)
        throw std::runtime_error(msg);
}
void Near(double a, double b, double tolerance, const char* msg) {
    check(std::abs(a - b) <= tolerance, msg);
}
logforge::Json Fixture() {
    return logforge::Json::parse(
        R"({"streams":[{"codec_type":"video","codec_name":"prores","profile":"HQ","width":1920,"height":1080,"pix_fmt":"yuv422p10le","bits_per_raw_sample":"10","color_primaries":"bt2020","color_transfer":"arib-std-b67","color_space":"bt2020nc","color_range":"tv","field_order":"progressive","r_frame_rate":"30000/1001","avg_frame_rate":"30000/1001","time_base":"1/30000","duration":"1.001","nb_frames":"30","tags":{"timecode":"01:00:00:00"}},{"codec_type":"audio","codec_name":"pcm_s24le","sample_rate":"48000","channels":2,"channel_layout":"stereo","duration":"1.001"}],"format":{"format_name":"mov,mp4,m4a,3gp,3g2,mj2","duration":"1.001","tags":{"major_brand":"qt  ","com.apple.quicktime.model":"iPhone 13 Pro"}}})");
}
void MediaTests() {
    using namespace logforge;
    auto j = Fixture();
    auto m = MediaInfo::Parse(j);
    check(m.UnsupportedReasons().empty(), "Valid HLG ProRes rejected");
    check(m.width == 1920 && m.height == 1080 && m.bitDepth == 10, "Media dimensions/depth parser");
    Near(m.fps.Value(), 30000.0 / 1001, 1e-10, "Rational fps parser");
    check(m.audio.size() == 1 && m.audio[0].sampleRate == 48000 && m.audio[0].channels == 2, "Audio parser");
    check(m.timecode == "01:00:00:00", "Timecode parser");
    for (auto key : {"codec_name", "profile", "pix_fmt", "color_primaries", "color_transfer", "color_space",
                     "color_range"}) {
        auto bad = j;
        bad["streams"][0][key] = "unsupported";
        check(!MediaInfo::Parse(bad).UnsupportedReasons().empty(), "Unsupported media accepted");
    }
    auto missing = j;
    missing["streams"][0].erase("color_transfer");
    check(!MediaInfo::Parse(missing).UnsupportedReasons().empty(), "Missing HLG tag accepted");
    auto vfr = j;
    vfr["streams"][0]["avg_frame_rate"] = "25/1";
    check(!MediaInfo::Parse(vfr).UnsupportedReasons().empty(), "VFR accepted");
    auto rotated = j;
    rotated["streams"][0]["side_data_list"] = Json::array({{{"rotation", -90}}});
    Near(MediaInfo::Parse(rotated).rotation, -90, 0, "Rotation parser");
    check(Rational::Parse("0/0").Value() == 0 && Rational::Parse("x/y").Value() == 0 &&
              Rational::Parse("24/1junk").Value() == 0,
          "Malformed rational accepted");
    bool threw = false;
    try {
        MediaInfo::Parse(Json::object());
    } catch (...) {
        threw = true;
    }
    check(threw, "Malformed JSON accepted");
    auto output = j;
    output["streams"][0].erase("color_transfer");
    output["format"]["tags"]["logforge.transfer"] = "Apple Log";
    check(ValidateOutput(m, MediaInfo::Parse(output), 30).passed, "Valid output rejected");
    for (auto [key, value] : std::vector<std::pair<std::string, Json>>{{"pix_fmt", "yuv420p"},
                                                                       {"codec_name", "h264"},
                                                                       {"color_primaries", "bt709"},
                                                                       {"color_transfer", "arib-std-b67"},
                                                                       {"profile", "Standard"},
                                                                       {"width", 1280},
                                                                       {"nb_frames", "29"}}) {
        auto bad = output;
        bad["streams"][0][key] = value;
        check(!ValidateOutput(m, MediaInfo::Parse(bad), 30).passed, "Incorrect output validated");
    }
    auto noAudio = output;
    noAudio["streams"].erase(1);
    check(!ValidateOutput(m, MediaInfo::Parse(noAudio), 30).passed, "Lost audio validated");
    auto wrongAudio = output;
    wrongAudio["streams"][1]["sample_rate"] = "44100";
    check(!ValidateOutput(m, MediaInfo::Parse(wrongAudio), 30).passed, "Changed sample rate validated");
    auto metadata = AppleLogMetadataWriter::Arguments(m);
    check(std::find(metadata.begin(), metadata.end(), L"2") != metadata.end(),
          "Unspecified transfer missing");
    std::cout << "PASS: media parser, unsupported inputs, output validation, metadata policy\n";
}
void PlatformTests() {
    using namespace logforge;
    check(FFmpegManager::MissingCapabilities(" VFS..D prores Apple ProRes", "Encoder prores_ks yuv422p10le",
                                             " zscale  format  setparams ", "gbrpf32le yuv422p10le")
              .empty(),
          "Capability parser");
    check(FFmpegManager::MissingCapabilities("", "", "", "").size() == 7, "Missing capability detection");
    check(QuoteArgument(L"a b") == L"\"a b\"", "Space quoting");
    check(QuoteArgument(L"a\"b") == L"\"a\\\"b\"", "Quote escaping");
    check(QuoteArgument(L"a\\") == L"\"a\\\\\"", "Trailing slash quoting");
    check(Utf8(Wide("中文 文件.mov")) == "中文 文件.mov", "Unicode paths");
    auto dir = fs::temp_directory_path() / (L"LogForge-tests-" + std::to_wstring(GetCurrentProcessId()));
    fs::create_directories(dir);
    auto file = dir / L"sha.txt";
    {
        std::ofstream f(file);
        f << "abc";
    }
    check(SHA256(file) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
          "SHA256 reference vector");
    fs::remove(file);
    std::atomic_bool cancel = false;
    Logger log;
    bool rejected = false;
    try {
        FFmpegManager(log).Check(dir / L"not-found.exe", cancel);
    } catch (...) {
        rejected = true;
    }
    check(rejected, "Missing FFmpeg was accepted");
    auto mov = dir / L"atoms.mov";
    {
        std::ofstream f(mov, std::ios::binary);
        const unsigned char bytes[]{0, 0, 0, 18, 'c', 'o', 'l', 'r', 'n', 'c', 'l', 'c', 0, 9, 0, 2, 0, 9};
        f.write(reinterpret_cast<const char*>(bytes), sizeof(bytes));
    }
    auto atoms = ReferenceMovAnalyzer::Analyze(mov);
    check(atoms["atoms"][0]["transfer"] == 2, "MOV color atom parser");
    fs::remove(mov);
    fs::remove(dir);
    auto spec = GyanReleaseProvider().Release();
    check(spec.url.starts_with(L"https://") && spec.sha256.size() == 64 && spec.version == L"8.1.2",
          "Pinned provider manifest");
    std::jthread timer([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
        cancel = true;
    });
    bool stopped = false;
    auto begin = std::chrono::steady_clock::now();
    try {
        RunProcess(ExecutableDirectory() / L"logforge_tests.exe", {L"--child-sleep"}, &cancel, 5);
    } catch (...) {
        stopped = true;
    }
    check(stopped && std::chrono::steady_clock::now() - begin < std::chrono::seconds(3),
          "Cancellation did not stop supervised child");
    timer.join();
    std::cout << "PASS: capability detection, missing FFmpeg, process quoting, SHA256, MOV atoms, provider\n";
}
int main(int argc, char** argv) {
    if (argc > 1 && std::string(argv[1]) == "--child-sleep") {
        Sleep(30000);
        return 0;
    }
    try {
        const std::string group = argc > 1 ? argv[1] : "all";
        if (group == "media") {
            MediaTests();
            return 0;
        }
        if (group == "platform") {
            PlatformTests();
            return 0;
        }
        using A = logforge::AppleLogTransferFunction;
        Near(A::EncodeLinearToAppleLog(0), 0.150476, 0.0000005,
             "Apple black reference (formula rounded to six decimals)");
        Near(A::EncodeLinearToAppleLog(0.18), 0.488272, 0.0000005, "Apple 18% gray reference");
        Near(A::EncodeLinearToAppleLog(0.9), 0.681687, 0.0000005,
             "Apple 90% white reference (formula rounded to six decimals)");
        Near(A::EncodeLinearToAppleLog(12), 1.0, 0.00001, "Apple 1200% reference");
        Near(A::EncodeLinearToAppleLog(A::R0), 0, 1e-15, "Apple floor");
        Near(A::DecodeAppleLogToLinear(-0.1), A::R0, 1e-15, "Apple negative code");
        for (int i = 0; i <= 100000; ++i) {
            const double r = A::R0 + (12.0 - A::R0) * i / 100000.0;
            Near(A::DecodeAppleLogToLinear(A::EncodeLinearToAppleLog(r)), r, 2e-7, "Apple round trip");
            const double h = -0.1 + i * 1.2 / 100000.0;
            Near(logforge::HLG::EncodeSceneLinear(logforge::HLG::DecodeToSceneLinear(h)), h, 1e-12,
                 "HLG round trip");
        }
        Near(logforge::HLG::DecodeToSceneLinear(0.5), 1.0 / 12, 1e-12, "HLG knee");
        Near(logforge::HLG::DecodeToSceneLinear(1), 1, 3e-8, "HLG peak (published rounded constants)");
        Near(logforge::HLGToAppleLog(0.75), A::EncodeLinearToAppleLog(0.9), 1e-12, "Diffuse white policy");
        Near(A::EncodeLinearToAppleLog(0.18), 0.4882724585268676, 1e-14, "Independent double gray reference");
        Near(A::EncodeLinearToAppleLog(0.0), 0.1504764523009125, 1e-14, "Independent double black reference");
        Near(A::EncodeLinearToAppleLog(0.01), A::Pt, 2e-8,
             "Apple knee continuity from published rounded coefficients");
        float data[]{-0.03f, 0, .5f, .75f, 1, 1.1f};
        logforge::TransformHLGToAppleLog(data);
        for (float v : data)
            check(std::isfinite(v), "Float transform finite output");
        float invalid[]{std::numeric_limits<float>::quiet_NaN()};
        bool rejected = false;
        try {
            logforge::TransformHLGToAppleLog(invalid);
        } catch (...) {
            rejected = true;
        }
        check(rejected, "Non-finite pixel accepted");
        std::cout << "PASS: Apple reference points, 100001 Apple/HLG round trips, normalization\n";
        if (group == "all") {
            MediaTests();
            PlatformTests();
        }
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
