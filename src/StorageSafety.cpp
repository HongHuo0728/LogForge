#include "logforge/StorageSafety.h"
#include "logforge/Localization.h"
#include "logforge/Platform.h"
#include <chrono>

namespace logforge {
namespace {
using Json = nlohmann::json;
uint64_t Started(HANDLE process) {
    FILETIME a{}, b{}, c{}, d{};
    if (!GetProcessTimes(process, &a, &b, &c, &d))
        return 0;
    return (uint64_t(a.dwHighDateTime) << 32) | a.dwLowDateTime;
}
Json Identity(HANDLE h) {
    BY_HANDLE_FILE_INFORMATION i{};
    if (!GetFileInformationByHandle(h, &i) || (i.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT))
        return Json();
    return {{"volume", i.dwVolumeSerialNumber},
            {"id_high", i.nFileIndexHigh},
            {"id_low", i.nFileIndexLow},
            {"created_high", i.ftCreationTime.dwHighDateTime},
            {"created_low", i.ftCreationTime.dwLowDateTime}};
}
Json Owner() {
    return {{"schema", "LogForge-owned-v1"},
            {"pid", GetCurrentProcessId()},
            {"process_start", Started(GetCurrentProcess())}};
}
bool DeadOwner(const Json& j) {
    if (j.value("schema", "") != "LogForge-owned-v1" || !j.value("process_start", uint64_t{}))
        return false;
    Handle process(
        OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, j.at("pid").get<DWORD>()));
    if (!process)
        return GetLastError() == ERROR_INVALID_PARAMETER; // Access denied is not evidence of death.
    const auto started = Started(process.get());
    return WaitForSingleObject(process.get(), 0) == WAIT_OBJECT_0 ||
           (started && started != j.at("process_start").get<uint64_t>());
}
bool EraseTracked(const Json& entry) {
    const fs::path p = Wide(entry.at("path").get<std::string>());
    const auto name = p.filename().wstring();
    if (!p.is_absolute() || name.find(L".logforge-") == std::wstring::npos ||
        (!name.ends_with(L".partial.mov") && !name.ends_with(L".rotated.mov")))
        return true;
    Handle file(CreateFileW(p.c_str(), DELETE | FILE_READ_ATTRIBUTES, 0, nullptr, OPEN_EXISTING,
                            FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
    if (!file)
        return GetLastError() == ERROR_FILE_NOT_FOUND || GetLastError() == ERROR_PATH_NOT_FOUND;
    if (Identity(file.get()) != entry.at("identity"))
        return true; // Replaced file is never ours to delete.
    FILE_DISPOSITION_INFO erase{TRUE};
    return SetFileInformationByHandle(file.get(), FileDispositionInfo, &erase, sizeof(erase)) != FALSE;
}
void Recover() {
    std::error_code ec;
    const auto dir = DataDirectory() / L"owned-jobs";
    for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
        if (it->is_symlink(ec) || !it->is_regular_file(ec) || it->path().extension() != L".json")
            continue;
        try {
            if (it->file_size() > 65536)
                continue;
            Json j;
            std::ifstream(it->path()) >> j;
            if (!DeadOwner(j))
                continue;
            bool complete = true;
            for (const auto& e : j.at("files"))
                complete = EraseTracked(e) && complete;
            if (complete)
                fs::remove(it->path(), ec); // Retry locked files at the next startup.
        } catch (...) {                     /* Malformed ownership is never authority to delete. */
        }
    }
}
void InstallRetention() {
    std::error_code ec;
    const auto root = DataDirectory() / L"tools/ffmpeg";
    for (fs::directory_iterator it(root, ec), end; !ec && it != end; it.increment(ec)) {
        if (it->is_symlink(ec) || !it->is_directory(ec))
            continue;
        const auto p = it->path();
        const auto name = p.filename().wstring();
        const auto attributes = GetFileAttributesW(p.c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_REPARSE_POINT))
            continue;
        if (!name.starts_with(L"install-") && name.find(L".replaced-") == std::wstring::npos)
            continue;
        try {
            const auto marker = p / L".logforge-owned.json";
            if (!fs::is_regular_file(marker) || fs::is_symlink(marker) || fs::file_size(marker) > 4096)
                continue;
            if (fs::file_time_type::clock::now() - fs::last_write_time(marker) < std::chrono::hours(24 * 7))
                continue;
            Json j;
            std::ifstream(marker) >> j;
            if (!DeadOwner(j) || j.at("directory") != PathText(fs::weakly_canonical(p)))
                continue;
            bool safe = true;
            for (fs::recursive_directory_iterator child(p, ec), last; !ec && child != last;
                 child.increment(ec)) {
                const auto attrs = GetFileAttributesW(child->path().c_str());
                if (attrs == INVALID_FILE_ATTRIBUTES || (attrs & FILE_ATTRIBUTE_REPARSE_POINT)) {
                    safe = false;
                    break;
                }
            }
            if (safe && !ec)
                fs::remove_all(p, ec);
        } catch (...) {
        }
    }
}
} // namespace
OwnedJobFiles::OwnedJobFiles() : data_(Owner()) {
    const auto dir = DataDirectory() / L"owned-jobs";
    RequireWritableDirectory(dir);
    journal_ =
        dir / (std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()) + L".json");
    data_["files"] = Json::array();
}
void OwnedJobFiles::Track(const fs::path& path) {
    Handle file(CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                            OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
    const auto id = file ? Identity(file.get()) : Json();
    if (id.is_null())
        throw AppError(TextId::ReportSave);
    data_["files"].push_back({{"path", PathText(fs::absolute(path))}, {"identity", id}});
    const auto temporary = fs::path(journal_.wstring() + L".tmp");
    {
        std::ofstream out(temporary);
        out << data_.dump();
        out.close();
        if (!out)
            throw AppError(TextId::ReportSave);
    }
    if (!MoveFileExW(temporary.c_str(), journal_.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        throw AppError(TextId::ReportSave);
}
OwnedJobFiles::~OwnedJobFiles() {
    try {
        bool complete = true;
        for (const auto& e : data_.at("files"))
            complete = EraseTracked(e) && complete;
        std::error_code ec;
        if (complete)
            fs::remove(journal_, ec);
    } catch (...) {
    }
}
void MarkOwnedInstallDirectory(const fs::path& p) {
    auto j = Owner();
    j["directory"] = PathText(fs::weakly_canonical(p));
    std::ofstream f(p / L".logforge-owned.json");
    f << j.dump();
    f.close();
    if (!f)
        throw AppError(TextId::ReportSave);
}
StateLock::StateLock() {
    const auto dir = DataDirectory();
    fs::create_directories(dir);
    file_.reset(CreateFileW((dir / L".state.lock").c_str(), GENERIC_READ | GENERIC_WRITE,
                            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_HIDDEN,
                            nullptr));
    if (!file_ || !LockFileEx(file_.get(), LOCKFILE_EXCLUSIVE_LOCK, 0, 1, 0, &offset_))
        throw AppError(Message(TextId::StateLock, {Utf8(WinError())}));
}
StateLock::~StateLock() {
    if (file_)
        UnlockFileEx(file_.get(), 0, 1, 0, &offset_);
}
void RequireWritableDirectory(const fs::path& dir) {
    std::error_code ec;
    fs::create_directories(dir, ec);
    const auto p = dir / (L".logforge-write-probe-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
                          std::to_wstring(GetTickCount64()));
    Handle f(CreateFileW(p.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                         FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, nullptr));
    DWORD n = 0;
    if (!f || !WriteFile(f.get(), "LogForge", 8, &n, nullptr) || n != 8 || !FlushFileBuffers(f.get()))
        throw AppError(Message(TextId::StorageWrite, {PathText(dir) + ": " + Utf8(WinError())}));
}
void MaintainOwnedStorage() {
    StateLock lock;
    Recover();
    InstallRetention();
    // Only LogForge-generated names in its own log directory; never search user
    // media directories by extension. Active files cannot be opened for deletion.
    std::error_code ec;
    const auto dir = DataDirectory() / L"logs";
    const auto now = fs::file_time_type::clock::now();
    for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
        const auto& p = it->path();
        if (it->is_symlink(ec) || !it->is_regular_file(ec))
            continue;
        const auto name = p.filename().wstring();
        if (!name.starts_with(L"LogForge-") ||
            (p.extension() != L".log" && !name.ends_with(L".validation.json")))
            continue;
        const auto time = it->last_write_time(ec);
        if (ec || now - time < std::chrono::hours(24 * 30))
            continue;
        Handle f(
            CreateFileW(p.c_str(), DELETE, 0, nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
        if (f) {
            FILE_DISPOSITION_INFO disposition{TRUE};
            SetFileInformationByHandle(f.get(), FileDispositionInfo, &disposition, sizeof(disposition));
        }
    }
}
} // namespace logforge
