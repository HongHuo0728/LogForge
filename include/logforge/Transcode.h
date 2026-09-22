#pragma once
#include "FFmpeg.h"

namespace logforge {
struct JobProgress {
    std::wstring stage;
    double fraction = -1, seconds = 0, speed = 0, fps = 0;
    int64_t frame = 0;
};
using JobCallback = std::function<void(const JobProgress&)>;
class TranscodeJob {
  public:
    static ValidationReport Run(const FFmpegInstallation& tools, const MediaInfo& source,
                                const fs::path& destination, Logger& logger, const std::atomic_bool& cancel,
                                const JobCallback& progress);
};
} // namespace logforge
