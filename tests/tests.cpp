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
    j["streams"][0]["chroma_location"] = "left";
    auto m = MediaInfo::Parse(j);
    std::vector<VideoPacketTiming> timing;
    for (int i = 0; i < 30; ++i)
        timing.push_back({i * 1001, 1001});
    m.cadence = AnalyzeCadence(timing, m);
    auto validated = [&](const Json& json) {
        auto media = MediaInfo::Parse(json);
        media.cadence = AnalyzeCadence(timing, media);
        return media;
    };
    check(m.UnsupportedReasons().empty(), "Valid HLG ProRes rejected");
    check(m.width == 1920 && m.height == 1080 && m.bitDepth == 10, "Media dimensions/depth parser");
    Near(m.fps.Value(), 30000.0 / 1001, 1e-10, "Rational fps parser");
    check(m.audio.size() == 1 && m.audio[0].sampleRate == 48000 && m.audio[0].channels == 2, "Audio parser");
    check(m.timecode == "01:00:00:00", "Timecode parser");
    for (auto key : {"codec_name", "profile", "color_transfer"}) {
        auto bad = j;
        bad["streams"][0][key] = "unsupported";
        check(!MediaInfo::Parse(bad).UnsupportedReasons().empty(), "Unsupported media accepted");
    }
    for (auto key : {"color_primaries", "color_space", "color_range", "chroma_location"}) {
        auto absent = j;
        absent["streams"][0].erase(key);
        check(MediaInfo::Parse(absent).UnsupportedReasons().empty(), "Optional tag blocked HLG ProRes");
    }
    auto unspecified = j;
    for (auto key : {"color_primaries", "color_space", "color_range", "chroma_location"})
        unspecified["streams"][0].erase(key);
    const auto assumed = MediaInfo::Parse(unspecified);
    const auto interpretation = assumed.InputInterpretation();
    check(assumed.InputWarnings().size() == 4 && interpretation["warnings"].size() == 4,
          "Missing source declarations must produce four traceable assumptions");
    check(interpretation["assumed"] == Json({{"primaries", "bt2020"}, {"matrix", "bt2020nc"},
                                            {"range", "tv"}, {"chroma_location", "left"}}) &&
              interpretation["overridden"].empty() && interpretation["conflicting_fields"].empty(),
          "Input assumption report is incorrect");
    check(assumed.EffectiveRange() == "tv" && assumed.EffectiveChromaLocation() == "left",
          "Missing range/chroma defaults changed");
    for (const char* unknown : {"unknown", "unspecified"}) {
        auto missingTags = unspecified;
        for (const char* key : {"color_primaries", "color_space", "color_range", "chroma_location"})
            missingTags["streams"][0][key] = unknown;
        check(MediaInfo::Parse(missingTags).UnsupportedReasons().empty() &&
                  MediaInfo::Parse(missingTags).InputWarnings().size() == 4,
              "Unspecified tag differs from absent tag policy");
    }
    check(m.InputWarnings().empty() && m.InputInterpretation()["origin"]["range"] == "declared-limited",
          "Declared source tags are incorrectly reported as assumptions");
    auto full = j;
    full["streams"][0]["color_range"] = "pc";
    check(MediaInfo::Parse(full).UnsupportedReasons().empty() &&
              MediaInfo::Parse(full).InputInterpretation()["origin"]["range"] == "declared-full",
          "Explicit full range is not preserved");
    for (const auto& [key, value, field] : std::vector<std::tuple<std::string, std::string, std::string>>{
             {"color_primaries", "bt709", "primaries"}, {"color_space", "bt709", "matrix"},
             {"color_transfer", "smpte2084", "transfer"}, {"color_range", "reserved", "range"},
             {"chroma_location", "topleft", "chroma_location"}}) {
        auto bad = j;
        bad["streams"][0][key] = value;
        auto media = MediaInfo::Parse(bad);
        check(!media.UnsupportedReasons().empty() &&
                  media.InputInterpretation()["conflicting_fields"] == Json::array({field}),
              "Explicit conflicting tag was silently accepted");
        media.forceBT2020Interpretation = true;
        if (field == "primaries" || field == "matrix") {
            check(media.UnsupportedReasons().empty() && media.InputWarnings().size() == 1 &&
                      media.InputInterpretation()["overridden"].contains(field) &&
                      media.InputInterpretation()["declared"][field] == value,
                  "Explicit override must record original declaration and effective interpretation");
        } else {
            check(!media.UnsupportedReasons().empty(), "Gamut override bypassed non-gamut safety contract");
        }
    }
    auto extraVideo = j;
    extraVideo["streams"].push_back(j["streams"][0]);
    check(!MediaInfo::Parse(extraVideo).UnsupportedReasons().empty(), "Second main video silently discarded");
    extraVideo["streams"].back()["disposition"]["attached_pic"] = 1;
    check(MediaInfo::Parse(extraVideo).UnsupportedReasons().empty(), "Trailing cover art blocks primary video");
    extraVideo["streams"][0]["disposition"]["attached_pic"] = 1;
    extraVideo["streams"].back()["disposition"]["attached_pic"] = 0;
    check(!MediaInfo::Parse(extraVideo).UnsupportedReasons().empty(), "Attached picture selected as main video");
    auto wrongContainer = j;
    wrongContainer["format"]["format_name"] = "matroska,webm";
    check(!MediaInfo::Parse(wrongContainer).UnsupportedReasons().empty(), "Non-MOV input enters MOV parser");
    wrongContainer = j;
    wrongContainer["format"]["tags"]["major_brand"] = "isom";
    check(!MediaInfo::Parse(wrongContainer).UnsupportedReasons().empty(), "Explicit non-QuickTime brand accepted");
    wrongContainer["format"]["tags"].erase("major_brand");
    check(MediaInfo::Parse(wrongContainer).UnsupportedReasons().empty(), "Legacy MOV without ftyp rejected");
    auto missing = j;
    missing["streams"][0].erase("color_transfer");
    check(!MediaInfo::Parse(missing).UnsupportedReasons().empty(), "Missing HLG tag accepted");
    auto vfr = j;
    vfr["streams"][0]["avg_frame_rate"] = "25/1";
    check(MediaInfo::Parse(vfr).UnsupportedReasons().empty(),
          "Rate tags must not decide VFR before packet verification");
    auto cameraRate = j;
    cameraRate["streams"][0]["r_frame_rate"] = "24/1";
    cameraRate["streams"][0]["avg_frame_rate"] = "83360/3473";
    auto camera = MediaInfo::Parse(cameraRate);
    check(camera.UnsupportedReasons().empty(), "Small camera clock correction rejected");
    Near(camera.fps.Value(), 24, 0, "Camera nominal rate must remain 24 fps");
    Near(camera.averageFps.Value(), 83360.0 / 3473, 1e-12, "Raw average rate must remain inspectable");
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
    for (auto [stream, key, value] :
         std::vector<std::tuple<size_t, std::string, Json>>{{0, "width", 1e30},
                                                            {0, "height", -1},
                                                            {0, "width", 12.5},
                                                            {0, "bits_per_raw_sample", "999999999999"},
                                                            {1, "channels", 1e30},
                                                            {1, "sample_rate", "999999999999"},
                                                            {0, "nb_frames", "9223372036854775808"},
                                                            {0, "nb_frames", "12junk"},
                                                            {0, "nb_frames", "-1"}}) {
        auto bad = j;
        bad["streams"][stream][key] = value;
        bool rejected = false;
        try {
            MediaInfo::Parse(bad);
        } catch (const AppError&) {
            rejected = true;
        }
        check(rejected, "Overflow/fractional numeric metadata was accepted");
    }
    auto exact = j;
    exact["streams"][0]["nb_frames"] = "9007199254740993";
    check(MediaInfo::Parse(exact).frames == 9007199254740993LL,
          "Frame count lost integer precision through double");
    auto output = j;
    output["streams"][0].erase("color_transfer");
    output["format"]["tags"]["logforge.transfer"] = "Apple Log";
    check(ValidateOutput(m, validated(output), 30).passed, "Valid output rejected");
    for (auto [key, value] : std::vector<std::pair<std::string, Json>>{{"pix_fmt", "yuv420p"},
                                                                       {"codec_name", "h264"},
                                                                       {"color_primaries", "bt709"},
                                                                       {"color_transfer", "arib-std-b67"},
                                                                       {"profile", "Standard"},
                                                                       {"width", 1280},
                                                                       {"nb_frames", "29"}}) {
        auto bad = output;
        bad["streams"][0][key] = value;
        check(!ValidateOutput(m, validated(bad), 30).passed, "Incorrect output validated");
    }
    auto noAudio = output;
    noAudio["streams"].erase(1);
    check(!ValidateOutput(m, validated(noAudio), 30).passed, "Lost audio validated");
    auto wrongAudio = output;
    wrongAudio["streams"][1]["sample_rate"] = "44100";
    check(!ValidateOutput(m, validated(wrongAudio), 30).passed, "Changed sample rate validated");
    for (auto [key, value] : std::vector<std::pair<std::string, Json>>{
             {"codec_name", "pcm_s16le"}, {"channels", 1}, {"channel_layout", "downmix"}}) {
        auto bad = output;
        bad["streams"][1][key] = value;
        check(!ValidateOutput(m, validated(bad), 30).passed, "Changed audio format validated");
    }
    auto unlabelledInput = j, unlabelledOutput = output;
    unlabelledInput["streams"][1].erase("channel_layout");
    unlabelledOutput["streams"][1].erase("channel_layout");
    const auto unlabelled = validated(unlabelledInput);
    check(ValidateOutput(unlabelled, validated(unlabelledOutput), 30).passed,
          "Preserved unlabelled audio rejected");
    const auto inventedLayout = ValidateOutput(unlabelled, validated(output), 30);
    check(!inventedLayout.passed && inventedLayout.errors.size() == 1 &&
              inventedLayout.errors[0].id == TextId::AudioLayoutChanged,
          "Invented layout must produce a specific diagnostic");
    check(!ValidateOutput(m, validated(unlabelledOutput), 30).passed,
          "Lost explicit channel layout validated");
    auto driftingOutput = output;
    driftingOutput["streams"][0]["avg_frame_rate"] = "29971/1000";
    check(!ValidateOutput(m, validated(driftingOutput), 30).passed,
          "Output average-rate drift hidden by nominal rate");
    auto metadata = AppleLogMetadataWriter::Arguments(m);
    check(std::find(metadata.begin(), metadata.end(), L"2") != metadata.end(),
          "Unspecified transfer missing");
    for (const auto* chroma : {"left", "center"}) {
        auto supported = j;
        supported["streams"][0]["chroma_location"] = chroma;
        check(MediaInfo::Parse(supported).UnsupportedReasons().empty(), "Supported chroma rejected");
    }
    auto unknown = j;
    unknown["streams"][0]["chroma_location"] = "unknown";
    check(MediaInfo::Parse(unknown).UnsupportedReasons().empty() &&
              MediaInfo::Parse(unknown).EffectiveChromaLocation() == "left",
          "Camera compatibility chroma default failed");
    auto explicitSiting = MediaInfo::Parse(unknown);
    explicitSiting.inputChromaOverride = "center";
    check(explicitSiting.UnsupportedReasons().empty() && explicitSiting.EffectiveChromaLocation() == "center",
          "Explicit siting failed");
    check(explicitSiting.InputInterpretation()["overridden"]["chroma_location"] == "center" &&
              explicitSiting.InputInterpretation()["origin"]["chroma_location"] == "explicit-override",
          "Explicit chroma choice is not recorded");
    explicitSiting.chromaLocation = "topleft";
    check(explicitSiting.EffectiveChromaLocation() == "topleft" && !explicitSiting.UnsupportedReasons().empty(),
          "Override replaced a native declaration");
    auto wrongChroma = output;
    wrongChroma["streams"][0]["chroma_location"] = "center";
    check(!ValidateOutput(m, validated(wrongChroma), 30).passed, "Wrong output chroma validated");
    for (const auto* key :
         {"com.apple.proapps.customgamma", "HLG", "HDR_format", "DolbyVision", "PQ_transfer", "colorspace"}) {
        auto conflict = output;
        conflict["streams"][0]["tags"][key] = "stale";
        check(!ValidateOutput(m, validated(conflict), 30).passed, "Stream color conflict accepted");
        conflict = output;
        conflict["format"]["tags"][key] = "stale";
        check(!ValidateOutput(m, validated(conflict), 30).passed, "Format color conflict accepted");
    }
    auto safe = m;
    safe.videoTags["com.apple.proapps.customgamma"] = "HLG";
    safe.tags["private_unknown"] = "do not copy";
    const auto plan = AppleLogMetadataWriter::CopyPlan(safe);
    check(plan["removed"].size() >= 3 && !plan["preserved"].empty(), "Whitelist copy plan missing");
    const auto args = AppleLogMetadataWriter::Arguments(safe);
    check(std::find(args.begin(), args.end(), L"com.apple.proapps.customgamma=HLG") == args.end(),
          "Custom gamma copied");
    check(std::find(args.begin(), args.end(), L"private_unknown=do not copy") == args.end(),
          "Unknown metadata copied");
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
        Near(logforge::HLGToAppleLog(0.75), A::EncodeLinearToAppleLog(1.0), 1e-12,
             "BT.2408 75% HLG is 100% reference white");
        // 18% gray is derived from the 100% reference white in BT.2408 Table 1.
        const double hlgGray =
            logforge::HLG::EncodeSceneLinear(0.18 * logforge::HLG::DecodeToSceneLinear(0.75));
        Near(hlgGray, 0.3782588830779046, 1e-14, "Independent BT.2408 gray reference");
        Near(logforge::HLGToAppleLog(hlgGray), A::EncodeLinearToAppleLog(0.18), 1e-12,
             "BT.2408 gray must land at Apple reference gray");
        for (double ev : {-2.0, -0.5, 0.0, 0.5, 2.0}) {
            Near(A::DecodeAppleLogToLinear(logforge::HLGToAppleLog(hlgGray, ev)), 0.18 * std::exp2(ev), 1e-12,
                 "Exposure is a scene-linear gain");
            Near(logforge::HLGToAppleLog(0, ev), A::EncodeLinearToAppleLog(0), 1e-12,
                 "Exposure must not lift the encoded black floor");
        }
        for (double ev :
             {-9.0, 9.0, std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()}) {
            bool badExposure = false;
            try {
                logforge::ValidateExposureStops(ev);
            } catch (...) {
                badExposure = true;
            }
            check(badExposure, "Invalid exposure accepted");
        }
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
        // Creative rendering is independently tested; it must never change the
        // published Apple Log curve or the disabled conversion path.
        logforge::ToneAdjustments tone;
        const std::array<double, 3> color{0.08, 0.2, 0.4};
        check(logforge::AdjustSceneLinearBT2020(color, tone) == color, "Disabled tone is not exact identity");
        tone.enabled = true;
        const auto luma = [](const std::array<double, 3>& rgb) {
            return .2627 * rgb[0] + .678 * rgb[1] + .0593 * rgb[2];
        };
        for (double strength : {0.0, 1.0, 3.0}) {
            tone.shadowStops = tone.highlightStops = strength;
            double last = -1;
            for (int i = 0; i <= 10000; ++i) {
                const double y = .18 * std::exp2(-24 + 48.0 * i / 10000);
                const auto out = logforge::AdjustSceneLinearBT2020({y, y, y}, tone);
                check(out[0] > last, "Creative luminance curve lost monotonicity");
                Near(out[0], out[1], 1e-9, "Creative curve tints neutrals");
                Near(out[1], out[2], 1e-9, "Creative curve tints neutrals");
                last = out[0];
            }
            Near(logforge::AdjustSceneLinearBT2020({.18, .18, .18}, tone)[0], .18, 1e-14,
                 "Creative curve moved reference gray");
            Near(logforge::AdjustSceneLinearBT2020({0, 0, 0}, tone)[0], 0, 0,
                 "Creative curve invents signal at black");
        }
        tone = {true, 3, 1, 1};
        Near(logforge::AdjustSceneLinearBT2020({.001, .001, .001}, tone)[0], .008, 1e-15,
             "Deep shadows should receive 3 stops");
        Near(logforge::AdjustSceneLinearBT2020({12, 12, 12}, tone)[0], 6, 1e-12,
             "High highlights should lose 1 stop");
        const auto unchangedSaturation = logforge::AdjustSceneLinearBT2020(color, tone);
        tone.saturation = .85;
        const auto desaturated = logforge::AdjustSceneLinearBT2020(color, tone);
        Near(luma(unchangedSaturation), luma(desaturated), 1e-14, "Saturation altered scene luminance");
        Near(desaturated[2] - desaturated[0], .85 * (unchangedSaturation[2] - unchangedSaturation[0]), 1e-14,
             "Saturation is not linear chroma scaling");
        tone.saturation = 0;
        const auto mono = logforge::AdjustSceneLinearBT2020(color, tone);
        Near(mono[0], mono[1], 0, "Zero saturation is not monochrome");
        Near(mono[1], mono[2], 0, "Zero saturation is not monochrome");
        for (const auto bad :
             {logforge::ToneAdjustments{true, 3.1, 1, 1}, logforge::ToneAdjustments{false, 0, -1, 1},
              logforge::ToneAdjustments{true, 0, 0, 1.51},
              logforge::ToneAdjustments{true, 0, 0, std::numeric_limits<double>::quiet_NaN()}}) {
            bool refused = false;
            try {
                logforge::ValidateToneAdjustments(bad);
            } catch (...) {
                refused = true;
            }
            check(refused, "Invalid creative parameters accepted");
        }
        float baseline[]{.02f, .1f, .02f, .1f, .02f, .1f};
        float disabled[]{.02f, .1f, .02f, .1f, .02f, .1f};
        logforge::TransformHLGToAppleLog(baseline);
        logforge::TransformHLGToAppleLog(disabled, 0, {false, 0, 3, 0});
        check(std::equal(std::begin(baseline), std::end(baseline), std::begin(disabled)),
              "Disabled adjustments changed pixels");
        logforge::SignalStatistics signal;
        float extremes[]{-.5f, 0, 1.5f};
        logforge::TransformHLGToAppleLog(extremes, 0, {}, &signal);
        check(signal.samples == 3 && signal.appleFloorClipped == 1 && signal.aboveNominalWhite == 1,
              "Signal range counters failed");
        Near(signal.inputMinimum, -.5, 0, "Input range counter");
        Near(signal.outputMinimum, 0, 0, "Output range counter");
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
