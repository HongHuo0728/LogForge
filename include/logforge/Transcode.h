#pragma once
#include "Color.h"
#include "FFmpeg.h"

namespace logforge {
struct JobProgress {
    Message stage{TextId::Starting};
    double fraction = -1, seconds = 0, speed = 0, fps = 0;
    int64_t frame = 0;
};
using JobCallback = std::function<void(const JobProgress&)>;
struct TranscodeOptions {
    // A scene-linear exposure gain before the unchanged Apple Log encoding.
    double exposureStops = 0;
    ToneAdjustments tone;
};
class TranscodeJob {
  public:
    static ValidationReport Run(const FFmpegInstallation& tools, const MediaInfo& source,
                                const fs::path& destination, Logger& logger, const std::atomic_bool& cancel,
                                const JobCallback& progress, const TranscodeOptions& options = {});
};
} // namespace logforge
