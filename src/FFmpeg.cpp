#include "logforge/FFmpeg.h"
#include <array>
#include <set>
#include <sstream>
#include <thread>
#include <winhttp.h>

namespace logforge {
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
        throw std::runtime_error("需要同一目录中的 ffmpeg.exe 和 ffprobe.exe。");
    auto run = [&](const std::vector<std::wstring>& args) {
        auto r = RunProcess(ff, args, &cancel);
        if (r.exitCode)
            throw std::runtime_error("FFmpeg capability check failed: " + r.error);
        return r.output + r.error;
    };
    auto version = run({L"-version"});
    auto pv = RunProcess(probe, {L"-version"}, &cancel);
    if (version.find("ffmpeg version ") != 0 || pv.exitCode || pv.output.find("ffprobe version ") != 0)
        throw std::runtime_error("FFmpeg / ffprobe 版本检测失败。");
    const auto decoder = run({L"-hide_banner", L"-decoders"});
    const auto encoder = run({L"-hide_banner", L"-h", L"encoder=prores_ks"});
    const auto filters = run({L"-hide_banner", L"-filters"});
    const auto formats = run({L"-hide_banner", L"-pix_fmts"});
    auto missing = MissingCapabilities(decoder, encoder, filters, formats);
    if (!missing.empty()) {
        std::string message = "FFmpeg 缺少所需功能：";
        for (const auto& m : missing)
            message += m + "; ";
        throw std::runtime_error(message);
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
            throw std::runtime_error("ProRes HQ 10-bit 编码实测失败。");
        run({L"-hide_banner", L"-v", L"error", L"-nostdin", L"-i", path.wstring(), L"-vf",
             L"zscale=matrixin=2020_ncl:matrix=gbr:rangein=limited:range=full:transferin=arib-std-b67:"
             L"transfer=arib-std-b67:primariesin=2020:primaries=2020,format=gbrpf32le",
             L"-frames:v", L"1", L"-f", L"null", L"-"});
        logger_.Write(
            "Capability smoke test passed: ProRes HQ encode, ffprobe, ProRes decode and float RGB zscale.");
    }
    return {ff, probe, version.substr(0, version.find('\n'))};
}
void FFmpegManager::SaveManual(const fs::path& executable) {
    fs::create_directories(DataDirectory());
    std::ofstream f(DataDirectory() / L"settings.json");
    if (!f)
        throw std::runtime_error("Cannot save FFmpeg setting.");
    f << Json{{"ffmpeg", PathText(fs::absolute(executable))}}.dump(2);
}
std::optional<FFmpegInstallation> FFmpegManager::Detect(const std::atomic_bool& cancel) {
    std::vector<fs::path> candidates{ManagedExecutable(),
                                     ExecutableDirectory() / L"tools" / L"ffmpeg" / L"bin" / L"ffmpeg.exe"};
    try {
        std::ifstream f(DataDirectory() / L"settings.json");
        if (f) {
            Json j;
            f >> j;
            candidates.push_back(Wide(j.value("ffmpeg", std::string())));
        }
    } catch (const std::exception& e) {
        logger_.Write(std::string("Settings ignored: ") + e.what());
    }
    wchar_t path[32768]{};
    GetEnvironmentVariableW(L"PATH", path, 32768);
    std::wstringstream split(path);
    std::wstring entry;
    while (std::getline(split, entry, L';')) {
        if (entry.size() > 1 && entry.front() == L'\"' && entry.back() == L'\"')
            entry = entry.substr(1, entry.size() - 2);
        if (!entry.empty())
            candidates.emplace_back(fs::path(entry) / L"ffmpeg.exe");
    }
    candidates.emplace_back(L"C:\\ffmpeg\\bin\\ffmpeg.exe");
    candidates.emplace_back(L"C:\\Program Files\\ffmpeg\\bin\\ffmpeg.exe");
    candidates.emplace_back(L"C:\\ProgramData\\chocolatey\\bin\\ffmpeg.exe");
    wchar_t user[32768]{};
    GetEnvironmentVariableW(L"USERPROFILE", user, 32768);
    candidates.push_back(fs::path(user) / L"scoop/apps/ffmpeg/current/bin/ffmpeg.exe");
    auto packages = DataDirectory().parent_path() / L"Microsoft/WinGet/Packages";
    std::error_code ec;
    for (fs::directory_iterator i(packages, ec), end; i != end && !ec; i.increment(ec))
        if (i->is_directory() && i->path().filename().wstring().find(L"FFmpeg") != std::wstring::npos) {
            for (fs::recursive_directory_iterator
                     k(i->path(), fs::directory_options::skip_permission_denied, ec),
                 ke;
                 k != ke && !ec; k.increment(ec)) {
                if (k.depth() > 3)
                    k.disable_recursion_pending();
                if (k->path().filename() == L"ffmpeg.exe")
                    candidates.push_back(k->path());
            }
        }
    std::set<fs::path> visited;
    for (const auto& p : candidates) {
        if (cancel.load())
            throw std::runtime_error("用户取消。");
        if (p.empty() || !fs::is_regular_file(p, ec) || !visited.insert(p).second)
            continue;
        try {
            return Check(p, cancel);
        } catch (const std::exception& e) {
            logger_.Write("Rejected FFmpeg " + PathText(p) + ": " + e.what());
        }
    }
    return {};
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
        throw std::runtime_error("FFmpeg download requires HTTPS.");
    InternetHandle session(WinHttpOpen(L"LogForge/0.1.0", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                       WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
    if (!session.h)
        throw std::runtime_error("无法初始化 HTTPS 下载。");
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
        throw std::runtime_error("无法建立 HTTPS 请求。");
    DWORD redirects = WINHTTP_OPTION_REDIRECT_POLICY_DISALLOW_HTTPS_TO_HTTP;
    WinHttpSetOption(request, WINHTTP_OPTION_REDIRECT_POLICY, &redirects, sizeof(redirects));
    if (!WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, nullptr, 0, 0, 0) ||
        !WinHttpReceiveResponse(request, nullptr))
        throw std::runtime_error("FFmpeg 下载连接失败：" + Utf8(WinError()));
    DWORD status = 0, len = sizeof(status);
    WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                        WINHTTP_HEADER_NAME_BY_INDEX, &status, &len, WINHTTP_NO_HEADER_INDEX);
    if (status != 200)
        throw std::runtime_error("FFmpeg 下载 HTTP " + std::to_string(status));
    wchar_t length[64]{};
    len = sizeof(length);
    uint64_t total = 0;
    if (WinHttpQueryHeaders(request, WINHTTP_QUERY_CONTENT_LENGTH, WINHTTP_HEADER_NAME_BY_INDEX, length, &len,
                            WINHTTP_NO_HEADER_INDEX))
        total = _wcstoui64(length, nullptr, 10);
    constexpr uint64_t limit = 512ull * 1024 * 1024;
    if (total > limit)
        throw std::runtime_error("FFmpeg archive exceeds the download size limit.");
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    if (!out)
        throw std::runtime_error("无法创建下载文件。");
    std::array<char, 65536> b{};
    uint64_t received = 0;
    for (;;) {
        if (cancel.load())
            throw std::runtime_error("用户取消。");
        DWORD got{};
        if (!WinHttpReadData(request, b.data(), static_cast<DWORD>(b.size()), &got))
            throw std::runtime_error("FFmpeg 下载中断：" + Utf8(WinError()));
        if (!got)
            break;
        received += got;
        if (received > limit)
            throw std::runtime_error("FFmpeg archive exceeds the download size limit.");
        out.write(b.data(), got);
        if (!out)
            throw std::runtime_error("磁盘空间不足或下载文件无法写入。");
        progress(received, total, L"下载 FFmpeg 8.1.2");
    }
    out.close();
    if (total && total != received)
        throw std::runtime_error("FFmpeg 下载不完整。");
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
            progress(1, 1, L"FFmpeg 已安装并通过验证");
            return current;
        } catch (const std::exception& e) {
            if (cancel.load())
                throw;
            log.Write(std::string("Managed installation needs repair: ") + e.what());
        }
    }
    if (fs::space(root).available < 1024ull * 1024 * 1024)
        throw std::runtime_error("磁盘空间不足：FFmpeg 安装至少需要 1 GB 临时空间。");
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
            progress(0, 0, L"下载失败，正在重试…");
        }
    }
    progress(0, 0, L"校验 SHA-256…");
    if (SHA256(zip) != spec.sha256)
        throw std::runtime_error("SHA-256 不匹配：已拒绝安装，未运行下载内容。");
    if (cancel.load())
        throw std::runtime_error("用户取消。");
    progress(0, 0, L"解压 FFmpeg…");
    auto tar = SystemExecutable(L"tar.exe");
    auto listing = RunProcess(tar, {L"-tf", zip.wstring()}, &cancel, 60);
    if (listing.exitCode)
        throw std::runtime_error("无法读取已验证的 FFmpeg ZIP。");
    std::istringstream lines(listing.output);
    std::string entry;
    while (std::getline(lines, entry)) {
        if (entry.empty())
            continue;
        auto p = fs::path(Wide(entry));
        if (p.is_absolute() || entry.find("..") != std::string::npos ||
            entry.find(':') != std::string::npos || entry.front() == '/' || entry.front() == '\\' ||
            !entry.starts_with(Utf8(spec.archiveRoot) + "/"))
            throw std::runtime_error("Unsafe path in FFmpeg ZIP.");
    }
    auto extracted = RunProcess(tar, {L"-xf", zip.wstring(), L"-C", staging.wstring()}, &cancel, 120);
    if (extracted.exitCode)
        throw std::runtime_error("FFmpeg 解压失败：" + extracted.error);
    FFmpegManager manager(log);
    auto found = manager.Check(staging / spec.archiveRoot / L"bin/ffmpeg.exe", cancel);
    auto target = root / spec.archiveRoot;
    if (fs::exists(target)) {
        try {
            auto existing = manager.Check(target / L"bin/ffmpeg.exe", cancel);
            fs::remove(zip);
            progress(1, 1, L"FFmpeg 已安装并通过验证");
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
    fs::remove(zip);
    fs::remove(staging);
    progress(1, 1, L"FFmpeg 安装完成");
    return manager.Check(target / L"bin/ffmpeg.exe", cancel);
}
} // namespace logforge
