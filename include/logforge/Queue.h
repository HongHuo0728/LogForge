#pragma once
#include "Transcode.h"
namespace logforge {
using QueueCallback = std::function<void(size_t, size_t, const MediaInfo&, const JobProgress&)>;
Json RunQueue(const FFmpegInstallation& tools, const std::vector<fs::path>& inputs, const fs::path& directory,
              Logger& logger, const std::atomic_bool& cancel, const TranscodeOptions& options,
              const std::string& explicitChroma, const QueueCallback& progress);
} // namespace logforge
