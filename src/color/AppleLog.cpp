#include "logforge/Color.h"
#include <cmath>

namespace logforge {
double AppleLogTransferFunction::EncodeLinearToAppleLog(double r) noexcept {
    if (r >= Rt)
        return Gamma * std::log2(r + Beta) + Delta;
    if (r >= R0)
        return C * (r - R0) * (r - R0);
    return 0.0;
}
double AppleLogTransferFunction::DecodeAppleLogToLinear(double p) noexcept {
    if (p >= Pt)
        return std::exp2((p - Delta) / Gamma) - Beta;
    if (p >= 0.0)
        return std::sqrt(p / C) + R0;
    return R0;
}
} // namespace logforge
