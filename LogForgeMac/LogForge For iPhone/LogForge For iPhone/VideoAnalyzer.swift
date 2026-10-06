import AVFoundation
import CoreMedia
import CoreVideo

final class VideoAnalyzer {

    func analyze(
        url: URL
    ) async throws -> VideoInfo {

        let asset =
            AVURLAsset(url: url)

        let tracks =
            try await asset.loadTracks(
                withMediaType: .video
            )

        guard let track = tracks.first else {

            throw NSError(
                domain: "LogForge",
                code: 1,
                userInfo: [
                    NSLocalizedDescriptionKey:
                        L10n.text("error.tracks")
                ]
            )
        }

        let formatDescriptions =
            try await track.load(
                .formatDescriptions
            )

        guard let formatDescription =
                formatDescriptions.first else {

            throw NSError(
                domain: "LogForge",
                code: 2,
                userInfo: [
                    NSLocalizedDescriptionKey:
                        L10n.text("error.sample")
                ]
            )
        }

        let dimensions =
            CMVideoFormatDescriptionGetDimensions(
                formatDescription
            )

        let mediaSubType =
            CMFormatDescriptionGetMediaSubType(
                formatDescription
            )

        let codec =
            codecName(
                mediaSubType
            )

        let frameRate =
            try await track.load(
                .nominalFrameRate
            )

        let duration =
            try await asset.load(
                .duration
            )

        let colorPrimaries =
            extensionString(
                from: formatDescription,
                key:
                    kCMFormatDescriptionExtension_ColorPrimaries
            )

        let transferFunction =
            extensionString(
                from: formatDescription,
                key:
                    kCMFormatDescriptionExtension_TransferFunction
            )

        let matrix =
            extensionString(
                from: formatDescription,
                key:
                    kCMFormatDescriptionExtension_YCbCrMatrix
            )

        let bits =
            bitDepth(
                formatDescription
            )

        let isProRes =
            isProResCodec(
                mediaSubType
            )

        let isHLG =
            transferFunction ==
            String(
                describing:
                    kCMFormatDescriptionTransferFunction_ITU_R_2100_HLG
            )

        let isBT2020 =
            colorPrimaries ==
            String(
                describing:
                    kCMFormatDescriptionColorPrimaries_ITU_R_2020
            )

        let pixelFormat =
            pixelFormatName(
                mediaSubType,
                bits: bits
            )

        return VideoInfo(

            fileName:
                url.lastPathComponent,

            width:
                Int(dimensions.width),

            height:
                Int(dimensions.height),

            frameRate:
                Double(frameRate),

            duration:
                duration.seconds,

            codec:
                codec,

            pixelFormat:
                pixelFormat,

            colorPrimaries:
                colorPrimaries ?? "Unknown",

            transferFunction:
                transferFunction ?? "Unknown",

            matrix:
                matrix ?? "Unknown",

            isProRes:
                isProRes,

            isHLG:
                isHLG,

            isBT2020:
                isBT2020,
            inputAccepted: (try? await InputContract.inspect(asset)) != nil
        )
    }

    // MARK: - Pixel Format

    private func pixelFormatName(
        _ codec: FourCharCode,
        bits: Int
    ) -> String {

        switch codec {

        case kCMVideoCodecType_AppleProRes422,
             kCMVideoCodecType_AppleProRes422HQ,
             kCMVideoCodecType_AppleProRes422LT,
             kCMVideoCodecType_AppleProRes422Proxy:

            if bits >= 10 {
                return "10-bit 4:2:2"
            }

            return "\(bits)-bit 4:2:2"

        default:
            return "Unknown"
        }
    }

    private func bitDepth(
        _ formatDescription:
            CMFormatDescription
    ) -> Int {

        if let value =
            CMFormatDescriptionGetExtension(
                formatDescription,
                extensionKey:
                    kCMFormatDescriptionExtension_BitsPerComponent
            ) {

            if let number =
                value as? NSNumber {

                return number.intValue
            }

            return Int(
                String(
                    describing: value
                )
            ) ?? 0
        }

        let codec =
            CMFormatDescriptionGetMediaSubType(
                formatDescription
            )

        switch codec {

        case kCMVideoCodecType_AppleProRes422,
             kCMVideoCodecType_AppleProRes422HQ,
             kCMVideoCodecType_AppleProRes422LT,
             kCMVideoCodecType_AppleProRes422Proxy:

            return 10

        default:

            return 0
        }
    }

    // MARK: - Codec

    private func codecName(
        _ codec: FourCharCode
    ) -> String {

        switch codec {

        case kCMVideoCodecType_AppleProRes422:
            return "Apple ProRes 422"

        case kCMVideoCodecType_AppleProRes422HQ:
            return "Apple ProRes 422 HQ"

        case kCMVideoCodecType_AppleProRes422LT:
            return "Apple ProRes 422 LT"

        case kCMVideoCodecType_AppleProRes422Proxy:
            return "Apple ProRes 422 Proxy"

        case kCMVideoCodecType_AppleProRes4444:
            return "Apple ProRes 4444"

        default:
            return "Unknown"
        }
    }

    private func isProResCodec(
        _ codec: FourCharCode
    ) -> Bool {

        switch codec {

        case kCMVideoCodecType_AppleProRes422,
             kCMVideoCodecType_AppleProRes422HQ,
             kCMVideoCodecType_AppleProRes422LT,
             kCMVideoCodecType_AppleProRes422Proxy,
             kCMVideoCodecType_AppleProRes4444:

            return true

        default:

            return false
        }
    }

    private func extensionString(
        from formatDescription:
            CMFormatDescription,
        key: CFString
    ) -> String? {

        guard let value =
            CMFormatDescriptionGetExtension(
                formatDescription,
                extensionKey: key
            )
        else {
            return nil
        }

        return String(
            describing: value
        )
    }
}
