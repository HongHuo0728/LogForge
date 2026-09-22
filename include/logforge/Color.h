#pragma once
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

// Exposure convention: 75% HLG signal = a 90% diffuse reflecting reference.
// This is a declared workflow assumption, not a recovered camera exposure.
double HLGToReflectanceScale() noexcept;
double HLGToAppleLog(double encoded, double exposureStops = 0.0) noexcept;
void TransformHLGToAppleLog(std::span<float> planarRgb, double exposureStops = 0.0);
} // namespace logforge
