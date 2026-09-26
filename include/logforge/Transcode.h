#pragma once
#include "Color.h"
#include "CudaTransformer.h"
#include "FFmpeg.h"

namespace logforge {
struct JobProgress {
    Message stage{TextId::Starting};
    double fraction = -1, seconds = 0, speed = 0, fps = 0;
    int64_t frame = 0;
};
using JobCallback = std::function<void(const JobProgress&)>;
struct JobIOHooks {
    // Dependency injection for deterministic disk-full/report/CUDA-fault tests.
    // Production GUI and CLI never set these hooks; there are no environment overrides.
    std::function<uintmax_t(const fs::path&)> availableSpace;
    std::function<void(const fs::path&)> beforeReportWrite;
    std::function<void(const char*)> cudaCheckpoint;
};
struct TranscodeOptions {
    // A scene-linear exposure gain before the unchanged Apple Log encoding.
    double exposureStops = 0;
    ToneAdjustments tone;
    ProcessingBackend backend = ProcessingBackend::Auto;
    const JobIOHooks* io = nullptr;
};
class TranscodeJob {
  public:
    static ValidationReport Run(const FFmpegInstallation& tools, const MediaInfo& source,
                                const fs::path& destination, Logger& logger, const std::atomic_bool& cancel,
                                const JobCallback& progress, const TranscodeOptions& options = {});
};
} // namespace logforge
