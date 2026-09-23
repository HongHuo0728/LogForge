#pragma once
#include "Localization.h"
#include "Platform.h"
#include <nlohmann/json.hpp>
#include <optional>

namespace logforge {
enum class DiscoveryMode { Quick, Deep };
enum class DiscoveryEnd { Completed, TimedOut, Cancelled, Failed };
enum class CandidateState { Unapproved, MissingProbe, Changed, Incompatible, Verified };
struct DiscoveryCandidate {
    fs::path ffmpeg, ffprobe;
    std::string source;
    bool paired = false;
    CandidateState state = CandidateState::Unapproved;
    Message issue{TextId::FFmpegDiscovered};
    nlohmann::json ToJson() const;
};
enum class DiscoveryPhase { CheckingPaths, ScanningDrive, CheckingCandidate, Verifying };
struct DiscoveryProgress {
    DiscoveryPhase phase = DiscoveryPhase::CheckingPaths;
    fs::path drive;
    uint64_t directories = 0, candidates = 0, skipped = 0;
    bool found = false;
    uint64_t elapsedMs = 0;
    std::optional<DiscoveryCandidate> candidate;
};
struct DiscoveryReport {
    DiscoveryMode mode = DiscoveryMode::Quick;
    DiscoveryEnd end = DiscoveryEnd::Completed;
    std::vector<DiscoveryCandidate> candidates;
    uint64_t elapsedMs = 0, verificationMs = 0, directories = 0, skipped = 0;
    std::string diagnostic;
    nlohmann::json ToJson() const;
};
struct DiscoveryOptions {
    DiscoveryMode mode = DiscoveryMode::Quick;
    // Empty in production. Explicit isolated roots for deterministic tests.
    std::vector<fs::path> roots;
    // Split prioritized discovery around validation without charging hashing or
    // numeric checks against the shared filesystem discovery budget.
    bool preferredOnly = false, skipPreferred = false;
    uint64_t budgetMs = 3000;
};
using DiscoveryCallback = std::function<void(const DiscoveryProgress&)>;
// Runs this application's hidden discovery mode, never a discovered executable.
DiscoveryReport DiscoverFFmpegPaths(const DiscoveryOptions&, const std::atomic_bool&,
                                    const DiscoveryCallback& = {}, const fs::path& helper = {});
// Internal entry point shared by the GUI and CLI, before constructing a window/logger.
int RunDiscoveryHelper(const std::vector<std::wstring>& args);
// Read-only registry parsing; a separate key permits isolated volatile-key tests.
std::vector<fs::path> RegisteredFFmpegPaths(
    const std::wstring& key = L"Software\\Microsoft\\Windows\\CurrentVersion\\App Paths\\ffmpeg.exe");
std::vector<fs::path> LocalDriveRoots();
// Enumeration itself never executes a file. Production callbacks may check ONLY
// previously approved hashes, and collect unknown paths for explicit review.
// Explicit roots make the traversal independently testable without scanning a user's drives.
DiscoveryProgress SearchFFmpegDirectories(const std::vector<fs::path>& roots, const std::atomic_bool& cancel,
                                          const std::function<bool(const fs::path&)>& accept,
                                          const DiscoveryCallback& progress = {});
} // namespace logforge
