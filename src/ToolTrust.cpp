#include "logforge/ToolTrust.h"
#include "logforge/Localization.h"
#include <cwctype>

namespace logforge {
namespace {
using Json = nlohmann::json;
std::mutex trustMutex;
std::string Key(const fs::path& p) {
    auto path = fs::weakly_canonical(fs::absolute(p)).wstring();
    for (auto& c : path)
        c = static_cast<wchar_t>(std::towlower(c));
    return Utf8(path);
}
Json Read() {
    try {
        std::ifstream f(DataDirectory() / L"ffmpeg-trust.json");
        Json j;
        f >> j;
        if (j.is_object())
            return j;
    } catch (...) {
    }
    return Json::object();
}
std::shared_ptr<ToolLease> Lock(const fs::path& p) {
    auto lease = std::make_shared<ToolLease>();
    lease->identity.ffmpeg = fs::weakly_canonical(fs::absolute(p));
    lease->identity.ffprobe = fs::weakly_canonical(lease->identity.ffmpeg.parent_path() / L"ffprobe.exe");
    if (lease->identity.ffprobe.parent_path() != lease->identity.ffmpeg.parent_path())
        throw AppError(TextId::FFmpegPair);
    if (!fs::is_regular_file(lease->identity.ffmpeg) || !fs::is_regular_file(lease->identity.ffprobe))
        throw AppError(TextId::FFmpegPair);
    for (auto pair : {std::pair{&lease->ffmpegFile, &lease->identity.ffmpeg},
                      std::pair{&lease->ffprobeFile, &lease->identity.ffprobe}}) {
        pair.first->reset(CreateFileW(pair.second->c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                                      OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
        if (!*pair.first)
            throw AppError(Message(TextId::FFmpegTrustLock, {PathText(*pair.second)}));
    }
    lease->identity.ffmpegHash = SHA256(lease->identity.ffmpeg);
    lease->identity.ffprobeHash = SHA256(lease->identity.ffprobe);
    return lease;
}
void Save(const ToolIdentity& id) {
    std::lock_guard lock(trustMutex);
    auto j = Read();
    j[Key(id.ffmpeg)] = id.ToJson();
    const auto dir = DataDirectory();
    fs::create_directories(dir);
    const auto tmp = dir / (L"trust-" + std::to_wstring(GetCurrentProcessId()) + L".tmp");
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        f << j.dump(2);
        f.close();
        if (!f)
            throw AppError(TextId::SettingsSaveFailed);
    }
    if (!MoveFileExW(tmp.c_str(), (dir / L"ffmpeg-trust.json").c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        throw AppError(TextId::SettingsSaveFailed);
}
} // namespace
Json ToolIdentity::ToJson() const {
    return {{"path", PathText(ffmpeg)}, {"ffprobe_path", PathText(ffprobe)},
            {"sha256", ffmpegHash},     {"ffprobe_sha256", ffprobeHash},
            {"trust_type", trustType},  {"verified_archive_sha256", archiveHash}};
}
ToolIdentity ToolTrust::Inspect(const fs::path& p) {
    return Lock(p)->identity;
}
void ToolTrust::ApproveManual(const ToolIdentity& approved) {
    auto lease = Lock(approved.ffmpeg);
    if (lease->identity.ffmpegHash != approved.ffmpegHash ||
        lease->identity.ffprobeHash != approved.ffprobeHash ||
        Key(lease->identity.ffprobe) != Key(approved.ffprobe))
        throw AppError(TextId::FFmpegHashChanged);
    lease->identity.trustType = "user-approved";
    Save(lease->identity);
}
void ToolTrust::RecordDownload(const fs::path& p, const std::string& archiveHash) {
    auto lease = Lock(p);
    lease->identity.trustType = "managed-sha256";
    lease->identity.archiveHash = archiveHash;
    Save(lease->identity);
}
bool ToolTrust::HasApproval(const fs::path& p) {
    std::lock_guard lock(trustMutex);
    return Read().contains(Key(p));
}
std::shared_ptr<ToolLease> ToolTrust::Acquire(const fs::path& p) {
    // Look up approval BEFORE hashing/locking unfamiliar binaries during discovery.
    Json record;
    {
        std::lock_guard lock(trustMutex);
        auto j = Read();
        const auto it = j.find(Key(p));
        if (it == j.end())
            throw AppError(Message(TextId::FFmpegUntrusted, {PathText(p)}));
        record = *it;
    }
    auto lease = Lock(p);
    if (record.value("sha256", "") != lease->identity.ffmpegHash ||
        record.value("ffprobe_sha256", "") != lease->identity.ffprobeHash ||
        record.value("ffprobe_path", "") != PathText(lease->identity.ffprobe))
        throw AppError(TextId::FFmpegHashChanged);
    lease->identity.trustType = record.value("trust_type", "");
    lease->identity.archiveHash = record.value("verified_archive_sha256", "");
    if (lease->identity.trustType != "user-approved" &&
        !(lease->identity.trustType == "managed-sha256" && lease->identity.archiveHash.size() == 64))
        throw AppError(Message(TextId::FFmpegUntrusted, {PathText(p)}));
    return lease;
}
} // namespace logforge
