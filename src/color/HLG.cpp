#include "logforge/Color.h"
#include "logforge/Localization.h"
#include <algorithm>
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
    return 1.0 / HLG::DecodeToSceneLinear(0.75);
}
double HLGToAppleLog(double v, double stops) noexcept {
    return AppleLogTransferFunction::EncodeLinearToAppleLog(HLG::DecodeToSceneLinear(v) *
                                                            HLGToReflectanceScale() * std::exp2(stops));
}
void ValidateExposureStops(double stops) {
    if (!std::isfinite(stops) || stops < -8 || stops > 8)
        throw AppError(TextId::ExposureRange);
}
void ValidateToneAdjustments(const ToneAdjustments& tone) {
    if (!std::isfinite(tone.shadowStops) || tone.shadowStops < 0 || tone.shadowStops > 3 ||
        !std::isfinite(tone.highlightStops) || tone.highlightStops < 0 || tone.highlightStops > 3 ||
        !std::isfinite(tone.saturation) || tone.saturation < 0 || tone.saturation > 1.5)
        throw AppError(TextId::ToneRange);
}
std::array<double, 3> AdjustSceneLinearBT2020(const std::array<double, 3>& rgb,
                                              const ToneAdjustments& tone) noexcept {
    if (!tone.enabled)
        return rgb;
    const double y = 0.2627 * rgb[0] + 0.6780 * rgb[1] + 0.0593 * rgb[2];
    // Global, time-invariant curve anchored at 18% scene-linear gray. The
    // six-stop smoothstep and <=3 EV limits keep neutral luminance monotonic:
    // d(log2 Yout)/d(log2 Yin) >= 1 - 1.5*3/6 = 0.25.
    const double x = y > 0 ? std::log2(y / 0.18) : -6.0;
    const double t = std::clamp(std::abs(x) / 6.0, 0.0, 1.0);
    const double weight = t * t * (3.0 - 2.0 * t);
    const double gain = std::exp2((x < 0 ? tone.shadowStops : -tone.highlightStops) * weight);
    std::array<double, 3> out{};
    for (size_t c = 0; c < out.size(); ++c)
        out[c] = gain * (y + tone.saturation * (rgb[c] - y));
    return out;
}
void TransformHLGToAppleLog(std::span<float> pixels, double stops, const ToneAdjustments& tone,
                            SignalStatistics* stats) {
    ValidateExposureStops(stops);
    ValidateToneAdjustments(tone);
    const double scale = HLGToReflectanceScale() * std::exp2(stops);
    auto encode = [&](float& v, double linear) {
        if (!std::isfinite(v))
            throw AppError(TextId::NonFiniteInput);
        const double result = AppleLogTransferFunction::EncodeLinearToAppleLog(linear);
        if (!std::isfinite(linear) || !std::isfinite(result))
            throw AppError(TextId::NonFiniteOutput);
        if (stats) {
            ++stats->samples;
            stats->inputMinimum = std::min(stats->inputMinimum, static_cast<double>(v));
            stats->inputMaximum = std::max(stats->inputMaximum, static_cast<double>(v));
            stats->outputMinimum = std::min(stats->outputMinimum, result);
            stats->outputMaximum = std::max(stats->outputMaximum, result);
            stats->appleFloorClipped += linear < AppleLogTransferFunction::R0;
            stats->aboveNominalWhite += result > 1;
        }
        v = static_cast<float>(result);
    };
    if (!tone.enabled) {
        for (auto& v : pixels)
            encode(v, HLG::DecodeToSceneLinear(v) * scale);
        return;
    }
    if (pixels.size() % 3)
        throw AppError(TextId::InvalidPlanes);
    const auto plane = pixels.size() / 3;
    for (size_t i = 0; i < plane; ++i) {
        // FFmpeg gbrpf32le plane order is G, B, R. Tone processing is RGB.
        const auto rgb = AdjustSceneLinearBT2020({HLG::DecodeToSceneLinear(pixels[2 * plane + i]) * scale,
                                                  HLG::DecodeToSceneLinear(pixels[i]) * scale,
                                                  HLG::DecodeToSceneLinear(pixels[plane + i]) * scale},
                                                 tone);
        encode(pixels[2 * plane + i], rgb[0]);
        encode(pixels[i], rgb[1]);
        encode(pixels[plane + i], rgb[2]);
    }
}
} // namespace logforge
