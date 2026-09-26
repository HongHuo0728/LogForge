#pragma once
#include "Media.h"
namespace logforge {
class PixelSanity {
  public:
    PixelSanity(const MediaInfo& input, int64_t frames);
    void Observe(std::span<const float> decoded);
    Json Validate(const fs::path& ffmpeg, const fs::path& output, const std::atomic_bool& cancel,
                  double exposure, const ToneAdjustments& tone);

  private:
    struct Patch {
        int x, y, w, h;
        std::vector<float> rgb;
    };
    struct Frame {
        int64_t number;
        std::vector<Patch> patches;
    };
    MediaInfo input_;
    std::vector<Frame> frames_;
    uint64_t offset_ = 0;
};
} // namespace logforge
