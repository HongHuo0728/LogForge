#pragma once
#include "Platform.h"

namespace logforge {
enum class DiscoveryPhase { CheckingPaths, ScanningDrive, CheckingCandidate };
struct DiscoveryProgress {
    DiscoveryPhase phase = DiscoveryPhase::CheckingPaths;
    fs::path drive;
    uint64_t directories = 0, candidates = 0, skipped = 0;
    bool found = false;
};
using DiscoveryCallback = std::function<void(const DiscoveryProgress&)>;
std::vector<fs::path> LocalDriveRoots();
// Enumeration itself never executes a file. Production callbacks may check ONLY
// previously approved hashes, and collect unknown paths for explicit review.
// Explicit roots make the traversal independently testable without scanning a user's drives.
DiscoveryProgress SearchFFmpegDirectories(const std::vector<fs::path>& roots, const std::atomic_bool& cancel,
                                          const std::function<bool(const fs::path&)>& accept,
                                          const DiscoveryCallback& progress = {});
} // namespace logforge
