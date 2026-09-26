#pragma once
#include "Color.h"
#include <memory>
#include <nlohmann/json.hpp>

namespace logforge {
enum class ProcessingBackend { Auto, CPU, CUDA };
const char* BackendName(ProcessingBackend backend);
class CudaTransformer {
  public:
    CudaTransformer(double exposure, const ToneAdjustments& tone);
    ~CudaTransformer();
    CudaTransformer(const CudaTransformer&) = delete;
    CudaTransformer& operator=(const CudaTransformer&) = delete;
    // On error the caller's original input remains intact for CPU fallback.
    void Apply(std::span<float> samples, SignalStatistics& cumulative);
    nlohmann::json Report() const;
    static nlohmann::json Qualify();

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace logforge
