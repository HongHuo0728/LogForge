#pragma once
#include "Transcode.h"
namespace logforge {
using QueueCallback = std::function<void(size_t, size_t, const MediaInfo&, const JobProgress&)>;
struct QueueItem {
    fs::path input, output;
};
using QueuePlan = std::vector<QueueItem>;
QueuePlan PlanQueue(const std::vector<fs::path>& inputs, const fs::path& directory);
Json QueuePlanJson(const QueuePlan& plan);
Json RunQueue(const FFmpegInstallation& tools, const QueuePlan& plan, Logger& logger,
              const std::atomic_bool& cancel, const TranscodeOptions& options,
              const std::string& explicitChroma, const QueueCallback& progress);
Json RunQueue(const FFmpegInstallation& tools, const std::vector<fs::path>& inputs, const fs::path& directory,
              Logger& logger, const std::atomic_bool& cancel, const TranscodeOptions& options,
              const std::string& explicitChroma, const QueueCallback& progress);
} // namespace logforge
