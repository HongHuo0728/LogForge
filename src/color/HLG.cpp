#include "logforge/Color.h"
#include <cmath>
#include <stdexcept>

namespace logforge {
double HLG::DecodeToSceneLinear(double v) noexcept {
    const double b = 1.0 - 4.0 * A;
    const double c = 0.5 - A * std::log(4.0 * A);
    // Signed extension below black preserves small matrix/resampling excursions.
    if (v < 0.0)
        return -v * v / 3.0;
    return v <= 0.5 ? v * v / 3.0 : (std::exp((v - c) / A) + b) / 12.0;
}
double HLG::EncodeSceneLinear(double v) noexcept {
    const double b = 1.0 - 4.0 * A;
    const double c = 0.5 - A * std::log(4.0 * A);
    if (v < 0.0)
        return -std::sqrt(-3.0 * v);
    return v <= 1.0 / 12.0 ? std::sqrt(3.0 * v) : A * std::log(12.0 * v - b) + c;
}
double HLGToReflectanceScale() noexcept {
    return 0.9 / HLG::DecodeToSceneLinear(0.75);
}
double HLGToAppleLog(double v, double stops) noexcept {
    return AppleLogTransferFunction::EncodeLinearToAppleLog(HLG::DecodeToSceneLinear(v) *
                                                            HLGToReflectanceScale() * std::exp2(stops));
}
void TransformHLGToAppleLog(std::span<float> pixels, double stops) {
    if (!std::isfinite(stops) || stops < -8 || stops > 8)
        throw std::runtime_error("Invalid exposure offset.");
    const double scale = HLGToReflectanceScale() * std::exp2(stops);
    for (auto& v : pixels) {
        if (!std::isfinite(v))
            throw std::runtime_error("Non-finite decoded RGB sample.");
        v = static_cast<float>(
            AppleLogTransferFunction::EncodeLinearToAppleLog(HLG::DecodeToSceneLinear(v) * scale));
    }
}
} // namespace logforge
