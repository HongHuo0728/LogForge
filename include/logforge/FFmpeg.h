#pragma once
#include "Discovery.h"
#include "Media.h"
#include "ToolTrust.h"
#include <optional>

namespace logforge {
struct FFmpegInstallation {
    fs::path ffmpeg, ffprobe;
    std::string version;
    std::shared_ptr<ToolLease> lease;
    bool numericallyVerified = false;
    Json numeric = Json::object();
    Json ToJson() const;
};
class FFmpegManager {
  public:
    explicit FFmpegManager(Logger& logger) : logger_(logger) {}
    std::optional<FFmpegInstallation> Detect(const std::atomic_bool& cancel,
                                             const DiscoveryCallback& progress = {},
                                             const DiscoveryOptions& options = {});
    const DiscoveryReport& Discovery() const {
        return discovery_;
    }
    FFmpegInstallation Check(const fs::path& executable, const std::atomic_bool& cancel, bool smoke = true);
    void SaveManual(const fs::path& executable);
    const std::vector<fs::path>& UnapprovedCandidates() const {
        return unapproved_;
    }
    bool DiscoverCandidate(const fs::path& path, const std::atomic_bool& cancel,
                           std::optional<FFmpegInstallation>& found);
    static fs::path ManagedExecutable();
    static std::vector<std::string> MissingCapabilities(const std::string& decoders,
                                                        const std::string& encoder,
                                                        const std::string& filters,
                                                        const std::string& formats);

  private:
    Logger& logger_;
    std::vector<fs::path> unapproved_;
    DiscoveryReport discovery_;
};
struct DownloadSpec {
    std::wstring name, version, url, archiveRoot;
    std::string sha256, license, sourceUrl;
};
class FFmpegBuildProvider {
  public:
    virtual ~FFmpegBuildProvider() = default;
    virtual DownloadSpec Release() const = 0;
};
class GyanReleaseProvider final : public FFmpegBuildProvider {
  public:
    DownloadSpec Release() const override;
};
using DownloadProgress = std::function<void(uint64_t, uint64_t, const Message&)>;
class FFmpegDownloader {
  public:
    static FFmpegInstallation Install(const FFmpegBuildProvider& provider, Logger& logger,
                                      const std::atomic_bool& cancel, const DownloadProgress& progress);
};
} // namespace logforge
