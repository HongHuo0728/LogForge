#include "logforge/FFmpeg.h"
#include "logforge/Settings.h"
#include <algorithm>
#include <cwctype>
#include <set>
#include <shlobj.h>
#include <sstream>
#include <wrl/client.h>

namespace logforge {
namespace {
using Microsoft::WRL::ComPtr;
constexpr size_t MaxCandidates = 256;
std::wstring Lower(std::wstring s) {
    for (auto& c : s)
        c = static_cast<wchar_t>(std::towlower(c));
    return s;
}
std::wstring Environment(const wchar_t* key) {
    const DWORD n = GetEnvironmentVariableW(key, nullptr, 0);
    if (!n || n > 32768)
        return {};
    std::wstring result(n, 0);
    const auto got = GetEnvironmentVariableW(key, result.data(), n);
    if (!got || got >= n)
        return {};
    result.resize(got);
    return result;
}
fs::path Known(REFKNOWNFOLDERID id) {
    PWSTR text = nullptr;
    if (FAILED(SHGetKnownFolderPath(id, KF_FLAG_DONT_VERIFY, nullptr, &text)))
        return {};
    fs::path p(text);
    CoTaskMemFree(text);
    return p;
}
bool Local(const fs::path& p) {
    if (!p.is_absolute())
        return false;
    auto s = p.wstring();
    if (s.starts_with(L"\\\\?\\") && s.size() > 6 && s[5] == L':') s.erase(0, 4);
    if (s.starts_with(L"\\\\"))
        return false;
    const auto type = GetDriveTypeW(fs::path(s).root_path().c_str());
    return type == DRIVE_FIXED || type == DRIVE_REMOVABLE;
}
fs::path NativePath(const fs::path& p) {
    const auto s = p.wstring();
    if (s.size() >= 248 && p.is_absolute() && !s.starts_with(L"\\\\")) return fs::path(L"\\\\?\\" + s);
    return p;
}
void Emit(const Json& value) {
    const auto line = value.dump() + '\n';
    DWORD written = 0;
    if (!WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), line.data(), static_cast<DWORD>(line.size()), &written,
                   nullptr) ||
        written != line.size())
        throw std::runtime_error("Discovery pipe closed");
}
struct Collector {
    DiscoveryMode mode;
    ULONGLONG started = GetTickCount64(), lastUpdate = 0;
    uint64_t directories = 0, skipped = 0;
    std::set<std::wstring> paths;
    bool limitReported = false;
    bool Expired() const {
        return mode == DiscoveryMode::Quick && GetTickCount64() - started >= 2800;
    }
    void Progress(const fs::path& p, bool force = false) {
        if (!force && GetTickCount64() - lastUpdate < 200)
            return;
        lastUpdate = GetTickCount64();
        Emit({{"type", "progress"},
              {"path", PathText(p)},
              {"directories", directories},
              {"skipped", skipped},
              {"candidates", paths.size()}});
    }
    void Add(const fs::path& supplied, const std::string& source) {
        if (Expired() || supplied.empty() || !Local(supplied))
            return;
        if (paths.size() >= MaxCandidates) {
            if (!limitReported)
                Emit({{"type", "diagnostic"},
                      {"message", "Candidate limit (256) reached; result list is incomplete. Use manual "
                                  "selection for another installation."}});
            limitReported = true;
            return;
        }
        std::error_code ec;
        const auto attrs = GetFileAttributesW(NativePath(supplied).c_str());
        if (attrs == INVALID_FILE_ATTRIBUTES || (attrs & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_OFFLINE)))
            return;
        auto path = NativePath(fs::canonical(NativePath(supplied), ec));
        if (ec || !Local(path)) {
            ++skipped;
            return;
        }
        const auto key = Lower(path.wstring());
        if (!paths.insert(key).second)
            return;
        DiscoveryCandidate candidate;
        candidate.ffmpeg = path;
        candidate.ffprobe = path.parent_path() / L"ffprobe.exe";
        candidate.source = source;
        const auto probeAttrs = GetFileAttributesW(candidate.ffprobe.c_str());
        if (probeAttrs != INVALID_FILE_ATTRIBUTES &&
            !(probeAttrs & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_OFFLINE))) {
            auto probe = NativePath(fs::canonical(candidate.ffprobe, ec));
            candidate.paired = !ec && Local(probe) &&
                               Lower(probe.parent_path().wstring()) == Lower(path.parent_path().wstring());
            if (candidate.paired)
                candidate.ffprobe = probe;
        }
        if (!candidate.paired) {
            candidate.state = CandidateState::MissingProbe;
            candidate.issue = TextId::FFmpegPair;
        }
        Emit({{"type", "candidate"}, {"candidate", candidate.ToJson()}});
    }
    void Tree(const fs::path& root, const std::string& source, int depth = 3) {
        if (root.empty() || !Local(root) || Expired())
            return;
        std::error_code ec;
        // An explicit package root such as Scoop's current may be a junction;
        // resolve that root once, but never recurse through child junctions.
        auto canonical = NativePath(fs::canonical(NativePath(root), ec));
        if (ec || !Local(canonical))
            return;
        const auto attrs = GetFileAttributesW(canonical.c_str());
        if (attrs == INVALID_FILE_ATTRIBUTES || (attrs & FILE_ATTRIBUTE_OFFLINE))
            return;
        size_t entries = 0;
        for (fs::recursive_directory_iterator i(canonical, fs::directory_options::skip_permission_denied, ec),
             end;
             i != end && !ec && !Expired() && entries++ < 4096; i.increment(ec)) {
            const auto a = GetFileAttributesW(i->path().c_str());
            if (a == INVALID_FILE_ATTRIBUTES ||
                (a & (FILE_ATTRIBUTE_OFFLINE | FILE_ATTRIBUTE_REPARSE_POINT))) {
                i.disable_recursion_pending();
                ++skipped;
                continue;
            }
            if (a & FILE_ATTRIBUTE_DIRECTORY) {
                ++directories;
                if (i.depth() >= depth)
                    i.disable_recursion_pending();
            } else if (_wcsicmp(i->path().filename().c_str(), L"ffmpeg.exe") == 0)
                Add(i->path(), source);
            Progress(canonical);
        }
    }
    void Packages(const fs::path& root, const std::string& source) {
        if (root.empty() || !Local(root) || Expired())
            return;
        std::error_code ec;
        size_t entries = 0;
        for (fs::directory_iterator i(root, ec), end; i != end && !ec && !Expired() && entries++ < 4096;
             i.increment(ec))
            if (Lower(i->path().filename().wstring()).find(L"ffmpeg") != std::wstring::npos)
                Tree(i->path(), source);
    }
};
} // namespace
std::vector<fs::path> RegisteredFFmpegPaths(const std::wstring& subkey) {
    std::vector<fs::path> result;
    for (auto hive : {HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE})
        for (auto view : {KEY_WOW64_64KEY, KEY_WOW64_32KEY}) {
            HKEY key = nullptr;
            if (RegOpenKeyExW(hive, subkey.c_str(), 0, KEY_READ | view, &key) != ERROR_SUCCESS)
                continue;
            struct Close {
                HKEY k;
                ~Close() {
                    RegCloseKey(k);
                }
            } close{key};
            wchar_t value[32768]{};
            DWORD type = 0, bytes = sizeof(value) - sizeof(wchar_t);
            if (RegQueryValueExW(key, nullptr, nullptr, &type, reinterpret_cast<BYTE*>(value), &bytes) !=
                    ERROR_SUCCESS ||
                (type != REG_SZ && type != REG_EXPAND_SZ))
                continue;
            std::wstring path(value);
            if (type == REG_EXPAND_SZ) {
                wchar_t expanded[32768]{};
                const auto n = ExpandEnvironmentStringsW(value, expanded, 32768);
                if (!n || n > 32768)
                    continue;
                path = expanded;
            }
            if (path.size() >= 2 && path.front() == L'"' && path.back() == L'"')
                path = path.substr(1, path.size() - 2);
            result.emplace_back(path);
        }
    return result;
}
namespace {
// Late-bound Windows ADO: no PowerShell, package-manager subprocess or bundled DLL.
struct Value {
    VARIANT v{};
    Value() {
        VariantInit(&v);
    }
    Value(const Value&) = delete;
    Value(Value&& other) noexcept : v(other.v) {
        VariantInit(&other.v);
    }
    ~Value() {
        VariantClear(&v);
    }
};
Value String(const wchar_t* s) {
    Value x;
    x.v.vt = VT_BSTR;
    x.v.bstrVal = SysAllocString(s);
    if (!x.v.bstrVal)
        throw std::bad_alloc();
    return x;
}
Value Invoke(IDispatch* object, const wchar_t* name, WORD flags, std::initializer_list<VARIANT> args = {}) {
    DISPID id{}, property = DISPID_PROPERTYPUT;
    auto text = const_cast<wchar_t*>(name);
    if (!object || FAILED(object->GetIDsOfNames(IID_NULL, &text, 1, LOCALE_INVARIANT, &id)))
        throw std::runtime_error("Windows Search dispatch unavailable");
    std::vector<VARIANT> reversed(args);
    std::reverse(reversed.begin(), reversed.end());
    DISPPARAMS params{reversed.data(), flags == DISPATCH_PROPERTYPUT ? &property : nullptr,
                      static_cast<UINT>(reversed.size()), flags == DISPATCH_PROPERTYPUT ? 1u : 0u};
    Value result;
    if (FAILED(object->Invoke(id, IID_NULL, LOCALE_INVARIANT, flags, &params, &result.v, nullptr, nullptr)))
        throw std::runtime_error("Windows Search query unavailable");
    return result;
}
void Index(Collector& c) {
    if (c.Expired())
        return;
    const HRESULT init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(init)) {
        ++c.skipped;
        return;
    }
    struct CloseCom {
        ~CloseCom() {
            CoUninitialize();
        }
    } closeCom;
    try {
        CLSID id{};
        ComPtr<IDispatch> connection;
        if (FAILED(CLSIDFromProgID(L"ADODB.Connection", &id)) ||
            FAILED(CoCreateInstance(id, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&connection))))
            throw std::runtime_error("Windows Search provider unavailable");
        VARIANT timeout{};
        timeout.vt = VT_I4;
        timeout.lVal = 1;
        Invoke(connection.Get(), L"ConnectionTimeout", DISPATCH_PROPERTYPUT, {timeout});
        Invoke(connection.Get(), L"CommandTimeout", DISPATCH_PROPERTYPUT, {timeout});
        auto address = String(L"Provider=Search.CollatorDSO;Extended Properties=\"Application=Windows\";");
        Invoke(connection.Get(), L"Open", DISPATCH_METHOD, {address.v});
        auto query = String(
            L"SELECT TOP 256 System.ItemPathDisplay FROM SystemIndex WHERE System.FileName = 'ffmpeg.exe'");
        auto rows = Invoke(connection.Get(), L"Execute", DISPATCH_METHOD, {query.v});
        if (rows.v.vt != VT_DISPATCH)
            throw std::runtime_error("Windows Search result unavailable");
        for (size_t n = 0; n < MaxCandidates && !c.Expired(); ++n) {
            auto eof = Invoke(rows.v.pdispVal, L"EOF", DISPATCH_PROPERTYGET);
            if (eof.v.vt != VT_BOOL || eof.v.boolVal)
                break;
            auto fields = Invoke(rows.v.pdispVal, L"Fields", DISPATCH_PROPERTYGET);
            if (fields.v.vt != VT_DISPATCH)
                break;
            auto key = String(L"System.ItemPathDisplay");
            auto field = Invoke(fields.v.pdispVal, L"Item", DISPATCH_PROPERTYGET, {key.v});
            if (field.v.vt != VT_DISPATCH)
                break;
            auto value = Invoke(field.v.pdispVal, L"Value", DISPATCH_PROPERTYGET);
            if (value.v.vt == VT_BSTR)
                c.Add(fs::path(value.v.bstrVal), "windows-index");
            Invoke(rows.v.pdispVal, L"MoveNext", DISPATCH_METHOD);
        }
        Invoke(rows.v.pdispVal, L"Close", DISPATCH_METHOD);
        Invoke(connection.Get(), L"Close", DISPATCH_METHOD);
    } catch (const std::exception&) {
        ++c.skipped;
        Emit({{"type", "diagnostic"},
              {"message", "Windows Search unavailable; other discovery sources remain usable."}});
    }
}
void Quick(Collector& c, bool preferredOnly, bool skipPreferred) {
    if (!skipPreferred) {
        c.Add(FFmpegManager::ManagedExecutable(), "managed");
        const auto settings = SettingsStore::Load();
        c.Add(settings.manualFFmpeg, "saved-manual");
        c.Add(settings.detectedFFmpeg, "last-success");
    }
    if (preferredOnly)
        return;
    c.Add(ExecutableDirectory() / L"tools/ffmpeg/bin/ffmpeg.exe", "beside-application");
    std::wstringstream path(Environment(L"PATH"));
    std::wstring entry;
    while (std::getline(path, entry, L';') && !c.Expired()) {
        if (entry.size() >= 2 && entry.front() == L'"' && entry.back() == L'"')
            entry = entry.substr(1, entry.size() - 2);
        if (!entry.empty())
            c.Add(fs::path(entry) / L"ffmpeg.exe", "path");
    }
    for (const auto& registered : RegisteredFFmpegPaths())
        c.Add(registered, "app-paths");
    const auto local = Known(FOLDERID_LocalAppData), profile = Known(FOLDERID_Profile);
    const auto program = Known(FOLDERID_ProgramFiles), programX86 = Known(FOLDERID_ProgramFilesX86);
    const auto programData = Known(FOLDERID_ProgramData);
    for (const auto& base : {local, program, programX86}) {
        if (base.empty())
            continue;
        const auto winget = base == local ? base / L"Microsoft/WinGet" : base / L"WinGet";
        c.Add(winget / L"Links/ffmpeg.exe", "winget-link");
        c.Packages(winget / L"Packages", "winget-package");
    }
    auto scoop = fs::path(Environment(L"SCOOP"));
    if (scoop.empty() && !profile.empty())
        scoop = profile / L"scoop";
    auto scoopGlobal = fs::path(Environment(L"SCOOP_GLOBAL"));
    if (scoopGlobal.empty() && !programData.empty())
        scoopGlobal = programData / L"scoop";
    for (const auto& base : {scoop, scoopGlobal})
        if (!base.empty()) {
            c.Add(base / L"apps/ffmpeg/current/bin/ffmpeg.exe", "scoop");
            c.Tree(base / L"apps/ffmpeg/current", "scoop");
        }
    auto chocolatey = fs::path(Environment(L"ChocolateyInstall"));
    if (chocolatey.empty() && !programData.empty())
        chocolatey = programData / L"chocolatey";
    if (!chocolatey.empty())
        c.Packages(chocolatey / L"lib", "chocolatey-package");
    for (const auto& base : {program, programX86, local})
        if (!base.empty()) {
            c.Tree(base / L"ffmpeg", "common-folder");
            if (base == local)
                c.Tree(base / L"Programs/ffmpeg", "common-folder");
        }
    for (const auto& drive : LocalDriveRoots())
        if (GetDriveTypeW(drive.c_str()) == DRIVE_FIXED) {
            c.Tree(drive / L"ffmpeg", "common-folder");
            c.Tree(drive / L"tools/ffmpeg", "common-folder");
        }
    Index(c);
}
} // namespace

nlohmann::json DiscoveryCandidate::ToJson() const {
    static constexpr const char* states[]{"unapproved", "missing-ffprobe", "hash-changed", "incompatible",
                                          "verified"};
    return {
        {"path", PathText(ffmpeg)}, {"ffprobe_path", PathText(ffprobe)},        {"source", source},
        {"paired", paired},         {"state", states[static_cast<int>(state)]}, {"issue", Translate(issue)}};
}
nlohmann::json DiscoveryReport::ToJson() const {
    static constexpr const char* ends[]{"completed", "timed-out", "cancelled", "failed"};
    Json list = Json::array();
    for (const auto& c : candidates)
        list.push_back(c.ToJson());
    return {{"mode", mode == DiscoveryMode::Quick ? "quick" : "deep"},
            {"end", ends[static_cast<int>(end)]},
            {"elapsed_ms", elapsedMs},
            {"verification_ms", verificationMs},
            {"directories", directories},
            {"skipped", skipped},
            {"diagnostic", diagnostic},
            {"candidates", list}};
}
int RunDiscoveryHelper(const std::vector<std::wstring>& args) {
    try {
        if (args.size() < 2 || args[0] != L"--internal-discover" ||
            (args[1] != L"quick" && args[1] != L"deep"))
            return 2;
        Collector c{args[1] == L"deep" ? DiscoveryMode::Deep : DiscoveryMode::Quick};
        std::vector<fs::path> roots;
        bool preferredOnly = false, skipPreferred = false;
        for (size_t i = 2; i < args.size(); ++i) {
            if (args[i] == L"--preferred-only") {
                preferredOnly = true;
                continue;
            }
            if (args[i] == L"--skip-preferred") {
                skipPreferred = true;
                continue;
            }
            if (args[i] != L"--root" || ++i == args.size())
                return 2;
            roots.emplace_back(args[i]);
        }
        if (c.mode == DiscoveryMode::Quick) {
            if (roots.empty())
                Quick(c, preferredOnly, skipPreferred);
            else
                for (const auto& root : roots)
                    c.Tree(root, "explicit-root");
        } else {
            SetThreadPriority(GetCurrentThread(), THREAD_MODE_BACKGROUND_BEGIN);
            std::atomic_bool cancelled = false;
            SearchFFmpegDirectories(
                roots.empty() ? LocalDriveRoots() : roots, cancelled,
                [&](const fs::path& p) {
                    c.Add(p, "deep-search");
                    return false;
                },
                [&](const DiscoveryProgress& p) {
                    c.directories = p.directories;
                    c.skipped = p.skipped;
                    c.Progress(p.drive);
                });
        }
        c.Progress({}, true);
        Emit({{"type", "end"}, {"timed_out", c.Expired()}});
        return 0;
    } catch (const std::exception&) {
        return 1;
    }
}
DiscoveryReport DiscoverFFmpegPaths(const DiscoveryOptions& options, const std::atomic_bool& cancel,
                                    const DiscoveryCallback& progress, const fs::path& helper) {
    DiscoveryReport report;
    report.mode = options.mode;
    std::wstring executable(32768, 0);
    executable.resize(GetModuleFileNameW(nullptr, executable.data(), static_cast<DWORD>(executable.size())));
    std::vector<std::wstring> args{L"--internal-discover",
                                   options.mode == DiscoveryMode::Quick ? L"quick" : L"deep"};
    if (options.preferredOnly)
        args.push_back(L"--preferred-only");
    if (options.skipPreferred)
        args.push_back(L"--skip-preferred");
    for (const auto& root : options.roots) {
        args.push_back(L"--root");
        args.push_back(fs::absolute(root).wstring());
    }
    const auto start = GetTickCount64();
    DiscoveryProgress state;
    state.phase =
        options.mode == DiscoveryMode::Quick ? DiscoveryPhase::CheckingPaths : DiscoveryPhase::ScanningDrive;
    bool ended = false;
    try {
        if (cancel.load())
            throw AppError(TextId::Cancelled);
        const auto result = RunProcess(
            helper.empty() ? fs::path(executable) : helper, args, &cancel,
            options.mode == DiscoveryMode::Quick ? 3 : 0,
            [&](const std::string& line) {
                const auto value = Json::parse(line);
                const auto type = value.value("type", "");
                state.candidate.reset();
                if (type == "candidate" && report.candidates.size() < MaxCandidates) {
                    const auto& j = value.at("candidate");
                    DiscoveryCandidate c;
                    c.ffmpeg = Wide(j.at("path").get<std::string>());
                    c.ffprobe = Wide(j.at("ffprobe_path").get<std::string>());
                    c.source = j.at("source").get<std::string>();
                    c.paired = j.at("paired").get<bool>();
                    if (!c.paired) {
                        c.state = CandidateState::MissingProbe;
                        c.issue = TextId::FFmpegPair;
                    }
                    report.candidates.push_back(c);
                    state.candidate = c;
                } else if (type == "progress") {
                    report.directories = value.at("directories").get<uint64_t>();
                    report.skipped = value.at("skipped").get<uint64_t>();
                    state.drive = Wide(value.at("path").get<std::string>());
                } else if (type == "diagnostic")
                    report.diagnostic = value.value("message", "");
                else if (type == "end") {
                    ended = true;
                    if (value.value("timed_out", false))
                        report.end = DiscoveryEnd::TimedOut;
                }
                state.directories = report.directories;
                state.skipped = report.skipped;
                state.candidates = report.candidates.size();
                state.elapsedMs = GetTickCount64() - start;
                if (progress)
                    progress(state);
            },
            options.mode == DiscoveryMode::Quick ? std::max<uint64_t>(1, options.budgetMs) : 0);
        if (result.exitCode || !ended) {
            report.end = DiscoveryEnd::Failed;
            report.diagnostic = "Discovery helper did not complete.";
        }
    } catch (const AppError& e) {
        report.end = e.message.id == TextId::Cancelled        ? DiscoveryEnd::Cancelled
                     : e.message.id == TextId::ProcessTimeout ? DiscoveryEnd::TimedOut
                                                              : DiscoveryEnd::Failed;
        report.diagnostic = e.what();
    } catch (const std::exception& e) {
        report.end = DiscoveryEnd::Failed;
        report.diagnostic = e.what();
    }
    report.elapsedMs = GetTickCount64() - start;
    return report;
}
} // namespace logforge
