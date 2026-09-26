#pragma once
#include "Platform.h"
#include <nlohmann/json.hpp>
namespace logforge {
// Crash-recovery journal: file IDs, process start time, and exact owned paths.
// A wildcard match is never proof that a video belongs to LogForge.
class OwnedJobFiles {
  public:
    OwnedJobFiles();
    ~OwnedJobFiles();
    void Track(const fs::path& path);

  private:
    fs::path journal_;
    nlohmann::json data_;
};
void MarkOwnedInstallDirectory(const fs::path& path);
} // namespace logforge
