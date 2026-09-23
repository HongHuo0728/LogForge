#pragma once
#include "Platform.h"
#include <memory>
#include <nlohmann/json.hpp>

namespace logforge {
struct ToolIdentity {
    fs::path ffmpeg, ffprobe;
    std::string ffmpegHash, ffprobeHash, trustType, archiveHash;
    nlohmann::json ToJson() const;
};
// Holds deny-write/deny-delete handles for both approved executable images.
// Keep the lease alive through ALL child executions, including ffprobe.
struct ToolLease {
    ToolIdentity identity;
    Handle ffmpegFile, ffprobeFile;
};
class ToolTrust {
  public:
    static ToolIdentity Inspect(const fs::path& executable); // read only, never starts a process
    static void ApproveManual(const ToolIdentity& explicitlyApproved);
    static std::shared_ptr<ToolLease> Acquire(const fs::path& executable);
    static bool HasApproval(const fs::path& executable);

  private:
    friend class FFmpegDownloader;
    static void RecordDownload(const fs::path& executable, const std::string& verifiedArchiveHash);
};
} // namespace logforge
