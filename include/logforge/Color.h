#pragma once
#include <array>
#include <cstdint>
#include <limits>
#include <span>

namespace logforge {
// Apple Log Profile (September 2023), also verified against Apple's ACES IDT.
struct AppleLogTransferFunction {
    static constexpr double R0 = -0.05641088;
    static constexpr double Rt = 0.01;
    static constexpr double C = 47.28711236;
    static constexpr double Beta = 0.00964052;
    static constexpr double Gamma = 0.08550479;
    static constexpr double Delta = 0.69336945;
    static constexpr double Pt = C * (Rt - R0) * (Rt - R0);
    static double EncodeLinearToAppleLog(double linear) noexcept;
    static double DecodeAppleLogToLinear(double encoded) noexcept;
};

struct HLG {
    static constexpr double A = 0.17883277;
    static double DecodeToSceneLinear(double encoded) noexcept;
    static double EncodeSceneLinear(double linear) noexcept;
};

// ITU-R BT.2408 reference: 75% HLG = 100% reflecting diffuse white.
// Applying the nominal reference to footage does not recover camera exposure.
double HLGToReflectanceScale() noexcept;
void ValidateExposureStops(double stops);
double HLGToAppleLog(double encoded, double exposureStops = 0.0) noexcept;

// Optional creative rendering, independent of the Apple Log transfer function.
// Positive shadowStops lifts shadows; positive highlightStops lowers highlights.
struct ToneAdjustments {
    bool enabled = false;
    double shadowStops = 3.0;
    double highlightStops = 1.0;
    double saturation = 0.85;
};
void ValidateToneAdjustments(const ToneAdjustments& tone);
std::array<double, 3> AdjustSceneLinearBT2020(const std::array<double, 3>& rgb,
                                              const ToneAdjustments& tone) noexcept;
struct SignalStatistics {
    uint64_t samples = 0, appleFloorClipped = 0, aboveNominalWhite = 0;
    double inputMinimum = std::numeric_limits<double>::infinity();
    double inputMaximum = -std::numeric_limits<double>::infinity();
    double outputMinimum = std::numeric_limits<double>::infinity();
    double outputMaximum = -std::numeric_limits<double>::infinity();
};
void TransformHLGToAppleLog(std::span<float> planarRgb, double exposureStops = 0.0,
                            const ToneAdjustments& tone = {}, SignalStatistics* statistics = nullptr);
} // namespace logforge
