import Foundation
import CoreVideo

struct V210Converter {

    static let pixelFormat: OSType =
        kCVPixelFormatType_422YpCbCr10

    // MARK: - Constants
    //
    // These values intentionally match the Metal shader exactly.

    private static let HLG_A: Float = 0.17883277
    private static let HLG_B: Float = 0.28466892
    private static let HLG_C: Float = 0.55991073

    private static let HLG_REFERENCE_WHITE: Float = 0.26496256

    private static let APPLE_LOG_R0: Float = -0.05641088
    private static let APPLE_LOG_RT: Float = 0.01000000
    private static let APPLE_LOG_SIGMA: Float = 47.28711236
    private static let APPLE_LOG_BETA: Float = 0.00964052
    private static let APPLE_LOG_GAMMA: Float = 0.08550479
    private static let APPLE_LOG_DELTA: Float = 0.69336945

    // MARK: - Errors

    enum V210Error: LocalizedError {

        case invalidPixelBuffer
        case unsupportedPixelFormat
        case invalidDimensions
        case sizeMismatch
        case strideTooSmall
        case lockFailed
        case baseAddressUnavailable

        var errorDescription: String? {

            switch self {

            case .invalidPixelBuffer:
                return L10n.text("error.sample")

            case .unsupportedPixelFormat:
                return L10n.text("error.input")

            case .invalidDimensions:
                return L10n.text("error.size")

            case .sizeMismatch:
                return L10n.text("error.size")

            case .strideTooSmall:
                return L10n.text("error.memory")

            case .lockFailed:
                return L10n.text("error.memory")

            case .baseAddressUnavailable:
                return L10n.text("error.memory")
            }
        }
    }

    // MARK: - Basic Information

    static func isSupported(
        _ pixelBuffer: CVPixelBuffer
    ) -> Bool {

        CVPixelBufferGetPixelFormatType(pixelBuffer)
            == pixelFormat
    }

    static func width(
        _ pixelBuffer: CVPixelBuffer
    ) -> Int {

        CVPixelBufferGetWidth(pixelBuffer)
    }

    static func height(
        _ pixelBuffer: CVPixelBuffer
    ) -> Int {

        CVPixelBufferGetHeight(pixelBuffer)
    }

    static func bytesPerRow(
        _ pixelBuffer: CVPixelBuffer
    ) -> Int {

        CVPixelBufferGetBytesPerRow(pixelBuffer)
    }

    static func groupsPerRow(
        width: Int
    ) -> Int {

        (width + 5) / 6
    }

    static func minimumBytesPerRow(
        width: Int
    ) -> Int {

        groupsPerRow(width: width) * 16
    }

    static func validate(
        _ pixelBuffer: CVPixelBuffer
    ) -> Bool {

        guard isSupported(pixelBuffer) else {
            return false
        }

        let width =
            CVPixelBufferGetWidth(pixelBuffer)

        let height =
            CVPixelBufferGetHeight(pixelBuffer)

        guard width > 0,
              height > 0 else {
            return false
        }

        return CVPixelBufferGetBytesPerRow(pixelBuffer)
            >= minimumBytesPerRow(width: width)
    }

    // MARK: - Helpers

    @inline(__always)
    private static func clamp01(
        _ value: Float
    ) -> Float {

        min(
            max(value, 0.0),
            1.0
        )
    }

    @inline(__always)
    private static func clamp10(
        _ value: Float
    ) -> UInt16 {

        let result =
            min(
                max(
                    value.rounded(),
                    0.0
                ),
                1023.0
            )

        return UInt16(result)
    }

    // MARK: - HLG

    @inline(__always)
    private static func hlgInverseOETF(
        _ input: Float
    ) -> Float {

        let E =
            clamp01(input)

        if E <= 0.5 {

            return (
                E * E
            ) / 3.0
        }

        return (
            exp(
                (E - HLG_C) / HLG_A
            )
            + HLG_B
        ) / 12.0
    }

    @inline(__always)
    private static func hlgToSceneLinear(
        _ hlgRGB: SIMD3<Float>
    ) -> SIMD3<Float> {

        SIMD3<Float>(
            hlgInverseOETF(hlgRGB.x),
            hlgInverseOETF(hlgRGB.y),
            hlgInverseOETF(hlgRGB.z)
        )
    }

    @inline(__always)
    private static func normalizeHLGReferenceWhite(
        _ sceneLinear: SIMD3<Float>
    ) -> SIMD3<Float> {

        sceneLinear /
            HLG_REFERENCE_WHITE
    }

    // MARK: - Apple Log

    @inline(__always)
    private static func appleLogEncode(
        _ R: Float
    ) -> Float {

        if R >= APPLE_LOG_RT {

            return
                APPLE_LOG_GAMMA *
                log2(
                    R + APPLE_LOG_BETA
                )
                +
                APPLE_LOG_DELTA
        }

        if R >= APPLE_LOG_R0 {

            let d =
                R - APPLE_LOG_R0

            return
                APPLE_LOG_SIGMA *
                d *
                d
        }

        return 0.0
    }

    @inline(__always)
    private static func appleLogEncodeRGB(
        _ linearRGB: SIMD3<Float>
    ) -> SIMD3<Float> {

        SIMD3<Float>(
            appleLogEncode(linearRGB.x),
            appleLogEncode(linearRGB.y),
            appleLogEncode(linearRGB.z)
        )
    }

    // MARK: - BT.2020

    @inline(__always)
    private static func bt2020YCbCrToRGB(
        Y: Float,
        Cb: Float,
        Cr: Float
    ) -> SIMD3<Float> {

        let R =
            Y +
            1.4746 * Cr

        let G =
            Y -
            0.16455 * Cb -
            0.57135 * Cr

        let B =
            Y +
            1.8814 * Cb

        return SIMD3(
            R,
            G,
            B
        )
    }

    @inline(__always)
    private static func bt2020RGBToYCbCr(
        _ rgb: SIMD3<Float>
    ) -> SIMD3<Float> {

        let R = rgb.x
        let G = rgb.y
        let B = rgb.z

        let Y =
            0.2627 * R +
            0.6780 * G +
            0.0593 * B

        let Cb =
            (B - Y) /
            1.8814

        let Cr =
            (R - Y) /
            1.4746

        return SIMD3(
            Y,
            Cb,
            Cr
        )
    }

    // MARK: - 10-bit Range

    @inline(__always)
    private static func decodeY10(
        _ value: UInt32
    ) -> Float {

        (
            Float(value) -
            64.0
        ) / 876.0
    }

    @inline(__always)
    private static func decodeC10(
        _ value: UInt32
    ) -> Float {

        (
            Float(value) -
            512.0
        ) / 896.0
    }

    @inline(__always)
    private static func encodeY10(
        _ value: Float
    ) -> UInt16 {

        clamp10(
            value * 876.0 + 64.0
        )
    }

    @inline(__always)
    private static func encodeC10(
        _ value: Float
    ) -> UInt16 {

        clamp10(
            value * 896.0 + 512.0
        )
    }

    // MARK: - Complete Pixel Conversion

    @inline(__always)
    private static func processPixel(
        Y10: UInt32,
        Cb10: UInt32,
        Cr10: UInt32
    ) -> SIMD3<Float> {

        let Y =
            decodeY10(Y10)

        let Cb =
            decodeC10(Cb10)

        let Cr =
            decodeC10(Cr10)

        let hlgRGB =
            bt2020YCbCrToRGB(
                Y: Y,
                Cb: Cb,
                Cr: Cr
            )

        let sceneLinear =
            hlgToSceneLinear(
                hlgRGB
            )

        let normalizedLinear =
            normalizeHLGReferenceWhite(
                sceneLinear
            )

        let appleLogRGB =
            appleLogEncodeRGB(
                normalizedLinear
            )

        return bt2020RGBToYCbCr(
            appleLogRGB
        )
    }

    // MARK: - V210 Read

    @inline(__always)
    private static func readUInt32(
        _ pointer: UnsafeRawPointer,
        offset: Int
    ) -> UInt32 {

        pointer.loadUnaligned(
            fromByteOffset: offset,
            as: UInt32.self
        )
    }

    // MARK: - V210 Write

    @inline(__always)
    private static func writeUInt32(
        _ pointer: UnsafeMutableRawPointer,
        offset: Int,
        value: UInt32
    ) {

        pointer.storeBytes(
            of: value,
            toByteOffset: offset,
            as: UInt32.self
        )
    }

    // MARK: - CPU Conversion

    /// Complete CPU implementation matching logForgeV210Kernel.
    static func convert(
        input: CVPixelBuffer,
        output: CVPixelBuffer
    ) throws {
        try convert(input: input, output: output, options: ConversionOptions(), fullRange: false, centeredChroma: false)
    }
}
