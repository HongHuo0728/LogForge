#pragma once
#include "Color.h"
#include <memory>
namespace logforge {
// Persistent CPU workers. Scalar color equations remain the reference and are
// invoked on bounded tiles; no approximate log/exp or fast-math is used.
class FloatTransformer {
  public:
    FloatTransformer(double exposure, const ToneAdjustments& tone, unsigned workers = 0);
    ~FloatTransformer();
    void Apply(std::span<float> pixels, SignalStatistics& cumulative);
    unsigned Workers() const;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace logforge
