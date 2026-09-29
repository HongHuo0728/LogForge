#include "logforge/FFmpeg.h"
#include "logforge/Settings.h"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <set>
#include <winver.h>

using namespace logforge;
namespace {
void Require(bool ok, const char* message) {
    if (!ok)
        throw std::runtime_error(message);
}
struct Sandbox {
    fs::path base = fs::absolute(DataDirectory()).lexically_normal();
    fs::path root = base / (L"application-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
                            std::to_wstring(GetTickCount64()));
    std::wstring original;
    Sandbox() {
        wchar_t buffer[32768]{};
        GetEnvironmentVariableW(L"LOGFORGE_DATA_DIR", buffer, 32768);
        original = buffer;
        fs::create_directories(root);
        SetEnvironmentVariableW(L"LOGFORGE_DATA_DIR", root.c_str());
    }
    ~Sandbox() {
        SetEnvironmentVariableW(L"LOGFORGE_DATA_DIR", original.empty() ? nullptr : original.c_str());
        // Delete only this test's explicitly bounded, uniquely named directory.
        if (root.parent_path() == base && root.filename().wstring().starts_with(L"application-") &&
            !(GetFileAttributesW(root.c_str()) & FILE_ATTRIBUTE_REPARSE_POINT)) {
            std::error_code ec;
            fs::remove_all(root, ec);
        }
    }
};
void Write(const fs::path& path, const std::string& text) {
    fs::create_directories(path.parent_path());
    std::ofstream file(path, std::ios::binary);
    file << text;
    Require(static_cast<bool>(file), "Cannot create test fixture");
}
Json Read(const fs::path& path) {
    std::ifstream file(path);
    return Json::parse(file);
}
void Preferences() {
    Sandbox fixture;
    Require(ValidateTranslations(), "Missing, duplicate or mismatched translations");
    std::set<std::string> keys;
    for (int i = 0; i < static_cast<int>(TextId::Count); ++i) {
        const auto id = static_cast<TextId>(i);
        Require(keys.insert(MessageKey(id)).second, "Duplicate message key");
        Require(!Translate(id, Language::English).empty(), "Empty English message");
        Require(!Translate(id, Language::SimplifiedChinese).empty(), "Empty Chinese message");
    }
    const std::string unusual = "D:\\{1}\\clip {0}.mov";
    Require(Translate({TextId::Unexpected, {unusual}}).find(unusual) != std::string::npos,
            "Placeholders inside paths were interpreted recursively");
    Require(Translate(TextId::Settings) == "Settings" &&
                Translate(TextId::Settings, Language::SimplifiedChinese) != "Settings",
            "Language selection failed");
    auto preferences = SettingsStore::Load();
    Require(preferences.language == Language::English && preferences.theme == Theme::Dark &&
                !preferences.tone.enabled && !preferences.recoveredDefaults,
            "Incorrect first-run defaults");
    const auto config = fixture.root / L"settings.json";
    const auto manual = fixture.root / L"manual" / L"ffmpeg.exe";
    const auto detected = fixture.root / L"detected" / L"ffmpeg.exe";
    Write(config, Json{{"ffmpeg", PathText(manual)}, {"future_setting", 42}}.dump());
    preferences = SettingsStore::Load();
    Require(preferences.manualFFmpeg == manual && preferences.language == Language::English,
            "Legacy FFmpeg setting did not migrate");
    preferences.language = Language::SimplifiedChinese;
    preferences.theme = Theme::Light;
    preferences.tone = {true, 2.5, 1.5, .65};
    SettingsStore::SavePreferences(preferences);
    SettingsStore::SaveFFmpeg(detected, true);
    Logger logger;
    FFmpegManager(logger).SaveManual(manual);
    const auto saved = SettingsStore::Load();
    Require(saved.language == preferences.language && saved.theme == preferences.theme &&
                saved.tone.enabled && saved.tone.shadowStops == 2.5 && saved.tone.highlightStops == 1.5 &&
                saved.tone.saturation == .65,
            "Preferences did not survive reload / FFmpeg cache update");
    Require(saved.manualFFmpeg == manual && saved.detectedFFmpeg == detected &&
                Read(config).at("future_setting") == 42,
            "Settings writes erased unrelated fields");
    auto off = saved;
    off.tone.enabled = false;
    SettingsStore::SavePreferences(off);
    Require(!SettingsStore::Load().tone.enabled && SettingsStore::Load().tone.shadowStops == 2.5,
            "Disabling creative rendering lost saved parameters");
    Write(config, R"({"language":"invalid","theme":7,"creative":{"shadow_stops":100}})");
    const auto invalid = SettingsStore::Load();
    Require(invalid.recoveredDefaults && invalid.language == Language::English &&
                invalid.theme == Theme::Dark && !invalid.tone.enabled,
            "Invalid settings did not recover");
    Write(config, "{truncated");
    Require(SettingsStore::Load().recoveredDefaults, "Corrupt settings did not recover");
    bool refused = false;
    preferences.tone.saturation = 2;
    try {
        SettingsStore::SavePreferences(preferences);
    } catch (const AppError&) {
        refused = true;
    }
    Require(refused, "Out-of-range settings were saved");
    std::cout << "PASS: complete bilingual catalog, default/legacy/corrupt settings, persistence and merge\n";
}
void Discovery(const fs::path& realFFmpeg) {
    Sandbox fixture;
    const auto first = fixture.root / L"first";
    const auto second = fixture.root / L"second";
    const auto empty = fixture.root / L"empty";
    fs::create_directories(empty);
    Write(first / L"ffmpeg.exe", "not an executable");
    Write(second / L"nested" / L"测试 folder" / L"FFMPEG.EXE", "second candidate");
    // Recycle / restore directories must not cause deleted executables to be selected.
    Write(first / L"$Recycle.Bin" / L"ffmpeg.exe", "ignored");
    std::atomic_bool cancel = false;
    size_t calls = 0, updates = 0;
    auto report = SearchFFmpegDirectories(
        {fixture.root / L"missing", first, first, empty, second}, cancel,
        [&](const fs::path&) {
            ++calls;
            return false;
        },
        [&](const auto&) { ++updates; });
    Require(!report.found && calls == 2 && report.candidates == 2 && report.skipped >= 2 && updates > 0,
            "Discovery did not cover roots / Unicode / case / dedup / exclusions");
    calls = 0;
    report = SearchFFmpegDirectories({first, second}, cancel, [&](const fs::path& candidate) {
        ++calls;
        return candidate.filename() == L"FFMPEG.EXE";
    });
    Require(report.found && calls == 2, "Discovery stopped after an invalid candidate");
    Require(!SearchFFmpegDirectories({empty}, cancel, [](const auto&) { return true; }).found,
            "An empty drive reported a candidate");
    report = SearchFFmpegDirectories({fs::path(first.wstring() + L"\\")}, cancel,
                                     [](const auto&) { return true; });
    Require(report.found && report.candidates == 1, "A drive-style trailing separator broke discovery");
    const auto drives = LocalDriveRoots();
    Require(!drives.empty(), "No local volume roots enumerated");
    for (const auto& drive : drives) {
        const auto type = GetDriveTypeW(drive.c_str());
        Require(type == DRIVE_FIXED || type == DRIVE_REMOVABLE, "Remote volume included in disk search");
    }
    bool cancelled = false;
    try {
        SearchFFmpegDirectories(
            {first}, cancel, [](const auto&) { return false; }, [&](const auto&) { cancel = true; });
    } catch (const AppError& e) {
        cancelled = e.message.id == TextId::Cancelled;
    }
    Require(cancelled, "Disk scan did not honour cancellation");
    cancel = false;
    const auto link = second / L"loop";
    const bool linked = CreateSymbolicLinkW(link.c_str(), second.c_str(),
                                            SYMBOLIC_LINK_FLAG_DIRECTORY |
                                                SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE) != FALSE;
    if (linked) {
        report = SearchFFmpegDirectories({second}, cancel, [](const auto&) { return false; });
        Require(report.candidates == 1 && report.skipped > 0, "Reparse directory was followed");
        fs::remove(link);
    }
    Logger logger;
    FFmpegManager manager(logger);
    bool pairRejected = false;
    try {
        manager.Check(first / L"ffmpeg.exe", cancel);
    } catch (const AppError& e) {
        pairRejected = e.message.id == TextId::FFmpegPair;
    }
    Require(pairRejected, "Candidate without ffprobe was accepted");
    if (!realFFmpeg.empty()) {
        const auto valid = fixture.root / L"valid";
        fs::create_directories(valid);
        for (const auto* name : {L"ffmpeg.exe", L"ffprobe.exe"}) {
            std::error_code ec;
            fs::create_hard_link(realFFmpeg.parent_path() / name, valid / name, ec);
            if (ec)
                fs::copy_file(realFFmpeg.parent_path() / name, valid / name);
        }
        size_t rejections = 0;
        ToolTrust::ApproveManual(ToolTrust::Inspect(valid / L"ffmpeg.exe"));
        report = SearchFFmpegDirectories({first, second, valid}, cancel, [&](const auto& candidate) {
            try {
                manager.Check(candidate, cancel);
                return true;
            } catch (const AppError&) {
                ++rejections;
                return false;
            }
        });
        Require(report.found && rejections == 2, "Real capability discovery failed after invalid candidates");
        SettingsStore::SaveFFmpeg(first / L"ffmpeg.exe", false);
        SettingsStore::SaveFFmpeg(valid / L"ffmpeg.exe", true);
        bool diskScan = false;
        const auto cached = manager.Detect(
            cancel, [&](const auto& p) { diskScan |= p.phase == DiscoveryPhase::ScanningDrive; });
        Require(cached && cached->ffmpeg == valid / L"ffmpeg.exe" && !diskScan,
                "Verified cache was not used after stale manual path");
    }
    std::cout << "PASS: drive traversal, invalid candidates, cancellation, Unicode, exclusions, "
              << (linked ? "reparse loop" : "reparse creation unavailable (skipped)")
              << (realFFmpeg.empty() ? "\n" : ", real ProRes capability check and cached rediscovery\n");
}
void Resources() {
    for (const auto* name : {L"LogForge.exe", L"LogForge-cli.exe"}) {
        const auto path = ExecutableDirectory() / name;
        DWORD unused = 0;
        const DWORD size = GetFileVersionInfoSizeW(path.c_str(), &unused);
        Require(size > 0, "Missing VERSIONINFO resource");
        std::vector<unsigned char> bytes(size);
        Require(GetFileVersionInfoW(path.c_str(), 0, size, bytes.data()) != FALSE,
                "Unreadable version resource");
        VS_FIXEDFILEINFO* fixed = nullptr;
        UINT length = 0;
        Require(VerQueryValueW(bytes.data(), L"\\", reinterpret_cast<void**>(&fixed), &length) != FALSE,
                "Missing numeric version");
        Require(fixed->dwFileVersionMS == MAKELONG(3, 1) && fixed->dwFileVersionLS == MAKELONG(0, 0) &&
                    fixed->dwProductVersionMS == MAKELONG(3, 1) &&
                    fixed->dwProductVersionLS == MAKELONG(0, 0),
                "Numeric Windows version is not 1.3.0.0");
        for (const auto* key : {L"FileVersion", L"ProductVersion"}) {
            wchar_t* value = nullptr;
            const auto query = std::wstring(L"\\StringFileInfo\\040904b0\\") + key;
            Require(VerQueryValueW(bytes.data(), query.c_str(), reinterpret_cast<void**>(&value), &length) &&
                        std::wstring(value) == L"1.3.0.0",
                    "Windows version text is not 1.3.0.0");
        }
        HMODULE module =
            LoadLibraryExW(path.c_str(), nullptr, LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE);
        Require(module != nullptr, "Cannot read executable resources");
        HRSRC icon = FindResourceW(module, MAKEINTRESOURCEW(101), RT_GROUP_ICON);
        Require(icon != nullptr, "Missing application icon");
        const auto data = static_cast<const WORD*>(LockResource(LoadResource(module, icon)));
        Require(data && data[2] == 9, "Application icon does not contain nine DPI sizes");
        FreeLibrary(module);
    }
    Require(std::string(DisplayVersion) == "1.3.0 (26929A)", "Incorrect displayed build number");
    std::cout << "PASS: GUI / CLI Windows versions, build number and embedded multiresolution icons\n";
}
} // namespace
int wmain(int argc, wchar_t** argv) {
    try {
        if (argc > 1 && std::wstring(argv[1]) == L"--internal-discover")
            return RunDiscoveryHelper(std::vector<std::wstring>(argv + 1, argv + argc));
        const std::wstring group = argc > 1 ? argv[1] : L"all";
        if (group == L"all" || group == L"preferences")
            Preferences();
        if (group == L"all" || group == L"discovery")
            Discovery(argc > 2 ? argv[2] : fs::path{});
        if (group == L"all" || group == L"resources")
            Resources();
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
