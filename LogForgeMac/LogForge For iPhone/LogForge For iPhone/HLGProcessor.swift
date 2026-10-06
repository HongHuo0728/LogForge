import Foundation
import simd

struct HLGProcessor {

    private static let a = 0.17883277
    private static let b = 1 - 4 * a
    private static let c = 0.5 - a * log(4 * a)

    // HLG inverse OETF
    static func inverseOETF(
        _ value: Double
    ) -> Double {

        ReferenceColor.inverseHLG(value)
    }

    static func inverseOETF(
        _ rgb: SIMD3<Double>
    ) -> SIMD3<Double> {

        SIMD3<Double>(
            inverseOETF(rgb.x),
            inverseOETF(rgb.y),
            inverseOETF(rgb.z)
        )
    }

    // BT.2408 reference white:
    //
    // HLG 75% signal corresponds to
    // diffuse reference white.
    static let referenceWhiteScale: Double = {
        let reference =
            inverseOETF(0.75)

        return 1.0 / reference
    }()

    static func toReferenceWhite(
        _ value: Double
    ) -> Double {

        value * referenceWhiteScale
    }

    static func process(
        _ rgb: SIMD3<Double>
    ) -> SIMD3<Double> {

        let linear =
            inverseOETF(rgb)

        return SIMD3<Double>(
            toReferenceWhite(linear.x),
            toReferenceWhite(linear.y),
            toReferenceWhite(linear.z)
        )
    }
}
