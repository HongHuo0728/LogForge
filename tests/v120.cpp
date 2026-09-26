#include "logforge/PixelSanity.h"
#include "logforge/Queue.h"
#include "logforge/Settings.h"
#include "logforge/StorageSafety.h"
#include <future>
#include <iostream>

using namespace logforge;
namespace {
void Check(bool ok, const char* message) {
    if (!ok)
        throw std::runtime_error(message);
}
fs::path Self() {
    return ExecutableDirectory() / L"logforge_v120_tests.exe";
}
void Semantics() {
    Check(SameMetadataValue("creation_time", "2024-05-01T12:00:00.000Z", "2024-05-01T12:00:00.000000Z"),
          "Fraction formatting");
    Check(SameMetadataValue("com.apple.quicktime.creationdate", "2024-05-01T20:00:00.123+0800",
                            "2024-05-01T12:00:00.123000Z"),
          "Zone semantic equality");
    Check(!SameMetadataValue("creation_time", "2024-05-01T12:00:00.123001Z", "2024-05-01T12:00:00.123Z"),
          "Different instants accepted");
    Check(!SameMetadataValue("creation_time", "2024-02-30T12:00:00Z", "2024-03-01T12:00:00Z"),
          "Invalid calendar accepted");
    for (auto text :
         {"0: 65536 0 0\n1: 0 65536 0\n2: 0 0 1073741824", "0: 0 -65536 0\n1: 65536 0 0\n2: 0 0 1073741824",
          "0: -65536 0 0\n1: 0 -65536 0\n2: 0 0 1073741824",
          "0: 0 65536 0\n1: -65536 0 0\n2: 0 0 1073741824"})
        Check(SupportedDisplayMatrix(text), "Unit rotation rejected");
    for (auto text :
         {"0: -65536 0 0\n1: 0 65536 0\n2: 0 0 1073741824", "0: 32768 0 0\n1: 0 65536 0\n2: 0 0 1073741824",
          "0: 65536 0 0\n1: 0 65536 0\n2: 100 0 1073741824", "broken"})
        Check(!SupportedDisplayMatrix(text), "Unsupported matrix accepted");
    MediaInfo m;
    m.raw = Json::object();
    for (const auto& edits :
         {Json::array(),
          Json::array(
              {{{"media_time", -1}, {"duration_ticks", 1}, {"rate_integer", 1}, {"rate_fraction", 0}}}),
          Json::array(
              {{{"media_time", 0}, {"duration_ticks", 1}, {"rate_integer", 0}, {"rate_fraction", 0}}})}) {
        bool refused = false;
        try {
            InspectMovTimeline(m, {{"atoms", Json::array({{{"type", "elst"},
                                                           {"path", "/moov[0]/trak[0]/edts[0]/elst[0]"},
                                                           {"edits", edits}}})}});
        } catch (...) {
            refused = true;
        }
        Check(refused, "Unsafe edit list accepted");
    }
}
void Storage() {
    const auto root = DataDirectory() / L"storage-fixtures";
    fs::create_directories(root);
    const auto orphan = root / L"owned.logforge-test.partial.mov",
               user = root / L"user.logforge-test.partial.mov";
    {
        std::ofstream f(user);
        f << "user content must remain";
    }
    auto child = RunProcess(Self(), {L"--orphan", orphan.wstring()}, nullptr, 10);
    Check(child.exitCode == 0 && fs::exists(orphan), "Crash fixture missing");
    MaintainOwnedStorage();
    Check(!fs::exists(orphan) && fs::exists(user),
          "Ownership cleanup deleted a user file or missed owned stale data");
    RunProcess(Self(), {L"--orphan", orphan.wstring()}, nullptr, 10);
    {
        Handle locked(
            CreateFileW(orphan.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr));
        Check(bool(locked), "Stale lock fixture failed");
        MaintainOwnedStorage();
        Check(fs::exists(orphan), "Locked stale media deleted");
    }
    MaintainOwnedStorage();
    Check(!fs::exists(orphan), "Locked stale file journal was lost before retry");
    const auto logs = DataDirectory() / L"logs";
    fs::create_directories(logs);
    const auto expired = logs / L"LogForge-retention.log", untouched = logs / L"user-notes.log";
    for (const auto& p : {expired, untouched}) {
        std::ofstream(p) << "fixture";
        fs::last_write_time(p, fs::file_time_type::clock::now() - std::chrono::hours(24 * 31));
    }
    MaintainOwnedStorage();
    Check(!fs::exists(expired) && fs::exists(untouched), "Retention ownership policy failed");
    fs::remove(untouched);
    std::vector<std::future<ProcessResult>> jobs;
    for (int i = 0; i < 4; ++i)
        jobs.push_back(std::async(std::launch::async, [i] {
            return RunProcess(Self(), {L"--state-writer", std::to_wstring(i)}, nullptr, 40);
        }));
    for (auto& job : jobs)
        Check(job.get().exitCode == 0, "Concurrent state writer failed");
    Check(!SettingsStore::Load().manualFFmpeg.empty(), "Preference writes overwrote FFmpeg path");
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 8; ++j)
            Check(ToolTrust::HasApproval(root / (std::to_wstring(i) + L"-" + std::to_wstring(j)) /
                                         L"ffmpeg.exe"),
                  "Trust record lost across processes");
    fs::remove(user);
}
int Writer(int n) {
    const auto root = DataDirectory() / L"storage-fixtures";
    for (int i = 0; i < 8; ++i) {
        if (n % 2) {
            auto settings = SettingsStore::Load();
            settings.theme = i % 2 ? Theme::Dark : Theme::Light;
            SettingsStore::SavePreferences(settings);
        } else
            SettingsStore::SaveFFmpeg(root / L"saved/ffmpeg.exe", false);
        const auto dir = root / (std::to_wstring(n) + L"-" + std::to_wstring(i));
        fs::create_directories(dir);
        for (auto name : {L"ffmpeg.exe", L"ffprobe.exe"})
            fs::copy_file(ExecutableDirectory() / L"logforge_fake_ffmpeg.exe", dir / name,
                          fs::copy_options::overwrite_existing);
        ToolTrust::ApproveManual(ToolTrust::Inspect(dir / L"ffmpeg.exe")); // Hash only: fake never executed.
    }
    return 0;
}
void Pipeline(const fs::path& ff) {
    std::atomic_bool cancel = false;
    Logger logger;
    ToolTrust::ApproveManual(ToolTrust::Inspect(ff));
    auto tools = FFmpegManager(logger).Check(ff, cancel);
    const auto root = DataDirectory() / L"media";
    fs::create_directories(root);
    auto run = [&](const std::vector<std::wstring>& args) {
        auto p = RunProcess(ff, args, &cancel, 40);
        Check(p.exitCode == 0, p.error.c_str());
    };
    const auto base = root / L"source.mov";
    run({L"-v",
         L"error",
         L"-y",
         L"-f",
         L"lavfi",
         L"-i",
         L"color=gray:size=128x64:rate=30000/1001",
         L"-f",
         L"lavfi",
         L"-i",
         L"sine=sample_rate=48000:duration=0.4004",
         L"-frames:v",
         L"12",
         L"-c:v",
         L"prores_ks",
         L"-profile:v",
         L"3",
         L"-pix_fmt",
         L"yuv422p10le",
         L"-vf",
         L"setparams=color_primaries=bt2020:color_trc=arib-std-b67:colorspace=bt2020nc:range=limited",
         L"-color_primaries",
         L"bt2020",
         L"-color_trc",
         L"arib-std-b67",
         L"-colorspace",
         L"bt2020nc",
         L"-c:a",
         L"pcm_s24le",
         L"-threads:v",
         L"2",
         base.wstring()});
    TranscodeOptions options;
    options.backend = ProcessingBackend::CPU;
    Json results = Json::array();
    auto convert = [&](const fs::path& in, const std::wstring& name, const TranscodeOptions& opt) {
        auto m = Probe(tools.ffprobe, in);
        m.inputChromaOverride = "left";
        auto out = root / (name + L".mov");
        std::error_code ec;
        fs::remove(out, ec);
        TextId complete = TextId::Starting;
        auto result = TranscodeJob::Run(
            tools, m, out, logger, cancel, [&](const JobProgress& p) { complete = p.stage.id; }, opt);
        Check(result.passed && result.signal.at("pixel_sanity").at("passed") == true,
              "Pixel/metadata validation failed");
        Check(complete == (opt.tone.enabled ? TextId::CompleteCreative : TextId::CompleteStandard),
              "Completion mode is wrong");
        return result;
    };
    for (int rotation : {0, 90, -90, 180, 270}) {
        const auto input = root / (L"portrait-" + std::to_wstring(rotation) + L".mov");
        run({L"-v",
             L"error",
             L"-y",
             L"-display_rotation:v:0",
             std::to_wstring(rotation),
             L"-i",
             base.wstring(),
             L"-map",
             L"0",
             L"-c",
             L"copy",
             L"-metadata",
             L"creation_time=2024-05-01T12:00:00.000Z",
             L"-metadata:s:v:0",
             L"creation_time=2024-05-01T12:00:01Z",
             L"-metadata:s:a:0",
             L"creation_time=2024-05-01T12:00:02Z",
             L"-metadata",
             L"com.apple.quicktime.creationdate=2024-05-01T20:00:00.123+0800",
             L"-timecode",
             L"01:02:03;04",
             L"-movflags",
             L"+write_colr+use_metadata_tags",
             input.wstring()});
        // Actual QTFF structure: deliberately distinct movie/video/audio times.
        // FFmpeg's muxer ignores per-track creation_time options, so construct
        // the fixture independently by patching documented header fields.
        const auto atoms = ReferenceMovAnalyzer::Analyze(input);
        std::fstream fixture(input, std::ios::binary | std::ios::in | std::ios::out);
        unsigned n = 0;
        for (const auto& atom : atoms.at("atoms"))
            if (atom.value("type", "") == "mdhd" || atom.value("type", "") == "tkhd") {
                const auto v = atom.at("creation_time_1904_seconds").get<uint64_t>() + ++n;
                const unsigned length = atom.at("version") == 1 ? 8 : 4;
                char bytes[8]{};
                for (unsigned i = 0; i < length; ++i)
                    bytes[i] = static_cast<char>(v >> ((length - i - 1) * 8));
                fixture.seekp(atom.at("offset").get<std::streamoff>() +
                              atom.at("header_size").get<std::streamoff>() + 4);
                fixture.write(bytes, length);
                Check(bool(fixture), "Creation time fixture patch failed");
            }
        fixture.close();
        auto report = convert(input, L"converted-" + std::to_wstring(rotation), options);
        for (const auto& tag : report.metadata["preserved"])
            Check(tag["verified_preserved"] == true, "Creation timestamp lost");
        results.push_back({{"rotation", rotation}, {"passed", true}, {"stages", report.timing["stages"]}});
    }
    options.tone.enabled = true;
    convert(base, L"creative", options);
    options.tone.enabled = false;
    {
        auto m = Probe(tools.ffprobe, base);
        PixelSanity wrong(m, 12);
        std::vector<float> white(128 * 64 * 3 * 12, 1.0f);
        wrong.Observe(white);
        Check(wrong.Validate(tools.ffmpeg, root / L"converted-0.mov", cancel, 0, {}).at("passed") == false,
              "Pixel sanity accepted a grossly incorrect transfer/range signal");
    }
    auto reject = [&](const fs::path& input, const std::wstring& name, const JobIOHooks& hook,
                      TextId expected, ProcessingBackend backend = ProcessingBackend::CPU) {
        auto opt = options;
        opt.io = &hook;
        opt.backend = backend;
        const auto target = root / (name + L".mov");
        bool failed = false;
        try {
            convert(input, name, opt);
        } catch (const AppError& e) {
            failed = e.message.id == expected;
        }
        Check(failed && !fs::exists(target), "Failure published an output or gave wrong diagnostic");
    };
    JobIOHooks space;
    space.availableSpace = [](const fs::path&) { return uintmax_t{0}; };
    reject(base, L"disk-full", space, TextId::OutputSpace);
    JobIOHooks report;
    report.beforeReportWrite = [](const fs::path& p) { fs::create_directory(p); };
    reject(base, L"report-failed", report, TextId::ReportSave);
    int checks = 0;
    JobIOHooks remux;
    remux.availableSpace = [&](const fs::path&) { return ++checks == 1 ? UINTMAX_MAX : 0; };
    reject(root / L"portrait-90.mov", L"rotation-full", remux, TextId::RotationSpace);
    JobIOHooks gpu;
    gpu.cudaCheckpoint = [](const char*) {
        throw std::runtime_error("Injected CUDA initialization/OOM failure");
    };
    options.io = &gpu;
    options.backend = ProcessingBackend::Auto;
    auto fallback = convert(base, L"cuda-fallback", options);
    Check(fallback.timing["backend"].contains("fallback_reason"), "Auto did not report CPU fallback");
    reject(base, L"forced-cuda-failure", gpu, TextId::BackendFailed, ProcessingBackend::CUDA);
    options.io = nullptr;
    options.backend = ProcessingBackend::CPU;
    bool cudaAvailable = false;
    try {
        cudaAvailable = CudaTransformer::Qualify().at("qualification").at("passed");
    } catch (...) {
    }
    if (cudaAvailable) {
        JobIOHooks runtime;
        runtime.cudaCheckpoint = [](const char* at) {
            if (std::string(at) == "transform")
                throw std::runtime_error("Injected CUDA runtime OOM");
        };
        options.backend = ProcessingBackend::Auto;
        options.io = &runtime;
        const auto resumed = convert(base, L"cuda-runtime-fallback", options);
        Check(resumed.timing.at("backend").at("backend") == "cpu after cuda",
              "Runtime error did not fall back safely");
        reject(base, L"cuda-runtime-forced", runtime, TextId::ProcessingFailed, ProcessingBackend::CUDA);
        options.io = nullptr;
        options.backend = ProcessingBackend::CPU;
    }
    {
        const auto metadata = root / L"chapters.txt", chapters = root / L"chapters.mov";
        std::ofstream(metadata) << ";FFMETADATA1\n[CHAPTER]\nTIMEBASE=1/"
                                   "1000\nSTART=0\nEND=200\ntitle=First\n[CHAPTER]\nTIMEBASE=1/"
                                   "1000\nSTART=200\nEND=400\ntitle=Second\n";
        run({L"-v", L"error", L"-y", L"-i", base.wstring(), L"-i", metadata.wstring(), L"-map", L"0:v:0",
             L"-map", L"0:a?", L"-c", L"copy", L"-map_chapters", L"1", chapters.wstring()});
        auto chaptersReport = convert(chapters, L"chapters-converted", options);
        Check(!chaptersReport.metadata.at("timeline_policy").at("removed_streams").empty(),
              "Chapter data-track regeneration was not recorded");
    }
    for (bool mirror : {false, true}) {
        const auto bad = root / (mirror ? L"mirrored.mov" : L"trimmed.mov");
        fs::copy_file(base, bad, fs::copy_options::overwrite_existing);
        auto atoms = ReferenceMovAnalyzer::Analyze(bad);
        bool changed = false;
        std::fstream f(bad, std::ios::binary | std::ios::in | std::ios::out);
        for (const auto& a : atoms.at("atoms"))
            if (!changed && a.value("type", "") == (mirror ? "tkhd" : "elst")) {
                const auto offset =
                    a.at("offset").get<std::streamoff>() + a.at("header_size").get<std::streamoff>();
                if (mirror) {
                    char v[]{-1, -1, 0, 0};
                    f.seekp(offset + 40);
                    f.write(v, 4);
                } else {
                    char v[]{0, 0, 0, 1};
                    f.seekp(offset + 12);
                    f.write(v, 4);
                }
                changed = true;
            }
        f.close();
        Check(changed, "MOV matrix/edit-list fixture missing");
        JobIOHooks none;
        reject(bad, mirror ? L"refused-mirror" : L"refused-trim", none,
               mirror ? TextId::InputRejected : TextId::InputEditList);
    }
    auto queue = RunQueue(tools, {root / L"missing.mov", base}, root, logger, cancel, options, "left",
                          [](auto, auto, const auto&, const auto&) {});
    Check(queue["failed"] == 1 && queue["successful"] == 1, "Failed queue item prevented later conversion");
    std::ofstream output(DataDirectory() / L"v120-report.json");
    output << Json{{"portrait", results}, {"fault_paths_passed", true}, {"queue", queue}}.dump(2);
    for (const auto& e : fs::directory_iterator(root))
        fs::remove(e.path());
    fs::remove(root);
}
} // namespace
int wmain(int argc, wchar_t** argv) {
    try {
        const std::wstring group = argc > 1 ? argv[1] : L"unit";
        if (group == L"--state-writer")
            return Writer(std::stoi(argv[2]));
        if (group == L"--orphan") {
            std::ofstream f(argv[2]);
            f << "owned fixture";
            f.close();
            OwnedJobFiles owner;
            owner.Track(argv[2]);
            ExitProcess(0);
        }
        if (group == L"unit") {
            Semantics();
            Storage();
        } else if (group == L"pipeline")
            Pipeline(argv[2]);
        else if (group == L"cuda") {
            auto report = CudaTransformer::Qualify();
            Check(report.at("qualification").at("passed") == true, "CUDA not qualified");
            std::ofstream(DataDirectory() / L"cuda-qualification.json") << report.dump(2);
        } else
            throw std::runtime_error("Unknown test");
        std::cout << "PASS: v1.2.0 regression group\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
