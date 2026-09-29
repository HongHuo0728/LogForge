#include "logforge/Queue.h"
#include "logforge/StorageSafety.h"
#include "logforge/AudioPayload.h"
#include <algorithm>
#include <iostream>
using namespace logforge;
namespace {
void Check(bool ok, const char* what) { if (!ok) throw std::runtime_error(what); }
void Unit() {
    Json devices = Json::array({{{"ordinal", 0}, {"uuid", "b"}, {"eligible", true}, {"total_memory_bytes", 8}, {"compute_score", 86}},
                               {{"ordinal", 1}, {"uuid", "a"}, {"eligible", true}, {"total_memory_bytes", 8}, {"compute_score", 86}},
                               {{"ordinal", 2}, {"uuid", "c"}, {"eligible", false}, {"total_memory_bytes", 24}, {"compute_score", 61}},
                               {{"ordinal", 3}, {"uuid", "d"}, {"eligible", true}, {"total_memory_bytes", 16}, {"compute_score", 75}}});
    const auto ranked = RankCudaCandidates(devices);
    std::reverse(devices.begin(), devices.end());
    Check(ranked == RankCudaCandidates(devices) && ranked[0]["uuid"] == "d" && ranked[1]["uuid"] == "a" &&
          ranked[3]["eligible"] == false, "GPU selection depends on enumeration order");
    const auto root = DataDirectory() / L"queue-preflight";
    fs::create_directories(root);
    const auto existing = root / L"IMG_0001_AppleLog.mov";
    std::ofstream(existing) << "preserve existing";
    const std::vector<fs::path> inputs{root / L"A/IMG_0001.MOV", root / L"B/IMG_0001.MOV",
                                      root / L"C/img_0001.mov"};
    const auto plan = PlanQueue(inputs, root);
    Check(plan[0].output.filename() == L"IMG_0001_AppleLog_2.mov" &&
          plan[1].output.filename() == L"IMG_0001_AppleLog_3.mov" &&
          plan[2].output.filename() == L"img_0001_AppleLog_4.mov", "Queue collision preflight failed");
    Check(QueuePlanJson(plan) == QueuePlanJson(PlanQueue(inputs, root)), "Queue plan is nondeterministic");
    fs::remove(existing); fs::remove(root);
    ValidationReport r;
    Check(r.passed && !r.Completed() && !r.ToJson().at("completed"), "Unpublished media marked complete");
    r.publication["published"] = true;
    Check(r.Completed(), "Published valid media not complete");
    r.passed = false;
    Check(!r.Completed(), "Invalid media marked complete");
    MediaInfo m;
    m.timecode = "01:00:00:00";
    m.raw = {{"streams", Json::array({{{"index", 0}, {"codec_type", "video"}},
              {{"index", 1}, {"codec_type", "audio"}},
              {{"index", 2}, {"codec_type", "video"}, {"disposition", {{"attached_pic", 1}}}},
              {{"index", 3}, {"codec_type", "data"}, {"codec_tag_string", "mebx"}},
              {{"index", 4}, {"codec_type", "subtitle"}},
              {{"index", 5}, {"codec_type", "data"}, {"codec_tag_string", "tmcd"}}})}};
    auto streams = InspectMovTimeline(m, {{"atoms", Json::array()}});
    Check(streams["preserved_streams"].size() == 2 && streams["removed_streams"].size() == 3 &&
          streams["regenerated_streams"].size() == 1, "Stream map/report mismatch");
    m.timecode.clear();
    Check(InspectMovTimeline(m, {{"atoms", Json::array()}})["removed_streams"].size() == 4,
          "Unmapped timecode silently omitted");
}
void CudaCache() {
    Json results;
    {
        CudaTransformer fresh(0, {}, true);
        results["fresh"] = fresh.Report();
        Check(!results["fresh"]["qualification_cache"]["hit"], "Explicit qualification used cache");
    }
    {
        CudaTransformer cached(0, {});
        results["cached"] = cached.Report();
        Check(results["cached"]["qualification_cache"]["hit"], "Qualified device not cached in process");
        const auto& key = results["cached"]["qualification_identity"];
        Check(key.at("ptx_sha256").get<std::string>().size() == 64 && key.at("build") == BuildNumber &&
              key.at("version") == Version && !key.at("device_uuid").get<std::string>().empty() &&
              key.contains("driver") && key.contains("driver_file_version") && key.contains("algorithm"),
              "Qualification identity missing required binding");
    }
    {
        ToneAdjustments tone; tone.saturation = .84;
        CudaTransformer changed(0, tone);
        results["changed_grade"] = changed.Report();
        Check(!results["changed_grade"]["qualification_cache"]["hit"], "Changed Creative parameters reused qualification");
    }
    // A failed operation must invalidate the cached qualification without
    // committing any caller pixels. This tests the actual CUDA error boundary.
    {
        ToneAdjustments tone; tone.enabled = true;
        CudaTransformer failed(0, tone);
        std::vector<float> incomplete{.2f, .3f};
        const auto before = incomplete;
        SignalStatistics stats;
        bool rejected = false;
        try { failed.Apply(incomplete, stats); } catch (...) { rejected = true; }
        Check(rejected && incomplete == before && stats.samples == 0, "CUDA failure modified input");
    }
    {
        CudaTransformer retry(0, {});
        results["after_failure"] = retry.Report();
        Check(!results["after_failure"]["qualification_cache"]["hit"], "CUDA failure left cached qualification");
    }
    std::ofstream(DataDirectory() / L"cuda-cache.json") << results.dump(2);
}
void CrashChild(const fs::path& ff, const fs::path& input, const fs::path& output, const std::string& stage) {
    std::atomic_bool cancel = false;
    Logger log;
    const auto tools = FFmpegManager(log).Check(ff, cancel);
    auto media = Probe(tools.ffprobe, input);
    // Fault injection into the legacy non-cardinal remux branch only. Normal
    // camera rotations remain baked to pixels and never reach this branch.
    if (stage.starts_with("rotation_")) media.rotation = 45;
    JobIOHooks hooks;
    hooks.stageCheckpoint = [&](const char* at, const fs::path&) { if (stage == at) ExitProcess(86); };
    TranscodeOptions options; options.backend = ProcessingBackend::CPU; options.io = &hooks;
    TranscodeJob::Run(tools, media, output, log, cancel, [](const auto&) {}, options);
    throw std::runtime_error("Crash checkpoint was not reached");
}
void Faults(const fs::path& ff) {
    const auto root = DataDirectory() / L"fault-media";
    fs::create_directories(root);
    const auto input = root / L"source.mov";
    std::atomic_bool cancel = false;
    Logger log;
    ToolTrust::ApproveManual(ToolTrust::Inspect(ff));
    const auto tools = FFmpegManager(log).Check(ff, cancel);
    const auto generated = RunProcess(ff, {L"-v", L"error", L"-y", L"-f", L"lavfi", L"-i",
        L"color=gray:size=128x64:rate=24", L"-f", L"lavfi", L"-i", L"sine=duration=0.5:sample_rate=48000",
        L"-frames:v", L"12", L"-vf", L"format=yuv422p10le,setparams=color_primaries=bt2020:color_trc=arib-std-b67:colorspace=bt2020nc:range=limited",
        L"-c:v", L"prores_ks", L"-profile:v", L"3", L"-threads:v", L"2", L"-c:a", L"pcm_s24le",
        input.wstring()}, &cancel, 30);
    Check(generated.exitCode == 0, "Fault fixture generation failed");
    const auto media = Probe(tools.ffprobe, input);
    TranscodeOptions options; options.backend = ProcessingBackend::CPU;
    Json evidence;
    JobIOHooks hooks;
    options.io = &hooks;
    const auto corrupted = root / L"audio-corrupt.mov";
    hooks.stageCheckpoint = [&](const char* at, const fs::path& partial) {
        if (std::string(at) != "metadata_patched") return;
        const auto packets = RunProcess(tools.ffprobe, {L"-v", L"error", L"-select_streams", L"a:0",
            L"-show_packets", L"-show_entries", L"packet=pos", L"-read_intervals", L"%+#1", L"-of", L"json", partial.wstring()}, &cancel, 15);
        Check(packets.exitCode == 0, "PCM packet location probe failed");
        const auto pos = std::stoll(Json::parse(packets.output).at("packets")[0].at("pos").get<std::string>());
        std::fstream f(partial, std::ios::binary | std::ios::in | std::ios::out);
        f.seekg(pos); char byte = 0; f.read(&byte, 1); byte ^= 1; f.seekp(pos); f.write(&byte, 1);
        Check(bool(f), "Audio corruption injection failed");
    };
    bool rejected = false;
    try { TranscodeJob::Run(tools, media, corrupted, log, cancel, [](const auto&) {}, options); }
    catch (const AppError& e) { rejected = e.message.id == TextId::ValidationFailed; }
    Check(rejected && !fs::exists(corrupted), "Changed PCM payload was published");
    bool foundAudioFailure = false;
    for (const auto& file : fs::directory_iterator(DataDirectory() / L"logs")) {
        if (!file.path().filename().wstring().ends_with(L".validation.json")) continue;
        Json report; std::ifstream(file.path()) >> report;
        if (report.value("final_output", "") != PathText(fs::absolute(corrupted))) continue;
        evidence["audio_corruption"] = report["validation"];
        foundAudioFailure = report["validation"]["metadata"]["audio_copy"]["status"] == "payload_mismatch" &&
                            report["validation"]["completed"] == false;
    }
    Check(foundAudioFailure, "Audio payload failure missing from validation report");
    fs::path replaced, moved;
    hooks.stageCheckpoint = [&](const char* at, const fs::path& partial) {
        if (std::string(at) != "encoded") return;
        replaced = partial; moved = root / L"moved-owned.mov";
        fs::rename(partial, moved);
        std::ofstream(partial) << "unrelated replacement";
        throw std::runtime_error("Injected file identity replacement");
    };
    try { TranscodeJob::Run(tools, media, root / L"replacement.mov", log, cancel, [](const auto&) {}, options); }
    catch (const std::runtime_error&) {}
    Check(!replaced.empty() && fs::exists(replaced) && fs::exists(moved), "Cleanup deleted a different file identity");
    MaintainOwnedStorage();
    Check(fs::exists(replaced), "Recovery deleted an unrelated replacement");
    fs::remove(replaced); fs::remove(moved);
    evidence["identity_replacement_preserved"] = true;
    const auto unknown = root / L"user.logforge-unknown.partial.mov";
    std::ofstream(unknown) << "not owned by LogForge";
    evidence["crash_stages"] = Json::array();
    for (const auto stage : {"partial_tracked", "during_transform", "encoded", "rotation_tracked", "rotation_encoded",
                              "metadata_patched", "before_report", "after_report", "before_publication", "after_publication"}) {
        const auto output = root / (Wide(stage) + L".mov");
        const auto child = RunProcess(ExecutableDirectory() / L"logforge_v130_tests.exe",
            {L"crash-child", ff.wstring(), input.wstring(), output.wstring(), Wide(stage)}, &cancel, 40);
        Check(child.exitCode == 86, (std::string("Crash checkpoint failed: ") + stage + " " + child.error).c_str());
        size_t remaining = 0;
        for (int retry = 0; retry < 40; ++retry) {
            MaintainOwnedStorage(); remaining = 0;
            for (const auto& file : fs::directory_iterator(root))
                if (file.path() != unknown && (file.path().filename().wstring().ends_with(L".partial.mov") ||
                                               file.path().filename().wstring().ends_with(L".rotated.mov"))) ++remaining;
            if (!remaining) break;
            Sleep(50); // Allow dying FFmpeg processes to release file handles.
        }
        Check(!remaining && fs::exists(unknown), "Stale cleanup missed owned files or deleted unowned media");
        const bool published = std::string(stage) == "after_publication";
        Check(fs::exists(output) == published, "Crash recovery changed publication boundary");
        if (published) Check(Probe(tools.ffprobe, output).codec == "prores", "Published movie was damaged by recovery");
        evidence["crash_stages"].push_back({{"stage", stage}, {"owned_partials_removed", true}, {"published_retained", published}});
    }
    std::ofstream(DataDirectory() / L"faults-result.json") << evidence.dump(2);
    fs::remove_all(root); // Exclusively generated fixtures under isolated LOGFORGE_DATA_DIR.
}
void Pipeline(const fs::path& ff) {
    const auto root = DataDirectory() / L"queue-media";
    fs::create_directories(root / L"A"); fs::create_directories(root / L"B");
    fs::create_directories(root / L"outputs");
    const auto first = root / L"A/IMG_0001.MOV", second = root / L"B/IMG_0001.MOV";
    std::atomic_bool cancel = false;
    Logger log;
    ToolTrust::ApproveManual(ToolTrust::Inspect(ff));
    const auto tools = FFmpegManager(log).Check(ff, cancel);
    auto made = RunProcess(ff, {L"-v", L"error", L"-y", L"-f", L"lavfi", L"-i",
        L"color=gray:size=128x64:rate=24", L"-f", L"lavfi", L"-i", L"sine=duration=0.5:sample_rate=48000",
        L"-frames:v", L"12", L"-vf", L"format=yuv422p10le,setparams=color_primaries=bt2020:color_trc=arib-std-b67:colorspace=bt2020nc:range=limited",
        L"-c:v", L"prores_ks", L"-profile:v", L"3", L"-threads:v", L"2", L"-c:a", L"pcm_s24le",
        first.wstring()}, &cancel, 30);
    Check(made.exitCode == 0, "Queue fixture generation failed");
    fs::copy_file(first, second, fs::copy_options::overwrite_existing);
    const auto existing = root / L"outputs/IMG_0001_AppleLog.mov";
    std::ofstream(existing) << "existing user file";
    const auto hash = SHA256(existing);
    auto plan = PlanQueue({first, second}, existing.parent_path());
    TranscodeOptions options; options.backend = ProcessingBackend::CPU;
    auto result = RunQueue(tools, plan, log, cancel, options, "left", [](auto, auto, const auto&, const auto&) {});
    Check(result.at("successful") == 2 && result.at("failed") == 0 && SHA256(existing) == hash,
          "Duplicate input stems failed or overwrote an existing output");
    for (size_t i = 0; i < plan.size(); ++i) {
        Check(fs::is_regular_file(plan[i].output) && result["items"][i]["output"] == PathText(plan[i].output) &&
              result["items"][i]["validation"]["publication"]["final_path"] == PathText(plan[i].output) &&
              result["items"][i]["validation"]["completed"] == true, "Queue final-path report mismatch");
        Check(result["items"][i]["validation"]["metadata"]["audio_copy"]["audio_payload_verified"] == true,
              "Copied audio payload was not verified");
    }
    std::ofstream(DataDirectory() / L"queue-result.json") << result.dump(2);
    fs::remove_all(root); // Test-owned synthetic directory under isolated LOGFORGE_DATA_DIR.
}
}
int wmain(int argc, wchar_t** argv) {
    try {
        const std::wstring group = argc > 1 ? argv[1] : L"unit";
        if (group == L"pipeline") Pipeline(argv[2]);
        else if (group == L"faults") Faults(argv[2]);
        else if (group == L"crash-child" && argc == 6) CrashChild(argv[2], argv[3], argv[4], Utf8(argv[5]));
        else if (group == L"cuda") CudaCache();
        else Unit();
        std::cout << "PASS: v1.3 queue/publication/stream-report regressions\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << "FAIL: " << e.what() << '\n'; return 1; }
}
