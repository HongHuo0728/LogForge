import Foundation
import AVFoundation
import CoreVideo
import CoreMedia

struct VideoProcessingResult {

    let outputURL: URL
    let frameCount: Int
    let duration: Double
    let diagnostics: VideoDiagnostics
}

// MARK: - Diagnostics

struct VideoDiagnostics {

    let nominalFrameRate: Double
    let detectedFrameRate: RationalFrameRate?
    let isLikelyCFR: Bool

    let colorPrimaries: String?
    let transferFunction: String?
    let yCbCrMatrix: String?

    let colorInterpretation: ColorInterpretation

    let warnings: [String]
    var processingBackend: ColorBackend = .cpu
    var encodingBackend: String = "unknown"
    var decodingBackend: String = "unknown"
}

struct RationalFrameRate: Equatable {

    let numerator: Int
    let denominator: Int

    var doubleValue: Double {
        Double(numerator) / Double(denominator)
    }

    var frameDuration: CMTime {
        CMTime(
            value: CMTimeValue(denominator),
            timescale: CMTimeScale(numerator)
        )
    }
}

enum ColorInterpretation {

    case bt2020HLG
    case missingColorInformation
    case unsupportedBT709
    case unsupportedOther
}

// MARK: - ProRes Quality

enum ProResQuality: String, CaseIterable, Identifiable {

    case proRes422HQ
    case proRes422

    var id: String {
        rawValue
    }

    var displayName: String {

        switch self {

        case .proRes422HQ:
            return "ProRes 422 HQ"

        case .proRes422:
            return "ProRes 422"
        }
    }

    var codec: AVVideoCodecType {

        switch self {

        case .proRes422HQ:
            return .proRes422HQ

        case .proRes422:
            return .proRes422
        }
    }
}

// MARK: - Video Processor
 
final class VideoProcessor {

    typealias ProgressHandler = (
        _ progress: Double,
        _ processedFrames: Int
    ) -> Void

    // MARK: Errors

    enum ProcessorError: LocalizedError {

        case noVideoTrack
        case invalidVideoSize
        case invalidFrameRate
        case invalidDuration

        case cannotCreateReader
        case cannotCreateReaderOutput
        case readerFailed(String)

        case cannotCreateWriter
        case cannotCreateWriterInput
        case writerSettingsUnsupported
        case cannotStartWriter
        case writerFailed(String)

        case missingPixelBuffer
        case pixelFormatMismatch

        case cannotCreatePixelBuffer
        case pixelBufferPoolUnavailable
        case pixelBufferPoolAllocationFailed

        case pixelBufferSizeMismatch
        case pixelBufferFormatMismatch

        case invalidPresentationTime
        case nonMonotonicPresentationTime

        case unsupportedInputColor(String)

        var errorDescription: String? {

            switch self {

            case .noVideoTrack:
                return "No video track found."

            case .invalidVideoSize:
                return "Invalid video dimensions."

            case .invalidFrameRate:
                return "Invalid video frame rate."

            case .invalidDuration:
                return "Invalid video duration."

            case .cannotCreateReader:
                return "Cannot create AVAssetReader."

            case .cannotCreateReaderOutput:
                return "Cannot create video reader output."

            case .readerFailed(let message):
                return "AVAssetReader failed: \(message)"

            case .cannotCreateWriter:
                return "Cannot create AVAssetWriter."

            case .cannotCreateWriterInput:
                return "Cannot create AVAssetWriterInput."

            case .writerSettingsUnsupported:
                return "The selected Apple ProRes encoder configuration is not supported on this device."

            case .cannotStartWriter:
                return "Cannot start AVAssetWriter."

            case .writerFailed(let message):
                return "AVAssetWriter failed: \(message)"

            case .missingPixelBuffer:
                return "Video sample does not contain a pixel buffer."

            case .pixelFormatMismatch:
                return "Input is not 10-bit 4:2:2."

            case .cannotCreatePixelBuffer:
                return "Cannot create 10-bit 4:2:2 output pixel buffer."

            case .pixelBufferPoolUnavailable:
                return "AVAssetWriter pixel buffer pool is unavailable."

            case .pixelBufferPoolAllocationFailed:
                return "Cannot allocate pixel buffer from AVAssetWriter pool."

            case .pixelBufferSizeMismatch:
                return "Output pixel buffer dimensions do not match the writer."

            case .pixelBufferFormatMismatch:
                return "Output pixel buffer format does not match the writer."

            case .invalidPresentationTime:
                return "Invalid video presentation timestamp."

            case .nonMonotonicPresentationTime:
                return "Video presentation timestamps are not monotonic."

            case .unsupportedInputColor(let message):
                return "Unsupported input color interpretation: \(message)"
            }
        }
    }

    // MARK: GPU

    private lazy var metalProcessor: MetalColorProcessor? = try? MetalColorProcessor()

    private(set) var usingMetal: Bool

    init() {

        // The active pipeline qualifies a backend for each job. Constructing this
        // wrapper must not initialize Metal for an explicitly CPU-only job.
        self.usingMetal = false
    }

    func process(url: URL, quality: ProResQuality = .proRes422HQ,
                 options: ConversionOptions, progress: @escaping ProgressHandler) async throws -> VideoProcessingResult {
        let result = try await NativePipeline(options: options, quality: quality, progress: progress).process(url: url)
        usingMetal = result.diagnostics.processingBackend == .metal
        return result
    }

    // MARK: Main Processing

    func process(
        url: URL,
        quality: ProResQuality = .proRes422HQ,
        progress: @escaping ProgressHandler
    ) async throws -> VideoProcessingResult {
        try await process(url: url, quality: quality, options: ConversionOptions(), progress: progress)
    }

    // MARK: - Rational Frame Rate

    static func detectRationalFrameRate(
        _ rate: Double
    ) -> RationalFrameRate? {

        let candidates: [RationalFrameRate] = [

            RationalFrameRate(
                numerator: 24000,
                denominator: 1001
            ),

            RationalFrameRate(
                numerator: 30000,
                denominator: 1001
            ),

            RationalFrameRate(
                numerator: 60000,
                denominator: 1001
            ),

            RationalFrameRate(
                numerator: 120000,
                denominator: 1001
            ),

            RationalFrameRate(
                numerator: 24,
                denominator: 1
            ),

            RationalFrameRate(
                numerator: 25,
                denominator: 1
            ),

            RationalFrameRate(
                numerator: 30,
                denominator: 1
            ),

            RationalFrameRate(
                numerator: 50,
                denominator: 1
            ),

            RationalFrameRate(
                numerator: 60,
                denominator: 1
            ),

            RationalFrameRate(
                numerator: 120,
                denominator: 1
            )
        ]

        var best: RationalFrameRate?
        var bestError =
            Double.greatestFiniteMagnitude

        for candidate in candidates {

            let error =
                abs(
                    candidate.doubleValue -
                    rate
                )

            if error < bestError {

                bestError = error
                best = candidate
            }
        }

        guard let best else {
            return nil
        }

        let relativeError =
            abs(
                best.doubleValue -
                rate
            )
            /
            max(
                rate,
                0.000001
            )

        if relativeError < 0.0005 {
            return best
        }

        return nil
    }

    // MARK: - Color Information

    private struct ColorInfo {

        let colorPrimaries: String?
        let transferFunction: String?
        let yCbCrMatrix: String?

        let interpretation:
            ColorInterpretation

        let warnings:
            [String]
    }

    private func inspectColorInformation(
        track: AVAssetTrack
    ) async -> ColorInfo {

        var primaries: String?
        var transfer: String?
        var matrix: String?

        var warnings: [String] = []

        do {

            let descriptions =
                try await track.load(
                    .formatDescriptions
                )

            for description in descriptions {

                guard let extensions =
                        CMFormatDescriptionGetExtensions(
                            description
                        ) as? [String: Any] else {

                    continue
                }

                if let value =
                    extensions[
                        String(
                            describing:
                                kCMFormatDescriptionExtension_ColorPrimaries
                        )
                    ] {

                    primaries =
                        normalizedMetadataValue(
                            value
                        )
                }

                if let value =
                    extensions[
                        String(
                            describing:
                                kCMFormatDescriptionExtension_TransferFunction
                        )
                    ] {

                    transfer =
                        normalizedMetadataValue(
                            value
                        )
                }

                if let value =
                    extensions[
                        String(
                            describing:
                                kCMFormatDescriptionExtension_YCbCrMatrix
                        )
                    ] {

                    matrix =
                        normalizedMetadataValue(
                            value
                        )
                }

                if primaries != nil,
                   transfer != nil,
                   matrix != nil {

                    break
                }
            }

        } catch {

            warnings.append(
                "Unable to read complete input color metadata."
            )
        }

        let bt2020Primaries =
            metadataString(
                kCVImageBufferColorPrimaries_ITU_R_2020
            )

        let bt2020Matrix =
            metadataString(
                kCVImageBufferYCbCrMatrix_ITU_R_2020
            )

        let hlgTransfer =
            metadataString(
                kCVImageBufferTransferFunction_ITU_R_2100_HLG
            )

        let bt709Primaries =
            metadataString(
                kCVImageBufferColorPrimaries_ITU_R_709_2
            )

        let bt709Matrix =
            metadataString(
                kCVImageBufferYCbCrMatrix_ITU_R_709_2
            )

        let isBT2020 =
            primaries == bt2020Primaries

        let isHLG =
            transfer == hlgTransfer

        let isBT709 =
            primaries == bt709Primaries
            ||
            matrix == bt709Matrix

        // Explicit BT.2020 + HLG.

        if isBT2020 && isHLG && (matrix == nil || matrix == bt2020Matrix) {

            if matrix == nil {

                warnings.append(
                    "BT.2020 HLG input has no explicit " +
                    "YCbCr matrix. BT.2020 matrix " +
                    "interpretation will be used."
                )
            }

            return ColorInfo(
                colorPrimaries:
                    primaries,
                transferFunction:
                    transfer,
                yCbCrMatrix:
                    matrix,
                interpretation:
                    .bt2020HLG,
                warnings:
                    warnings
            )
        }

        // Explicit BT.709 conflict.

        if isBT709 {

            warnings.append(
                "Input is explicitly tagged as BT.709."
            )

            return ColorInfo(
                colorPrimaries:
                    primaries,
                transferFunction:
                    transfer,
                yCbCrMatrix:
                    matrix,
                interpretation:
                    .unsupportedBT709,
                warnings:
                    warnings
            )
        }

        // No metadata at all.

        if primaries == nil,
           transfer == nil,
           matrix == nil {

            warnings.append(
                "Input contains no explicit color metadata."
            )

            return ColorInfo(
                colorPrimaries:
                    nil,
                transferFunction:
                    nil,
                yCbCrMatrix:
                    nil,
                interpretation:
                    .missingColorInformation,
                warnings:
                    warnings
            )
        }

        warnings.append(
            "Input color metadata does not identify " +
            "a supported BT.2020 HLG signal."
        )

        return ColorInfo(
            colorPrimaries:
                primaries,
            transferFunction:
                transfer,
            yCbCrMatrix:
                matrix,
            interpretation:
                .unsupportedOther,
            warnings:
                warnings
        )
    }

    // MARK: - Metadata Helpers

    private func normalizedMetadataValue(
        _ value: Any
    ) -> String {

        if let string = value as? String {
            return string
        }

        if let nsString = value as? NSString {
            return nsString as String
        }

        return String(
            describing: value
        )
    }

    private func metadataString(
        _ value: CFString
    ) -> String {

        return String(
            describing: value
        )
    }

    // MARK: - Writer Settings

    private func makeWriterSettings(
        width: Int,
        height: Int,
        quality: ProResQuality
    ) -> [String: Any] {

        return [

            AVVideoCodecKey:
                quality.codec,

            AVVideoWidthKey:
                width,

            AVVideoHeightKey:
                height
        ]
    }

    // MARK: - Writer Error

    private func detailedWriterError(
        _ writer: AVAssetWriter
    ) -> String {

        var messages: [String] = []

        if let error = writer.error {

            let nsError =
                error as NSError

            messages.append(
                "Writer: \(error.localizedDescription)"
            )

            messages.append(
                "Writer domain: \(nsError.domain)"
            )

            messages.append(
                "Writer code: \(nsError.code)"
            )

            if let underlying =
                nsError.userInfo[
                    NSUnderlyingErrorKey
                ] as? NSError {

                messages.append(
                    "Underlying: " +
                    "\(underlying.localizedDescription)"
                )

                messages.append(
                    "Underlying domain: " +
                    "\(underlying.domain)"
                )

                messages.append(
                    "Underlying code: " +
                    "\(underlying.code)"
                )
            }

            if let reason =
                nsError.userInfo[
                    NSLocalizedFailureReasonErrorKey
                ] as? String {

                messages.append(
                    "Failure reason: \(reason)"
                )
            }

            if let recovery =
                nsError.userInfo[
                    NSLocalizedRecoverySuggestionErrorKey
                ] as? String {

                messages.append(
                    "Recovery: \(recovery)"
                )
            }
        }

        if messages.isEmpty {

            messages.append(
                "AVAssetWriter failed to encode the video."
            )
        }

        return messages.joined(
            separator: " | "
        )
    }

    // MARK: - Pixel Buffer Metadata

    private func clearColorAttachments(
        _ pixelBuffer: CVPixelBuffer
    ) {

        CVBufferRemoveAttachment(
            pixelBuffer,
            kCVImageBufferColorPrimariesKey
        )

        CVBufferRemoveAttachment(
            pixelBuffer,
            kCVImageBufferTransferFunctionKey
        )

        CVBufferRemoveAttachment(
            pixelBuffer,
            kCVImageBufferYCbCrMatrixKey
        )

        CVBufferRemoveAttachment(
            pixelBuffer,
            kCVImageBufferLogTransferFunctionKey
        )
    }

    private func tagAsAppleLog(
        _ pixelBuffer: CVPixelBuffer
    ) {

        CVBufferRemoveAttachment(
            pixelBuffer,
            kCVImageBufferTransferFunctionKey
        )

        CVBufferSetAttachment(
            pixelBuffer,
            kCVImageBufferColorPrimariesKey,
            kCVImageBufferColorPrimaries_ITU_R_2020,
            .shouldPropagate
        )

        CVBufferSetAttachment(
            pixelBuffer,
            kCVImageBufferYCbCrMatrixKey,
            kCVImageBufferYCbCrMatrix_ITU_R_2020,
            .shouldPropagate
        )

        CVBufferSetAttachment(
            pixelBuffer,
            kCVImageBufferLogTransferFunctionKey,
            kCVImageBufferLogTransferFunction_AppleLog,
            .shouldPropagate
        )
    }

    // MARK: - Output URL

    private func makeOutputURL(
        sourceURL: URL,
        quality: ProResQuality
    ) -> URL {

        let directory =
            FileManager.default.temporaryDirectory

        let sourceName =
            sourceURL
                .deletingPathExtension()
                .lastPathComponent

        let suffix: String

        switch quality {

        case .proRes422HQ:
            suffix =
                "_AppleLog_ProRes422HQ"

        case .proRes422:
            suffix =
                "_AppleLog_ProRes422"
        }

        return directory.appendingPathComponent(
            "\(sourceName)\(suffix).mov"
        )
    }

    // MARK: - Finish Writer

    private func finishWriter(
        _ writer: AVAssetWriter
    ) async {

        await withCheckedContinuation {
            (
                continuation:
                    CheckedContinuation<
                        Void,
                        Never
                    >
            ) in

            writer.finishWriting {

                continuation.resume()
            }
        }
    }
}
