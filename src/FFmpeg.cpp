#include "logforge/FFmpeg.h"
#include "logforge/Settings.h"
#include <array>
#include <set>
#include <sstream>
#include <thread>
#include <winhttp.h>

namespace logforge {
Json VerifyFFmpegNumerics(const fs::path&, const std::atomic_bool&, Logger&);
Json FFmpegInstallation::ToJson() const {
    auto result = lease ? lease->identity.ToJson() : Json::object();
    result["version"] = version;
    result["verification"] = numericallyVerified ? "Verified FFmpeg" : "Compatible but unverified FFmpeg";
    result["reference_signal"] = numeric;
    return result;
}
fs::path FFmpegManager::ManagedExecutable() {
    return DataDirectory() / L"tools" / L"ffmpeg" / L"ffmpeg-8.1.2-essentials_build" / L"bin" / L"ffmpeg.exe";
}
std::vector<std::string> FFmpegManager::MissingCapabilities(const std::string& d, const std::string& e,
                                                            const std::string& f, const std::string& p) {
    std::vector<std::string> missing;
    if (d.find(" prores ") == std::string::npos)
        missing.push_back("ProRes decoder");
    if (e.find("Encoder prores_ks") == std::string::npos || e.find("yuv422p10le") == std::string::npos)
        missing.push_back("prores_ks 10-bit encoder");
    for (const auto& filter : {" zscale ", " format ", " setparams "})
        if (f.find(filter) == std::string::npos)
            missing.push_back(filter);
    for (const auto& fmt : {"gbrpf32le", "yuv422p10le"})
        if (p.find(fmt) == std::string::npos)
            missing.push_back(fmt);
    return missing;
}
FFmpegInstallation FFmpegManager::Check(const fs::path& exe, const std::atomic_bool& cancel, bool smoke) {
    fs::path ff = fs::absolute(exe), probe = ff.parent_path() / L"ffprobe.exe";
    if (!fs::is_regular_file(ff) || !fs::is_regular_file(probe))
        throw AppError(TextId::FFmpegPair);
    auto lease = ToolTrust::Acquire(ff);
    // Execute the resolved pair whose images are locked and hashed, not the
    // discovery alias (whose sibling ffprobe may be an unrelated executable).
    ff = lease->identity.ffmpeg;
    probe = lease->identity.ffprobe;
    auto run = [&](const std::vector<std::wstring>& args) {
        auto r = RunProcess(ff, args, &cancel);
        if (r.exitCode)
            throw AppError(Message(TextId::FFmpegCapability, {r.error}));
        return r.output + r.error;
    };
    auto version = run({L"-version"});
    auto pv = RunProcess(probe, {L"-version"}, &cancel);
    if (version.find("ffmpeg version ") != 0 || pv.exitCode || pv.output.find("ffprobe version ") != 0)
        throw AppError(TextId::FFmpegVersion);
    const auto decoder = run({L"-hide_banner", L"-decoders"});
    const auto encoder = run({L"-hide_banner", L"-h", L"encoder=prores_ks"});
    const auto filters = run({L"-hide_banner", L"-filters"});
    const auto formats = run({L"-hide_banner", L"-pix_fmts"});
    auto missing = MissingCapabilities(decoder, encoder, filters, formats);
    if (!missing.empty()) {
        std::string message;
        for (const auto& m : missing)
            message += m + "; ";
        throw AppError(Message(TextId::FFmpegMissingFeatures, {message}));
    }
    logger_.Write("FFmpeg path: " + PathText(ff) + "\n" + version);
    logger_.Write("ffprobe: " + pv.output);
    if (smoke) {
        auto dir = DataDirectory() / L"cache";
        fs::create_directories(dir);
        auto path = dir / (L"capability-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
                           std::to_wstring(GetTickCount64()) + L".mov");
        struct Cleanup {
            fs::path p;
            ~Cleanup() {
                std::error_code ec;
                fs::remove(p, ec);
            }
        } cleanup{path};
        run({L"-hide_banner",
             L"-v",
             L"error",
             L"-nostdin",
             L"-f",
             L"lavfi",
             L"-i",
             L"color=black:s=64x64:r=24",
             L"-frames:v",
             L"1",
             L"-vf",
             L"format=yuv422p10le",
             L"-c:v",
             L"prores_ks",
             L"-profile:v",
             L"3",
             L"-color_primaries",
             L"bt2020",
             L"-color_trc",
             L"arib-std-b67",
             L"-colorspace",
             L"bt2020nc",
             L"-y",
             path.wstring()});
        auto m = Probe(probe, path, &cancel);
        if (m.codec != "prores" || m.profile != "HQ" || m.bitDepth != 10 || m.pixelFormat != "yuv422p10le")
            throw AppError(TextId::FFmpegSmokeFailed);
        run({L"-hide_banner", L"-v", L"error", L"-nostdin", L"-i", path.wstring(), L"-vf",
             L"zscale=matrixin=2020_ncl:matrix=gbr:rangein=limited:range=full:transferin=arib-std-b67:"
             L"transfer=arib-std-b67:primariesin=2020:primaries=2020,format=gbrpf32le",
             L"-frames:v", L"1", L"-f", L"null", L"-"});
        logger_.Write(
            "Capability smoke test passed: ProRes HQ encode, ffprobe, ProRes decode and float RGB zscale.");
    }
    FFmpegInstallation installation{ff, probe, version.substr(0, version.find_first_of("\r\n")),
                                    std::move(lease), false};
    if (smoke) {
        installation.numeric = VerifyFFmpegNumerics(ff, cancel, logger_);
        installation.numericallyVerified = true;
        logger_.Write("Verified FFmpeg: " + installation.ToJson().dump());
    }
    return installation;
}
void FFmpegManager::SaveManual(const fs::path& executable) {
    SettingsStore::SaveFFmpeg(executable, false);
}
bool FFmpegManager::DiscoverCandidate(const fs::path& p, const std::atomic_bool& cancel,
                                      std::optional<FFmpegInstallation>& found) {
    if (!ToolTrust::HasApproval(p)) {
        unapproved_.push_back(p);
        logger_.Write("Discovered, NOT EXECUTED (no approval): " + PathText(p));
        return false;
    }
    try {
        found = Check(p, cancel);
        SettingsStore::SaveFFmpeg(found->ffmpeg, true);
        return true;
    } catch (const std::exception& e) {
        if (cancel.load())
            throw AppError(TextId::Cancelled);
        unapproved_.push_back(p);
        logger_.Write("Approved candidate unavailable; requires review: " + PathText(p) + ": " + e.what());
        return false;
    }
}
std::optional<FFmpegInstallation> FFmpegManager::Detect(const std::atomic_bool& cancel,
                                                        const DiscoveryCallback& progress,
                                                        const DiscoveryOptions& options) {
    unapproved_.clear();
    discovery_ = {};
    discovery_.mode = options.mode;
    std::optional<FFmpegInstallation> found;
    auto pass = [&](const DiscoveryOptions& request) {
        auto report = DiscoverFFmpegPaths(request, cancel, [&](const DiscoveryProgress& p) {
            if (progress) {
                auto combined = p;
                combined.elapsedMs += discovery_.elapsedMs;
                combined.directories += discovery_.directories;
                combined.skipped += discovery_.skipped;
                combined.candidates += discovery_.candidates.size();
                progress(combined);
            }
        });
        discovery_.elapsedMs += report.elapsedMs;
        discovery_.directories += report.directories;
        discovery_.skipped += report.skipped;
        discovery_.end = report.end;
        if (!report.diagnostic.empty())
            discovery_.diagnostic = report.diagnostic;
        for (auto candidate : report.candidates) {
            if (std::any_of(discovery_.candidates.begin(), discovery_.candidates.end(),
                            [&](const auto& prior) {
                                return _wcsicmp(prior.ffmpeg.c_str(), candidate.ffmpeg.c_str()) == 0;
                            }))
                continue;
            if (!found && candidate.paired && discovery_.end != DiscoveryEnd::Cancelled) {
                if (!ToolTrust::HasApproval(candidate.ffmpeg)) {
                    unapproved_.push_back(candidate.ffmpeg);
                    logger_.Write("Discovered, NOT EXECUTED (no approval): " + PathText(candidate.ffmpeg));
                } else {
                    const auto started = GetTickCount64();
                    try {
                        if (progress) {
                            DiscoveryProgress state;
                            state.phase = DiscoveryPhase::Verifying;
                            state.drive = candidate.ffmpeg;
                            state.elapsedMs = discovery_.elapsedMs;
                            progress(state);
                        }
                        found = Check(candidate.ffmpeg, cancel);
                        SettingsStore::SaveFFmpeg(found->ffmpeg, true);
                        candidate.state = CandidateState::Verified;
                        candidate.issue = TextId::ToolsReady;
                    } catch (const AppError& e) {
                        if (cancel.load())
                            discovery_.end = DiscoveryEnd::Cancelled;
                        candidate.state = e.message.id == TextId::FFmpegHashChanged
                                              ? CandidateState::Changed
                                              : CandidateState::Incompatible;
                        candidate.issue = e.message;
                        unapproved_.push_back(candidate.ffmpeg);
                        logger_.Write("Approved candidate failed: " + PathText(candidate.ffmpeg) + ": " +
                                      e.what());
                    }
                    discovery_.verificationMs += GetTickCount64() - started;
                }
            }
            discovery_.candidates.push_back(std::move(candidate));
        }
    };
    if (options.mode == DiscoveryMode::Quick && options.roots.empty()) {
        auto request = options;
        request.preferredOnly = true;
        pass(request);
        // A verified managed/saved installation avoids registry, package and
        // index work entirely. Failed/unapproved entries still allow fallbacks.
        if (!found && discovery_.end == DiscoveryEnd::Completed && discovery_.elapsedMs < options.budgetMs) {
            request.preferredOnly = false;
            request.skipPreferred = true;
            request.budgetMs = options.budgetMs - discovery_.elapsedMs;
            pass(request);
        } else if (!found && discovery_.end == DiscoveryEnd::Completed) {
            discovery_.end = DiscoveryEnd::TimedOut;
        }
    } else
        pass(options);
    logger_.Write("FFmpeg discovery: " + discovery_.ToJson().dump());
    return found;
}

DownloadSpec GyanReleaseProvider::Release() const {
    return {L"Gyan.dev essentials",
            L"8.1.2",
            L"https://www.gyan.dev/ffmpeg/builds/packages/ffmpeg-8.1.2-essentials_build.zip",
            L"ffmpeg-8.1.2-essentials_build",
            "db580001caa24ac104c8cb856cd113a87b0a443f7bdf47d8c12b1d740584a2ec",
            "GPL-3.0-or-later",
            "https://github.com/FFmpeg/FFmpeg/tree/n8.1.2"};
}
namespace {
class InternetHandle {
  public:
    HINTERNET h{};
    explicit InternetHandle(HINTERNET v) : h(v) {}
    ~InternetHandle() {
        if (h)
            WinHttpCloseHandle(h);
    }
    InternetHandle(const InternetHandle&) = delete;
    operator HINTERNET() const {
        return h;
    }
};
void download(const std::wstring& url, const fs::path& file, const std::atomic_bool& cancel,
              const DownloadProgress& progress) {
    URL_COMPONENTS parts{};
    parts.dwStructSize = sizeof(parts);
    parts.dwHostNameLength = static_cast<DWORD>(-1);
    parts.dwUrlPathLength = static_cast<DWORD>(-1);
    parts.dwExtraInfoLength = static_cast<DWORD>(-1);
    if (!WinHttpCrackUrl(url.c_str(), 0, 0, &parts) || parts.nScheme != INTERNET_SCHEME_HTTPS)
        throw AppError(TextId::HttpsRequired);
    InternetHandle session(WinHttpOpen(Wide(std::string("LogForge/") + Version).c_str(),
                                       WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
                                       WINHTTP_NO_PROXY_BYPASS, 0));
    if (!session.h)
        throw AppError(TextId::HttpsInit);
    WinHttpSetTimeouts(session, 15000, 15000, 15000, 15000);
    InternetHandle connection(WinHttpConnect(
        session, std::wstring(parts.lpszHostName, parts.dwHostNameLength).c_str(), parts.nPort, 0));
    std::wstring requestPath(parts.lpszUrlPath, parts.dwUrlPathLength);
    if (parts.dwExtraInfoLength)
        requestPath.append(parts.lpszExtraInfo, parts.dwExtraInfoLength);
    InternetHandle request(WinHttpOpenRequest(connection, L"GET", requestPath.c_str(), nullptr,
                                              WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                              WINHTTP_FLAG_SECURE));
    if (!request.h)
        throw AppError(TextId::HttpsRequest);
    DWORD redirects = WINHTTP_OPTION_REDIRECT_POLICY_DISALLOW_HTTPS_TO_HTTP;
    WinHttpSetOption(request, WINHTTP_OPTION_REDIRECT_POLICY, &redirects, sizeof(redirects));
    if (!WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, nullptr, 0, 0, 0) ||
        !WinHttpReceiveResponse(request, nullptr))
        throw AppError(Message(TextId::DownloadConnect, {Utf8(WinError())}));
    DWORD status = 0, len = sizeof(status);
    WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                        WINHTTP_HEADER_NAME_BY_INDEX, &status, &len, WINHTTP_NO_HEADER_INDEX);
    if (status != 200)
        throw AppError(Message(TextId::DownloadHttp, {std::to_string(status)}));
    wchar_t length[64]{};
    len = sizeof(length);
    uint64_t total = 0;
    if (WinHttpQueryHeaders(request, WINHTTP_QUERY_CONTENT_LENGTH, WINHTTP_HEADER_NAME_BY_INDEX, length, &len,
                            WINHTTP_NO_HEADER_INDEX))
        total = _wcstoui64(length, nullptr, 10);
    constexpr uint64_t limit = 512ull * 1024 * 1024;
    if (total > limit)
        throw AppError(TextId::ArchiveLimit);
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    if (!out)
        throw AppError(TextId::DownloadCreate);
    std::array<char, 65536> b{};
    uint64_t received = 0;
    for (;;) {
        if (cancel.load())
            throw AppError(TextId::Cancelled);
        DWORD got{};
        if (!WinHttpReadData(request, b.data(), static_cast<DWORD>(b.size()), &got))
            throw AppError(Message(TextId::DownloadInterrupted, {Utf8(WinError())}));
        if (!got)
            break;
        received += got;
        if (received > limit)
            throw AppError(TextId::ArchiveLimit);
        out.write(b.data(), got);
        if (!out)
            throw AppError(TextId::DownloadWrite);
        progress(received, total, Message(TextId::Downloading));
    }
    out.close();
    if (total && total != received)
        throw AppError(TextId::DownloadIncomplete);
}
} // namespace
FFmpegInstallation FFmpegDownloader::Install(const FFmpegBuildProvider& provider, Logger& log,
                                             const std::atomic_bool& cancel,
                                             const DownloadProgress& progress) {
    const auto spec = provider.Release();
    auto root = DataDirectory() / L"tools" / L"ffmpeg";
    fs::create_directories(root);
    if (fs::is_regular_file(root / spec.archiveRoot / L"bin/ffmpeg.exe")) {
        try {
            auto current = FFmpegManager(log).Check(root / spec.archiveRoot / L"bin/ffmpeg.exe", cancel);
            progress(1, 1, Message(TextId::Installed));
            return current;
        } catch (const std::exception& e) {
            if (cancel.load())
                throw;
            log.Write(std::string("Managed installation needs repair: ") + e.what());
        }
    }
    if (fs::space(root).available < 1024ull * 1024 * 1024)
        throw AppError(TextId::DownloadSpace);
    auto staging = root / (L"install-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
                           std::to_wstring(GetTickCount64()));
    fs::create_directories(staging);
    auto zip = staging / L"download.zip";
    log.Write("FFmpeg provider: " + Utf8(spec.name) + " " + Utf8(spec.version) + "; " + Utf8(spec.url) +
              "; SHA256 " + spec.sha256 + "; " + spec.license);
    bool complete = false;
    for (int attempt = 1; attempt <= 3 && !complete; ++attempt) {
        try {
            download(spec.url, zip, cancel, progress);
            complete = true;
        } catch (const std::exception& e) {
            log.Write("Download attempt " + std::to_string(attempt) + ": " + e.what());
            if (cancel.load() || attempt == 3)
                throw;
            progress(0, 0, Message(TextId::DownloadRetry));
        }
    }
    progress(0, 0, Message(TextId::CheckingHash));
    if (SHA256(zip) != spec.sha256)
        throw AppError(TextId::DownloadHash);
    if (cancel.load())
        throw AppError(TextId::Cancelled);
    progress(0, 0, Message(TextId::Extracting));
    auto tar = SystemExecutable(L"tar.exe");
    auto listing = RunProcess(tar, {L"-tf", zip.wstring()}, &cancel, 60);
    if (listing.exitCode)
        throw AppError(TextId::DownloadArchive);
    std::istringstream lines(listing.output);
    std::string entry;
    while (std::getline(lines, entry)) {
        if (entry.empty())
            continue;
        auto p = fs::path(Wide(entry));
        if (p.is_absolute() || entry.find("..") != std::string::npos ||
            entry.find(':') != std::string::npos || entry.front() == '/' || entry.front() == '\\' ||
            !entry.starts_with(Utf8(spec.archiveRoot) + "/"))
            throw AppError(TextId::ArchivePath);
    }
    auto extracted = RunProcess(tar, {L"-xf", zip.wstring(), L"-C", staging.wstring()}, &cancel, 120);
    if (extracted.exitCode)
        throw AppError(Message(TextId::DownloadExtractFailed, {extracted.error}));
    FFmpegManager manager(log);
    const auto stagedExe = staging / spec.archiveRoot / L"bin/ffmpeg.exe";
    ToolTrust::RecordDownload(stagedExe, spec.sha256);
    {
        auto checked = manager.Check(stagedExe, cancel);
    }
    auto target = root / spec.archiveRoot;
    if (fs::exists(target)) {
        try {
            auto existing = manager.Check(target / L"bin/ffmpeg.exe", cancel);
            fs::remove(zip);
            progress(1, 1, Message(TextId::Installed));
            return existing;
        } catch (const std::exception&) {
            if (cancel.load())
                throw;
            auto backup = root / (spec.archiveRoot + L".replaced-" + std::to_wstring(GetTickCount64()));
            fs::rename(target, backup);
            log.Write("Preserved unusable managed installation: " + PathText(backup));
        }
    }
    fs::rename(staging / spec.archiveRoot, target);
    ToolTrust::RecordDownload(target / L"bin/ffmpeg.exe", spec.sha256);
    fs::remove(zip);
    fs::remove(staging);
    progress(1, 1, Message(TextId::Installed));
    return manager.Check(target / L"bin/ffmpeg.exe", cancel);
}
} // namespace logforge
