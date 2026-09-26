#include "logforge/FFmpeg.h"
#include "logforge/Settings.h"
#include <algorithm>
#include <chrono>
#include <iostream>
#include <thread>

using namespace logforge;
namespace {
void Require(bool ok, const char* message) {
    if (!ok)
        throw std::runtime_error(message);
}
void Write(const fs::path& path, const std::string& data) {
    fs::create_directories(path.parent_path());
    std::ofstream f(path);
    f << data;
    Require(static_cast<bool>(f), "Fixture write failed");
}
struct Sandbox {
    fs::path base = fs::absolute(DataDirectory());
    fs::path root = base / (L"reliability-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
                            std::to_wstring(GetTickCount64()));
    ~Sandbox() {
        std::error_code ec;
        if (root.parent_path() == base)
            fs::remove_all(root, ec);
    }
};
struct Environment {
    std::wstring key, prior;
    bool present;
    Environment(const wchar_t* name, const std::wstring& value) : key(name) {
        const DWORD n = GetEnvironmentVariableW(name, nullptr, 0);
        present = n != 0;
        prior.resize(n);
        if (n)
            prior.resize(GetEnvironmentVariableW(name, prior.data(), n));
        Require(SetEnvironmentVariableW(name, value.c_str()) != FALSE, "Environment fixture failed");
    }
    ~Environment() {
        SetEnvironmentVariableW(key.c_str(), present ? prior.c_str() : nullptr);
    }
};
void Processes() {
    const auto helper = ExecutableDirectory() / L"logforge_process_fixture.exe";
    std::atomic_bool cancel = false;
    Require(RunProcess(helper, {L"exit"}, &cancel, 2).exitCode == 77, "Exit status lost");
    for (const auto* mode : {L"stdout-long", L"stderr-long"}) {
        bool rejected = false;
        try {
            RunProcess(helper, {mode}, &cancel, 3, [](const auto&) {});
        } catch (const AppError& e) {
            rejected = e.message.id == TextId::ProcessOutputLimit;
        }
        Require(rejected, "Unbounded/misreported long pipe line");
    }
    bool propagated = false;
    try {
        RunProcess(helper, {L"line"}, &cancel, 2,
                   [](const auto&) { throw std::runtime_error("callback failure"); });
    } catch (const std::runtime_error& e) {
        propagated = std::string(e.what()) == "callback failure";
    }
    Require(propagated, "Reader callback exception was swallowed");
    for (const bool interrupt : {false, true}) {
        DWORD descendant = 0;
        std::jthread canceller;
        cancel = false;
        if (interrupt)
            canceller = std::jthread([&] {
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
                cancel = true;
            });
        bool stopped = false;
        const auto start = GetTickCount64();
        try {
            RunProcess(helper, {L"heldpipe"}, &cancel, 1,
                       [&](const auto& line) { descendant = static_cast<DWORD>(std::stoul(line)); });
        } catch (const AppError& e) {
            stopped = e.message.id == (interrupt ? TextId::Cancelled : TextId::ProcessTimeout);
        }
        Require(stopped && GetTickCount64() - start < 3000,
                "Parent-exit held pipes lost cancellation/deadline");
        Require(descendant != 0, "Descendant fixture did not start");
        Handle child(OpenProcess(SYNCHRONIZE, FALSE, descendant));
        Require(!child || WaitForSingleObject(child.get(), 1000) == WAIT_OBJECT_0,
                "Orphan child survived job termination");
    }
    std::cout << "PASS: process exit, stdout/stderr bounds, callback errors, held descendants, deadline and "
                 "cancellation\n";
}
void Discovery() {
    Sandbox fixture;
    const auto root = fixture.root / L"unicode 测试";
    const auto good = root / L"pair";
    const auto missing = root / L"missing-probe";
    const auto deepOnly = root / L"one/two/three/four/five/six";
    fs::create_directories(good);
    fs::create_directories(missing);
    fs::create_directories(deepOnly);
    const auto fake = ExecutableDirectory() / L"logforge_fake_ffmpeg.exe";
    for (const auto& dir : {good, missing, deepOnly})
        fs::copy_file(fake, dir / L"ffmpeg.exe");
    fs::copy_file(fake, good / L"ffprobe.exe");
    fs::copy_file(fake, deepOnly / L"ffprobe.exe");
    const auto marker = fixture.root / L"executed.txt";
    SetEnvironmentVariableW(L"LOGFORGE_FAKE_MARKER", marker.c_str());
    struct Restore {
        ~Restore() {
            SetEnvironmentVariableW(L"LOGFORGE_FAKE_MARKER", nullptr);
        }
    } restore;
    const auto helper = ExecutableDirectory() / L"LogForge-cli.exe";
    std::atomic_bool cancel = false;
    DiscoveryOptions options{DiscoveryMode::Quick, {root, root}};
    auto report = DiscoverFFmpegPaths(options, cancel, {}, helper);
    Require(report.end == DiscoveryEnd::Completed && report.elapsedMs < 3500,
            "Quick discovery did not finish");
    Require(report.candidates.size() == 2, "Quick search recursed too far or failed dedup");
    bool pair = false, incomplete = false;
    for (const auto& c : report.candidates) {
        pair |= c.ffmpeg.parent_path() == good && c.paired;
        incomplete |=
            c.ffmpeg.parent_path() == missing && !c.paired && c.state == CandidateState::MissingProbe;
    }
    Require(pair && incomplete && !fs::exists(marker), "Discovery executed or misclassified a fake tool");
    options.mode = DiscoveryMode::Deep;
    report = DiscoverFFmpegPaths(options, cancel, {}, helper);
    Require(report.end == DiscoveryEnd::Completed && report.candidates.size() == 3 && !fs::exists(marker),
            "Explicit deep search did not find deep fixture safely");
    const auto longRoot = fixture.root / std::wstring(160, L'a') / std::wstring(160, L'b');
    const auto nativeLongRoot = fs::path(L"\\\\?\\" + longRoot.wstring());
    fs::create_directories(nativeLongRoot);
    fs::copy_file(fake, nativeLongRoot / L"ffmpeg.exe");
    fs::copy_file(fake, nativeLongRoot / L"ffprobe.exe");
    const auto longReport = DiscoverFFmpegPaths({DiscoveryMode::Quick, {longRoot}}, cancel, {}, helper);
    Require(longReport.end == DiscoveryEnd::Completed && longReport.candidates.size() == 1 &&
                longReport.candidates.front().paired && !fs::exists(marker),
            "Long local path discovery failed");
    // Actual environment providers, with fake executables in isolated directories.
    const auto scoop = fixture.root / L"scoop/apps/ffmpeg/current/bin";
    const auto choco = fixture.root / L"choco/lib/ffmpeg-test/tools/bin";
    for (const auto& dir : {scoop, choco}) {
        fs::create_directories(dir);
        fs::copy_file(fake, dir / L"ffmpeg.exe");
        fs::copy_file(fake, dir / L"ffprobe.exe");
    }
    Environment pathEnv(L"PATH", good.wstring());
    Environment scoopEnv(L"SCOOP", (fixture.root / L"scoop").wstring());
    Environment chocoEnv(L"ChocolateyInstall", (fixture.root / L"choco").wstring());
    report = DiscoverFFmpegPaths({}, cancel, {}, helper);
    for (const auto* source : {"path", "scoop", "chocolatey-package"})
        Require(std::any_of(report.candidates.begin(), report.candidates.end(),
                            [&](const auto& c) { return c.source == source; }),
                "Environment package/PATH provider did not find its real location");
    Require(!fs::exists(marker), "A provider executed a discovered executable");
    // Test App Paths parsing without touching the real application registration.
    const auto keyName = L"Software\\LogForge-Discovery-Test-" + std::to_wstring(GetCurrentProcessId());
    HKEY key{};
    Require(RegCreateKeyExW(HKEY_CURRENT_USER, keyName.c_str(), 0, nullptr, REG_OPTION_VOLATILE, KEY_WRITE,
                            nullptr, &key, nullptr) == ERROR_SUCCESS,
            "Volatile registry fixture failed");
    struct RegistryCleanup {
        HKEY key;
        std::wstring name;
        ~RegistryCleanup() {
            RegCloseKey(key);
            RegDeleteTreeW(HKEY_CURRENT_USER, name.c_str());
        }
    } registry{key, keyName};
    const std::wstring expanded = L"\"%SCOOP%\\apps\\ffmpeg\\current\\bin\\ffmpeg.exe\"";
    Require(RegSetValueExW(key, nullptr, 0, REG_EXPAND_SZ, reinterpret_cast<const BYTE*>(expanded.c_str()),
                           static_cast<DWORD>((expanded.size() + 1) * sizeof(wchar_t))) == ERROR_SUCCESS,
            "Registry value failed");
    const auto registered = RegisteredFFmpegPaths(keyName);
    Require(!registered.empty() &&
                registered.front().lexically_normal() == (scoop / L"ffmpeg.exe").lexically_normal(),
            "App Paths quoted environment expansion failed");
    Require(RegisteredFFmpegPaths(keyName + L"-absent").empty(), "Missing App Paths key did not fail safely");
    {
        Environment data(L"LOGFORGE_DATA_DIR", (fixture.root / L"failed-trust-save").wstring());
        const auto destination = DataDirectory() / L"ffmpeg-trust.json";
        fs::create_directories(destination); // Force atomic replacement to fail.
        Write(destination / L"keep.txt", "preserve original");
        bool failed = false;
        try {
            ToolTrust::ApproveManual(ToolTrust::Inspect(good / L"ffmpeg.exe"));
        } catch (const AppError& e) {
            failed = e.message.id == TextId::SettingsSaveFailed;
        }
        Require(failed && fs::exists(destination / L"keep.txt"), "Trust-save failure damaged existing data");
        for (const auto& entry : fs::directory_iterator(DataDirectory()))
            Require(entry.path().extension() != L".tmp", "Failed trust save leaked its temporary file");
    }
    // Hostile/stalled filesystem or index provider is represented by an owned helper fixture.
    options.mode = DiscoveryMode::Quick;
    report =
        DiscoverFFmpegPaths(options, cancel, {}, ExecutableDirectory() / L"logforge_process_fixture.exe");
    Require(report.end == DiscoveryEnd::TimedOut && report.elapsedMs >= 2900 && report.elapsedMs < 4500 &&
                report.candidates.size() == 1,
            "Quick deadline failed or discarded streamed candidates");
    options.mode = DiscoveryMode::Deep;
    report = DiscoverFFmpegPaths(
        options, cancel,
        [&](const DiscoveryProgress& p) {
            if (p.candidate)
                cancel = true; // Handshake avoids assuming subprocess startup latency.
        },
        ExecutableDirectory() / L"logforge_process_fixture.exe");
    Require(report.end == DiscoveryEnd::Cancelled && report.elapsedMs < 1500 && report.candidates.size() == 1,
            "Deep cancellation lost streamed candidates");
    std::cout << "PASS: quick scope, deep opt-in, Unicode, duplicate/missing pairs, fake non-execution, "
                 "deadline and partial results\n";
}
void CommandLine() {
    std::atomic_bool cancel = false;
    const auto cli = ExecutableDirectory() / L"LogForge-cli.exe";
    for (const auto& args : {std::vector<std::wstring>{L"--invalid"},
                             {L"--convert"},
                             {L"--deep-search", L"--probe", L"missing.mov"},
                             {L"--detect", L"extra"}}) {
        const auto result = RunProcess(cli, args, &cancel, 2);
        Require(result.exitCode != 0 && result.error.find("CLICommand") != std::string::npos,
                "Malformed command launched discovery or returned the wrong error");
    }
    Require(RunProcess(cli, {L"--version"}, &cancel, 2).output.find("1.2.0 (26926A)") != std::string::npos,
            "CLI build identifier wrong");
    std::cout << "PASS: malformed CLI rejected before tool detection and current version\n";
}
} // namespace
int main(int argc, char** argv) {
    try {
        const std::string group = argc > 1 ? argv[1] : "all";
        if (group == "process" || group == "all")
            Processes();
        if (group == "discovery" || group == "all")
            Discovery();
        if (group == "cli" || group == "all")
            CommandLine();
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
