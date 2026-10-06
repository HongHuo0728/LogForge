import Foundation
import simd

struct ColorConverter {

    // MARK: - BT.2020 Y'CbCr → RGB

    static func bt2020YCbCrToRGB(
        ycbcr: SIMD3<Double>
    ) -> SIMD3<Double> {

        let y = ycbcr.x
        let cb = ycbcr.y
        let cr = ycbcr.z

        let r =
            y
            + 1.4746 * cr

        let g =
            y
            - 0.16455312684366 * cb
            - 0.57135312684366 * cr

        let b =
            y
            + 1.8814 * cb

        return SIMD3(
            r,
            g,
            b
        )
    }

    // MARK: - RGB → BT.2020 Y'CbCr

    static func bt2020RGBToYCbCr(
        _ rgb: SIMD3<Double>
    ) -> SIMD3<Double> {

        let y =
            0.2627 * rgb.x
            + 0.6780 * rgb.y
            + 0.0593 * rgb.z

        let cb =
            -0.139630 * rgb.x
            - 0.360370 * rgb.y
            + 0.500000 * rgb.z

        let cr =
            0.500000 * rgb.x
            - 0.459786 * rgb.y
            - 0.040214 * rgb.z

        return SIMD3(
            y,
            cb,
            cr
        )
    }
}
