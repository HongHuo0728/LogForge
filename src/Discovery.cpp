#include "logforge/Discovery.h"
#include "logforge/Localization.h"
#include <cwctype>
#include <set>

namespace logforge {
namespace {
std::wstring Extended(const fs::path& path) {
    const auto s = fs::absolute(path).wstring();
    if (s.starts_with(L"\\\\?\\"))
        return s;
    if (s.starts_with(L"\\\\"))
        return L"\\\\?\\UNC\\" + s.substr(2);
    return L"\\\\?\\" + s;
}
std::wstring Key(const fs::path& path) {
    auto key = fs::absolute(path).lexically_normal().wstring();
    for (auto& c : key)
        c = static_cast<wchar_t>(std::towlower(c));
    return key;
}
struct FindHandle {
    HANDLE value;
    ~FindHandle() {
        if (value != INVALID_HANDLE_VALUE)
            FindClose(value);
    }
};
struct VolumeHandle {
    HANDLE value;
    ~VolumeHandle() {
        if (value != INVALID_HANDLE_VALUE)
            FindVolumeClose(value);
    }
};
} // namespace
std::vector<fs::path> LocalDriveRoots() {
    std::vector<fs::path> roots;
    std::set<std::wstring> seen;
    const auto add = [&](const wchar_t* path) {
        const auto type = GetDriveTypeW(path);
        if ((type == DRIVE_FIXED || type == DRIVE_REMOVABLE) && seen.insert(Key(path)).second)
            roots.emplace_back(path);
    };
    DWORD size = GetLogicalDriveStringsW(0, nullptr);
    if (size) {
        std::vector<wchar_t> buffer(size + 1);
        if (GetLogicalDriveStringsW(static_cast<DWORD>(buffer.size()), buffer.data()))
            for (auto p = buffer.data(); *p; p += wcslen(p) + 1)
                add(p);
    }
    // Also cover local volumes mounted in folders rather than assigned a drive letter.
    wchar_t volume[MAX_PATH]{};
    VolumeHandle find{FindFirstVolumeW(volume, MAX_PATH)};
    if (find.value != INVALID_HANDLE_VALUE) {
        do {
            DWORD needed = 0;
            GetVolumePathNamesForVolumeNameW(volume, nullptr, 0, &needed);
            if (!needed)
                continue;
            std::vector<wchar_t> paths(needed + 1);
            if (GetVolumePathNamesForVolumeNameW(volume, paths.data(), static_cast<DWORD>(paths.size()),
                                                 &needed))
                for (auto p = paths.data(); *p; p += wcslen(p) + 1)
                    add(p);
        } while (FindNextVolumeW(find.value, volume, MAX_PATH));
    }
    return roots;
}
DiscoveryProgress SearchFFmpegDirectories(const std::vector<fs::path>& roots, const std::atomic_bool& cancel,
                                          const std::function<bool(const fs::path&)>& accept,
                                          const DiscoveryCallback& progress) {
    DiscoveryProgress state;
    std::set<std::wstring> seenRoots;
    ULONGLONG lastUpdate = 0;
    const auto publish = [&](bool force) {
        const auto now = GetTickCount64();
        if (progress && (force || now - lastUpdate >= 200)) {
            progress(state);
            lastUpdate = now;
        }
    };
    for (const auto& root : roots) {
        if (cancel.load())
            throw AppError(TextId::Cancelled);
        if (!seenRoots.insert(Key(root)).second)
            continue;
        state.drive = root;
        state.phase = DiscoveryPhase::ScanningDrive;
        publish(true);
        std::vector<fs::path> pending{root};
        while (!pending.empty()) {
            if (cancel.load())
                throw AppError(TextId::Cancelled);
            auto directory = std::move(pending.back());
            pending.pop_back();
            WIN32_FIND_DATAW data{};
            FindHandle search{FindFirstFileExW(Extended(directory / L"*").c_str(), FindExInfoBasic, &data,
                                               FindExSearchNameMatch, nullptr, FIND_FIRST_EX_LARGE_FETCH)};
            if (search.value == INVALID_HANDLE_VALUE) {
                ++state.skipped;
                publish(false);
                continue;
            }
            ++state.directories;
            do {
                if (cancel.load())
                    throw AppError(TextId::Cancelled);
                if (wcscmp(data.cFileName, L".") == 0 || wcscmp(data.cFileName, L"..") == 0)
                    continue;
                const fs::path path = directory / data.cFileName;
                if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                    // No junction/symlink loops, cloud hydration, recycle bins or protected restore data.
                    if ((data.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_OFFLINE)) ||
                        _wcsicmp(data.cFileName, L"$Recycle.Bin") == 0 ||
                        _wcsicmp(data.cFileName, L"System Volume Information") == 0)
                        ++state.skipped;
                    else
                        pending.push_back(path);
                } else if (_wcsicmp(data.cFileName, L"ffmpeg.exe") == 0) {
                    ++state.candidates;
                    state.phase = DiscoveryPhase::CheckingCandidate;
                    publish(true);
                    if (!(data.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_OFFLINE)) &&
                        accept(path)) {
                        state.found = true;
                        publish(true);
                        return state;
                    }
                    state.phase = DiscoveryPhase::ScanningDrive;
                }
            } while (FindNextFileW(search.value, &data));
            if (GetLastError() != ERROR_NO_MORE_FILES)
                ++state.skipped;
            publish(false);
        }
    }
    publish(true);
    return state;
}
} // namespace logforge
