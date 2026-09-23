#include "logforge/FFmpeg.h"
#include "logforge/FloatTransformer.h"
#include "logforge/Transcode.h"
#include <chrono>
#include <cmath>
#include <iostream>
#include <thread>

using namespace logforge;
void Require(bool ok, const char* what) {
    if (!ok)
        throw std::runtime_error(what);
}
void Cadence() {
    MediaInfo m;
    m.timeBase = {1, 3000000};
    m.nominalFps = {30, 1};
    m.averageFps = {2999, 100};
    std::vector<VideoPacketTiming> packets;
    for (auto [ticks, rate] : std::vector<std::pair<int64_t, Rational>>{{100000, {30, 1}},
                                                                        {100100, {30000, 1001}},
                                                                        {100033, {3000000, 100033}},
                                                                        {100067, {3000000, 100067}}}) {
        packets.clear();
        for (int64_t i = 0; i < 12000; ++i)
            packets.push_back({i * ticks, ticks});
        auto r = AnalyzeCadence(packets, m);
        Require(r.verified && std::abs(r.rate.Value() - rate.Value()) < 1e-12,
                "Fixed nonstandard cadence rejected");
        Require(r.maxPhaseError == 0 && r.maxIntervalError == 0, "Fixed cadence has fabricated phase error");
    }
    // EXACT decimal capture rates, with metadata declaring the usual nominal 30.
    for (int64_t numerator : {2999, 2998, 299701}) {
        const int64_t ticks = numerator == 299701 ? 10000 : 100;
        m.timeBase = {1, numerator};
        m.averageFps = {25, 1};
        packets.clear();
        for (int64_t i = 0; i < 10000; ++i)
            packets.push_back({i * ticks, ticks});
        auto r = AnalyzeCadence(packets, m);
        Require(r.verified && r.rate.numerator * ticks == numerator * r.rate.denominator,
                "29.99/29.98/29.9701 regression");
    }
    m.timeBase = {1, 480};
    m.nominalFps = {24, 1};
    m.averageFps = {24001, 1000};
    packets.clear();
    for (int64_t i = 0; i < 256; ++i)
        packets.push_back({i * 20 - (i >= 30), 20});
    packets[29].duration = 19;
    Require(AnalyzeCadence(packets, m).verified, "Single clock quantization correction rejected");
    for (size_t i = 0; i < packets.size(); ++i) {
        const auto n = static_cast<int64_t>(i);
        packets[i] = {n * 20 - n / 64, 20 - ((n + 1) % 64 == 0 ? 1 : 0)};
    }
    auto drift = AnalyzeCadence(packets, m);
    Require(!drift.verified && drift.errorPacket == 128 && drift.maxPhaseError >= 3,
            "Sustained phase drift accepted");
    packets.clear();
    for (int64_t i = 0; i < 100; ++i)
        packets.push_back({i * 20, 20});
    packets[50].pts += 5;
    Require(!AnalyzeCadence(packets, m).verified && AnalyzeCadence(packets, m).errorPacket == 50,
            "Gap accepted");
    packets[50].pts = packets[49].pts;
    Require(!AnalyzeCadence(packets, m).verified, "Duplicate PTS accepted");
    packets[50].pts = packets[49].pts - 1;
    Require(!AnalyzeCadence(packets, m).verified, "Backwards PTS accepted");
    int64_t pts = 0;
    packets.clear();
    for (int i = 0; i < 100; ++i) {
        const auto duration = i % 2 ? 24 : 16;
        packets.push_back({pts, duration});
        pts += duration;
    }
    Require(!AnalyzeCadence(packets, m).verified, "True VFR accepted");
    std::cout << "PASS: 29.99, 29.98, 29.9701, metadata mismatch, drift, gap, duplicate/reverse PTS, VFR\n";
}
void Security(const fs::path& root) {
    fs::create_directories(root);
    const auto fake = root / L"ffmpeg.exe", probe = root / L"ffprobe.exe", marker = root / L"executed.txt";
    fs::copy_file(ExecutableDirectory() / L"logforge_fake_ffmpeg.exe", fake,
                  fs::copy_options::overwrite_existing);
    fs::copy_file(fake, probe, fs::copy_options::overwrite_existing);
    fs::remove(marker);
    SetEnvironmentVariableW(L"LOGFORGE_FAKE_MARKER", marker.c_str());
    Logger log;
    FFmpegManager manager(log);
    std::atomic_bool cancel = false;
    std::optional<FFmpegInstallation> found;
    // Production discovery callback, not a mock that merely promises not to run.
    SearchFFmpegDirectories({root}, cancel,
                            [&](const auto& p) { return manager.DiscoverCandidate(p, cancel, found); });
    Require(!found && !manager.UnapprovedCandidates().empty() && !fs::exists(marker),
            "Discovered malicious fake was executed");
    bool rejected = false;
    try {
        manager.Check(fake, cancel);
    } catch (const AppError& e) {
        rejected = e.message.id == TextId::FFmpegUntrusted;
    }
    Require(rejected && !fs::exists(marker), "Direct unapproved capability check executed a file");
    auto identity = ToolTrust::Inspect(fake);
    ToolTrust::ApproveManual(identity);
    {
        auto lease = ToolTrust::Acquire(fake);
        Handle write(CreateFileW(fake.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                 OPEN_EXISTING, 0, nullptr));
        Require(!write, "Approved executable was writable during an execution lease");
    }
    {
        std::ofstream f(fake, std::ios::binary | std::ios::app);
        f.put('x');
    }
    rejected = false;
    try {
        manager.Check(fake, cancel);
    } catch (const AppError& e) {
        rejected = e.message.id == TextId::FFmpegHashChanged;
    }
    Require(rejected && !fs::exists(marker), "Changed ffmpeg was executed");
    rejected = false;
    try {
        ToolTrust::ApproveManual(identity);
    } catch (const AppError& e) {
        rejected = e.message.id == TextId::FFmpegHashChanged;
    }
    Require(rejected, "Approval snapshot race accepted");
    ToolTrust::ApproveManual(ToolTrust::Inspect(fake));
    {
        std::ofstream f(probe, std::ios::binary | std::ios::app);
        f.put('y');
    }
    rejected = false;
    try {
        manager.Check(fake, cancel);
    } catch (const AppError& e) {
        rejected = e.message.id == TextId::FFmpegHashChanged;
    }
    Require(rejected && !fs::exists(marker), "Changed ffprobe was executed");
    // Positive control proves the canary really is executable and marks any invocation.
    ToolTrust::ApproveManual(ToolTrust::Inspect(fake));
    try {
        manager.Check(fake, cancel);
    } catch (const AppError&) {
    }
    Require(fs::exists(marker), "Fake execution canary positive control failed");
    fs::remove(marker);
    fs::remove(fake);
    fs::remove(probe);
    fs::remove(root);
    std::cout << "PASS: untrusted discovery non-execution, direct execution gate, both hashes, approval race "
                 "and deny-write lease\n";
}
std::string BE(uint32_t n) {
    std::string r;
    for (int shift = 24; shift >= 0; shift -= 8)
        r += static_cast<char>(n >> shift);
    return r;
}
std::string Atom(const std::string& type, const std::string& data) {
    return BE(static_cast<uint32_t>(8 + data.size())) + type + data;
}
std::string Data(int type, const std::string& data) {
    return Atom("data", BE(type) + BE(0) + data);
}
void Mov(const fs::path& root) {
    fs::create_directories(root);
    const auto a = root / L"a.mov", b = root / L"b.mov";
    auto make = [](bool reversed, bool full) {
        const std::string name = "com.apple.proapps.customgamma", model = "com.apple.quicktime.model";
        auto key = [](const std::string& s) { return BE(static_cast<uint32_t>(s.size() + 8)) + "mdta" + s; };
        const auto keys =
            Atom("keys", BE(0) + BE(2) + (reversed ? key(model) + key(name) : key(name) + key(model)));
        const auto gamma = Atom(BE(reversed ? 2 : 1),
                                Data(1, "com.apple.rec2020.apple-log") + Data(21, std::string(1, '\xff')));
        const auto device = Atom(BE(reversed ? 1 : 2), Data(1, "Synthetic fixture"));
        const auto hdlr = Atom("hdlr", BE(0) + BE(0) + "mdta" + std::string(12, 0));
        return Atom("moov", Atom("meta", (full ? BE(0) : std::string{}) + hdlr +
                                             Atom("ilst", gamma + device) + keys));
    };
    {
        std::ofstream f(a, std::ios::binary);
        f << make(false, true);
    }
    {
        std::ofstream f(b, std::ios::binary);
        f << Atom("free", std::string(1000, 0)) << make(true, false) << Atom("mdat", std::string(64, 0));
    }
    const auto first = ReferenceMovAnalyzer::Analyze(a), second = ReferenceMovAnalyzer::Analyze(b);
    Require(first["metadata"][0]["key"] == "com.apple.proapps.customgamma",
            "mdta index/name resolution failed");
    Require(first["metadata"][0]["values"][0]["value"] == "com.apple.rec2020.apple-log",
            "data UTF-8 value failed");
    Require(first["metadata"][0]["values"][1]["value"] == -1, "data signed integer failed");
    Require(ReferenceMovAnalyzer::SemanticDiff(first, second).empty(),
            "Offsets, sizes or key reorder polluted semantic diff");
    auto changed = second;
    changed["semantic"]["metadata"].begin().value()[0]["values"][0]["value"] = "changed";
    Require(!ReferenceMovAnalyzer::SemanticDiff(first, changed).empty(), "Semantic value change invisible");
    {
        std::ofstream f(b, std::ios::binary | std::ios::trunc);
        f << BE(100) << "meta";
    }
    bool bad = false;
    try {
        ReferenceMovAnalyzer::Analyze(b);
    } catch (const AppError&) {
        bad = true;
    }
    Require(bad, "Truncated atom accepted");
    const auto sample = [](const std::string& suffix) {
        auto data = Atom("apcn", std::string(78, 0) +
                                    Atom("logs", "com.apple.rec2020.apple-log") + suffix);
        data = Atom("stsd", BE(0) + BE(1) + data);
        for (const auto* parent : {"stbl", "minf", "mdia", "trak", "moov"})
            data = Atom(parent, data);
        return data;
    };
    for (const auto& suffix : {std::string{}, BE(0)}) {
        { std::ofstream f(b, std::ios::binary | std::ios::trunc); f << sample(suffix); }
        const auto parsed = ReferenceMovAnalyzer::Analyze(b);
        bool found = false;
        for (const auto& atom : parsed["atoms"])
            found |= atom.value("log_transfer_function", "") == "com.apple.rec2020.apple-log";
        Require(found, "Native logs sample-entry identifier not decoded");
    }
    for (const auto& suffix : {std::string(3, 0), std::string(5, 0), BE(1)}) {
        { std::ofstream f(b, std::ios::binary | std::ios::trunc); f << sample(suffix); }
        bad = false;
        try { ReferenceMovAnalyzer::Analyze(b); } catch (const AppError&) { bad = true; }
        Require(bad, "Malformed sample-description terminator accepted");
    }
    { std::ofstream f(b, std::ios::binary | std::ios::trunc); f << Atom("moov", BE(0)); }
    bad = false;
    try { ReferenceMovAnalyzer::Analyze(b); } catch (const AppError&) { bad = true; }
    Require(bad, "Video terminator allowed outside a video sample entry");
    fs::remove(a);
    fs::remove(b);
    fs::remove(root);
    std::cout
        << "PASS: QuickTime/ISO meta, reordered keys, ilst, mdta, typed data, semantic diff, truncation\n";
}
void Parallel() {
    constexpr size_t plane = 3840 * 2160;
    std::vector<float> reference(plane * 3), parallel(reference.size());
    for (size_t i = 0; i < reference.size(); ++i)
        reference[i] = static_cast<float>(-0.03 + 1.12 * (i % 65537) / 65536.);
    const auto original = reference;
    for (bool creative : {false, true}) {
        auto tone = ToneAdjustments{};
        tone.enabled = creative;
        reference = original;
        parallel = original;
        SignalStatistics scalarStats, parallelStats;
        auto start = std::chrono::steady_clock::now();
        TransformHLGToAppleLog(reference, .5, tone, &scalarStats);
        auto scalarTime = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        FloatTransformer workers(.5, tone, 4);
        start = std::chrono::steady_clock::now();
        workers.Apply(parallel, parallelStats);
        auto parallelTime = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        Require(reference == parallel, "Parallel output differs from independent scalar path");
        Require(scalarStats.samples == parallelStats.samples &&
                    scalarStats.appleFloorClipped == parallelStats.appleFloorClipped &&
                    scalarStats.aboveNominalWhite == parallelStats.aboveNominalWhite &&
                    scalarStats.outputMinimum == parallelStats.outputMinimum &&
                    scalarStats.outputMaximum == parallelStats.outputMaximum,
                "Parallel statistics disagree");
        std::cout << "4K " << (creative ? "creative" : "standard") << " scalar=" << scalarTime
                  << "s parallel=" << parallelTime << "s, exact float match\n";
        // Reuse workers for a partial tile after a full 4K frame.
        float invalid = std::numeric_limits<float>::quiet_NaN();
        bool refused = false;
        try {
            workers.Apply(std::span(&invalid, 1), parallelStats);
        } catch (...) {
            refused = true;
        }
        Require(refused, "Worker non-finite error lost");
    }
}
void Publication(const fs::path& ff, const fs::path& root) {
    fs::create_directories(root);
    const auto input = root / L"input.mov", output = root / L"created-during-conversion.mov";
    fs::remove(output);
    Logger log;
    std::atomic_bool cancel = false;
    ToolTrust::ApproveManual(ToolTrust::Inspect(ff));
    auto tools = FFmpegManager(log).Check(ff, cancel);
    // A file alias to an approved ffmpeg must not cause execution of an
    // unapproved ffprobe beside that alias. Both executables must resolve to
    // the same pair that was actually approved and locked.
    const auto alias = root / L"alias";
    fs::create_directories(alias);
    const auto aliasExe = alias / L"ffmpeg.exe", aliasProbe = alias / L"ffprobe.exe";
    if (CreateSymbolicLinkW(aliasExe.c_str(), tools.ffmpeg.c_str(),
                            SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE)) {
        fs::copy_file(ExecutableDirectory() / L"logforge_fake_ffmpeg.exe", aliasProbe);
        const auto marker = alias / L"executed.txt";
        SetEnvironmentVariableW(L"LOGFORGE_FAKE_MARKER", marker.c_str());
        auto checked = FFmpegManager(log).Check(aliasExe, cancel, false);
        Require(!fs::exists(marker), "Unapproved ffprobe beside an approved alias was executed");
        Require(checked.ffmpeg == tools.ffmpeg && checked.ffprobe == tools.ffprobe,
                "Capability check did not retain canonical approved paths");
        fs::remove(aliasExe);
        fs::remove(aliasProbe);
        std::cout << "PASS: canonical pair ignores malicious alias sibling ffprobe\n";
    } else {
        std::cout << "SKIP: alias-sibling test requires Windows symbolic-link permission\n";
    }
    fs::remove(alias);
    const auto made = RunProcess(ff, {L"-v", L"error", L"-nostdin", L"-y", L"-f", L"lavfi", L"-i",
                                      L"color=size=128x64:rate=24", L"-frames:v", L"24", L"-vf",
                                      L"format=yuv422p10le,setparams=color_primaries=bt2020:color_trc=arib-"
                                      L"std-b67:colorspace=bt2020nc:range=limited",
                                      L"-c:v", L"prores_ks", L"-profile:v", L"3", input.wstring()});
    Require(made.exitCode == 0, "Publication fixture encoding failed");
    auto media = Probe(tools.ffprobe, input);
    media.inputChromaOverride = "left";
    bool created = false, refused = false;
    try {
        TranscodeJob::Run(tools, media, output, log, cancel, [&](const JobProgress& p) {
            if (p.frame > 0 && !created) {
                std::ofstream f(output);
                f << "unrelated destination";
                created = true;
            }
        });
    } catch (const AppError& e) {
        refused = e.message.id == TextId::OutputExists;
    }
    std::ifstream f(output);
    std::string content((std::istreambuf_iterator<char>(f)), {});
    f.close();
    Require(created && refused && content == "unrelated destination", "Late destination was overwritten");
    for (const auto& entry : fs::directory_iterator(root))
        Require(entry.path().filename().wstring().find(L"partial") == std::wstring::npos,
                "Publication failure leaked a partial");
    fs::remove(input);
    fs::remove(output);
    fs::remove(root);
    std::cout << "PASS: concurrent destination creation refused after validation; partial cleaned\n";
}
int main(int argc, char** argv) {
    try {
        const std::string group = argc > 1 ? argv[1] : "all";
        const auto dir = DataDirectory() / (L"hardening-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
                                            std::to_wstring(GetTickCount64()));
        if (group == "cadence" || group == "all")
            Cadence();
        if (group == "security" || group == "all")
            Security(dir / L"security");
        if (group == "mov" || group == "all")
            Mov(dir / L"mov");
        if (group == "parallel" || group == "all")
            Parallel();
        if (group == "publication" && argc == 3)
            Publication(fs::path(Wide(argv[2])), dir / L"publication");
        std::error_code ec;
        fs::remove(dir, ec);
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
