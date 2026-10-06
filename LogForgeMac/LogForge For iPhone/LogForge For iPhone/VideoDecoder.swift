import Foundation
import AVFoundation
import CoreMedia
import CoreVideo

struct DecodedVideoFrame {
    let pixelBuffer: CVPixelBuffer
    let presentationTime: CMTime
}

final class VideoDecoder {

    func decodeFirstFrame(
        from url: URL
    ) async throws -> DecodedVideoFrame {

        let asset = AVURLAsset(url: url)

        let tracks =
            try await asset.loadTracks(withMediaType: .video)

        guard let track = tracks.first else {
            throw DecoderError.noVideoTrack
        }

        // 先不强制转换成 10-bit 4:2:2。
        // 直接让 AVAssetReader 返回原始/可用的解码格式。
        let reader = try AVAssetReader(asset: asset)

        let output =
            AVAssetReaderTrackOutput(
                track: track,
                outputSettings: [kCVPixelBufferPixelFormatTypeKey as String: kCVPixelFormatType_422YpCbCr10]
            )

        output.alwaysCopiesSampleData = false

        guard reader.canAdd(output) else {
            throw DecoderError.cannotAddOutput
        }

        reader.add(output)
        defer { reader.cancelReading() }

        guard reader.startReading() else {
            throw reader.error
                ?? DecoderError.cannotStartReader
        }

        while let sampleBuffer =
                output.copyNextSampleBuffer() {

            guard let pixelBuffer =
                    CMSampleBufferGetImageBuffer(sampleBuffer)
            else {
                continue
            }

            let pts =
                CMSampleBufferGetPresentationTimeStamp(
                    sampleBuffer
                )

            return DecodedVideoFrame(
                pixelBuffer: pixelBuffer,
                presentationTime: pts
            )
        }

        throw reader.error
            ?? DecoderError.noFrame
    }

    enum DecoderError: LocalizedError {

        case noVideoTrack
        case cannotAddOutput
        case cannotStartReader
        case noFrame

        var errorDescription: String? {

            switch self {

            case .noVideoTrack:
                return "No video track found."

            case .cannotAddOutput:
                return "Unable to add video output."

            case .cannotStartReader:
                return "Unable to start AVAssetReader."

            case .noFrame:
                return "No decoded video frame was returned."
            }
        }
    }
}
