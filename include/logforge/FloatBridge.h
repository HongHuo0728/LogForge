#pragma once
#include "Color.h"
#include "Platform.h"
#include <span>
namespace logforge {
struct BridgeTiming {
    double read = 0, transform = 0, write = 0;
    size_t bufferBytes = 0;
    unsigned buffers = 0;
};
using TransformCallback = std::function<void(std::span<float>)>;
// Three bounded slots maintain byte/plane order while decoder, color processing
// and encoder overlap. Creative uses one full planar frame as the atomic unit.
int64_t RunFloatBridge(FFmpegProcess& decoder, FFmpegProcess& encoder, size_t frameSamples, bool creative,
                       const std::atomic_bool& cancel, const TransformCallback& transform,
                       BridgeTiming& timing);
struct ThreadPlan {
    unsigned logical, decode, encode, transform, filter = 1;
};
ThreadPlan PlanThreads(bool cuda);
} // namespace logforge
