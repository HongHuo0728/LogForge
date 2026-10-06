import Foundation
import simd

struct AppleLogProcessor {

    // Apple Log Profile
    //
    // R0 = -0.05641088
    // Rt =  0.01
    // c  = 47.28711236
    // β  = 0.00964052
    // γ  = 0.08550479
    // δ  = 0.69336945

    private static let r0 = -0.05641088
    private static let rt = 0.01
    private static let c = 47.28711236
    private static let beta = 0.00964052
    private static let gamma = 0.08550479
    private static let delta = 0.69336945

    private static let pt =
        c * pow(rt - r0, 2.0)

    // Scene-linear → Apple Log
    static func encode(_ value: Double) -> Double {

        if value < r0 {
            return 0.0
        }

        if value < rt {
            return c * pow(value - r0, 2.0)
        }

        return gamma *
            log2(value + beta) +
            delta
    }

    static func encode(
        _ rgb: SIMD3<Double>
    ) -> SIMD3<Double> {

        SIMD3<Double>(
            encode(rgb.x),
            encode(rgb.y),
            encode(rgb.z)
        )
    }

    // Apple Log → Scene-linear
    static func decode(_ value: Double) -> Double {

        if value >= pt {
            return pow(
                2.0,
                (value - delta) / gamma
            ) - beta
        }

        if value >= 0.0 {
            return sqrt(value / c) + r0
        }

        return r0
    }

    static func decode(
        _ rgb: SIMD3<Double>
    ) -> SIMD3<Double> {

        SIMD3<Double>(
            decode(rgb.x),
            decode(rgb.y),
            decode(rgb.z)
        )
    }
}
