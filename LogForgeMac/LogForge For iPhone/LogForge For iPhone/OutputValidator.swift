import Foundation
import AVFoundation
import CoreMedia
import CoreVideo

struct OutputValidationResult {

    let codec: String
    let pixelFormat: String

    let isProRes: Bool
    let is10Bit422: Bool

    let isBT2020: Bool
    let isAppleLog: Bool

    let frameCount: Int
    let expectedFrameCount: Int

    let duration: Double
    let expectedDuration: Double

    let ptsContinuous: Bool

    let ffmpegAvailable: Bool
    let timingIssues: [String]
    let decodingBackend: String

    var passed: Bool {
        isProRes &&
        is10Bit422 &&
        isBT2020 &&
        isAppleLog &&
        frameCount == expectedFrameCount &&
        ptsContinuous && timingIssues.isEmpty &&
        frameCount > 0 && duration.isFinite && expectedDuration > 0 &&
        abs(duration - expectedDuration) <= 0.002
    }
}

struct StoredFrameInspection {
    let frameCount: Int
    let duration: Double
    let ptsContinuous: Bool
    let timestamps: [CMTime]
    let issues: [String]
    var decodingBackend = "unverified"
}

struct OutputFrameTiming {
    let expectedPTS: [CMTime]
    let expectedDurations: [CMTime]
    private(set) var timestamps: [CMTime] = []
    private var end = CMTime.invalid
    private var issues: [String] = []
    init(expectedPTS: [CMTime], expectedDurations: [CMTime]) {
        self.expectedPTS = expectedPTS; self.expectedDurations = expectedDurations
    }
    static func numeric(_ time: CMTime) -> Bool {
        time.isNumeric && time.timescale > 0 && time.epoch == 0 && time.seconds.isFinite
    }
    static func matches(_ actual: CMTime, _ expected: CMTime) -> Bool {
        guard numeric(actual), numeric(expected) else { return false }
        let delta = actual-expected
        return numeric(delta) && abs(delta.seconds) <= 0.000002
    }
    mutating func append(pts: CMTime, duration: CMTime) {
        let frame = timestamps.count
        func describe(_ time: CMTime) -> String { NativeSamples.describe(time) }
        if !Self.numeric(pts) || pts < .zero {
            issue("stored frame=\(frame): invalid PTS \(describe(pts))")
        }
        if let previous = timestamps.last, !Self.numeric(previous) || !Self.numeric(pts) || pts <= previous {
            issue("stored frame=\(frame): non-increasing PTS \(describe(pts)), previous=\(describe(previous))")
        }
        if !Self.numeric(duration) || duration <= .zero {
            issue("stored frame=\(frame): invalid duration \(describe(duration))")
        }
        if !expectedPTS.isEmpty, frame >= expectedPTS.count || !Self.matches(pts,expectedPTS[frame]) {
            issue("stored frame=\(frame): PTS mismatch actual=\(describe(pts)), expected=\(frame < expectedPTS.count ? describe(expectedPTS[frame]) : "none")")
        }
        if !expectedDurations.isEmpty, frame >= expectedDurations.count || !Self.matches(duration,expectedDurations[frame]) {
            issue("stored frame=\(frame): duration mismatch actual=\(describe(duration)), expected=\(frame < expectedDurations.count ? describe(expectedDurations[frame]) : "none")")
        }
        timestamps.append(pts)
        end = pts+duration
    }
    private mutating func issue(_ detail: String) { if issues.count < 8 { issues.append(detail) } }
    func result() -> StoredFrameInspection {
        var detail = issues
        if !expectedPTS.isEmpty && expectedPTS.count != timestamps.count { detail.append("stored PTS count=\(timestamps.count), expected=\(expectedPTS.count)") }
        if !expectedDurations.isEmpty && expectedDurations.count != timestamps.count { detail.append("stored duration count=\(timestamps.count), expected=\(expectedDurations.count)") }
        let elapsed = timestamps.first.map { end-$0 } ?? .invalid
        let duration = Self.numeric(elapsed) && elapsed > .zero ? elapsed.seconds : 0
        if duration == 0 { detail.append("invalid stored duration: end=\(NativeSamples.describe(end)), first=\(timestamps.first.map { NativeSamples.describe($0) } ?? "none")") }
        return StoredFrameInspection(frameCount:timestamps.count,duration:duration,ptsContinuous:detail.isEmpty,timestamps:timestamps,issues:detail)
    }
}

final class OutputValidator {

    func validate(
        outputURL: URL,
        expectedFrameCount: Int,
        expectedDuration: Double,
        expectedPTS: [CMTime] = [], expectedDurations: [CMTime] = [],
        expectedCodec: AVVideoCodecType? = nil, forceSoftwareDecoding: Bool = false
    ) async throws -> OutputValidationResult {

        try AppleLogMOV.process(url: outputURL, patch: false)
        let asset = AVURLAsset(url: outputURL)

        let tracks = try await asset.loadTracks(
            withMediaType: .video
        )

        guard tracks.count == 1, let track = tracks.first else {
            throw ValidationError.noVideoTrack
        }

        let descriptions =
            try await track.load(.formatDescriptions)

        guard descriptions.count == 1, let formatDescription = descriptions.first else {
            throw ValidationError.noFormatDescription
        }

        let codecType =
            CMFormatDescriptionGetMediaSubType(
                formatDescription
            )

        let codec =
            codecName(codecType)

        let matchesCodec = expectedCodec == nil ||
            (expectedCodec == .proRes422HQ && codecType == kCMVideoCodecType_AppleProRes422HQ) ||
            (expectedCodec == .proRes422 && codecType == kCMVideoCodecType_AppleProRes422)
        let isProRes = isProResCodec(codecType) && matchesCodec

        let dimensions =
            CMVideoFormatDescriptionGetDimensions(
                formatDescription
            )

        let pixelFormat =
            pixelFormatName(
                formatDescription
            )

        let bitsPerComponent =
            bitDepth(
                formatDescription
            )

        let is10Bit =
            bitsPerComponent >= 10

        let is422 =
            pixelFormat.contains("4:2:2") ||
            codecType == kCMVideoCodecType_AppleProRes422 ||
            codecType == kCMVideoCodecType_AppleProRes422HQ

        let is10Bit422 =
            is10Bit && is422

        let colorPrimaries =
            extensionString(
                from: formatDescription,
                key: kCMFormatDescriptionExtension_ColorPrimaries
            )

        let matrix = extensionString(from: formatDescription, key: kCMFormatDescriptionExtension_YCbCrMatrix)
        let isBT2020 = matrix == String(describing: kCMFormatDescriptionYCbCrMatrix_ITU_R_2020) &&
            colorPrimaries ==
            String(
                describing:
                    kCMFormatDescriptionColorPrimaries_ITU_R_2020
            )

        let isAppleLog =
            checkAppleLog(
                formatDescription
            )

        let frameResult =
            try await inspectFrames(
                asset: asset,
                track: track, expectedPTS: expectedPTS, expectedDurations: expectedDurations,forceSoftwareDecoding:forceSoftwareDecoding
            )

        _ = dimensions

        return OutputValidationResult(
            codec: codec,
            pixelFormat: pixelFormat,
            isProRes: isProRes,
            is10Bit422: is10Bit422,
            isBT2020: isBT2020,
            isAppleLog: isAppleLog,
            frameCount: frameResult.frameCount,
            expectedFrameCount: expectedFrameCount,
            duration: frameResult.duration,
            expectedDuration: expectedDuration,
            ptsContinuous: frameResult.ptsContinuous,
            ffmpegAvailable: false, timingIssues: frameResult.issues,decodingBackend:frameResult.decodingBackend
        )
    }

    // MARK: - Apple Log

    private func checkAppleLog(
        _ formatDescription: CMFormatDescription
    ) -> Bool {

        // Apple Log has a dedicated Core Media log-transfer
        // identifier on supported SDKs.

        if let logValue =
            CMFormatDescriptionGetExtension(
                formatDescription,
                extensionKey:
                    kCMFormatDescriptionExtension_LogTransferFunction
            ) {

            if CFEqual(
                logValue,
                kCMFormatDescriptionLogTransferFunction_AppleLog
            ) {
                return true
            }

            let value =
                String(describing: logValue)

            if value == String(describing:kCMFormatDescriptionLogTransferFunction_AppleLog) {
                return true
            }
        }

        return false
    }

    // MARK: - Frame inspection

    private func inspectFrames(
        asset: AVAsset, track: AVAssetTrack, expectedPTS: [CMTime], expectedDurations: [CMTime], forceSoftwareDecoding: Bool
    ) async throws -> StoredFrameInspection {
        var stored = try await inspectStoredFrames(asset:asset,track:track,expectedPTS:expectedPTS,expectedDurations:expectedDurations)
        if forceSoftwareDecoding {
            try await inspectDecodedFrames(asset:asset,track:track,stored:stored,software:true)
            stored.decodingBackend = "prores"
        } else {
            do {
                try await inspectDecodedFrames(asset:asset,track:track,stored:stored,software:false)
                stored.decodingBackend = "AVFoundation"
            } catch {
                if error is CancellationError { throw error }
                // A wrong image, frame count or timestamp is a validation error,
                // not an excuse to weaken the checks. Retry only decoder/reader
                // failures using an independent compressed reader.
                if let failure = error as? NativeFailure, failure.key != "error.reader" { throw error }
                try await inspectDecodedFrames(asset:asset,track:track,stored:stored,software:true)
                stored.decodingBackend = "prores"
            }
        }
        return stored
    }
    private func inspectDecodedFrames(asset: AVAsset, track: AVAssetTrack, stored: StoredFrameInspection, software: Bool) async throws {
        let formats = try await track.load(.formatDescriptions)
        guard let format = formats.first else { throw ValidationError.noFormatDescription }
        let dimensions = CMVideoFormatDescriptionGetDimensions(format)
        let decoder = try (software ? SoftwareProResDecoder(width:Int(dimensions.width),height:Int(dimensions.height)) : nil)
        let pixels = try (software ? NativeSamples.buffer(width:Int(dimensions.width),height:Int(dimensions.height)) : nil)
        let reader = try AVAssetReader(asset:asset)
        defer { reader.cancelReading() }
        let output = AVAssetReaderTrackOutput(track:track,outputSettings:software ? nil : [kCVPixelBufferPixelFormatTypeKey as String:kCVPixelFormatType_422YpCbCr10])
        let samples = VideoSampleReader(output)
        output.alwaysCopiesSampleData = false
        guard reader.canAdd(output) else { throw NativeFailure("error.reader","validation cannot add decoded track") }
        reader.add(output)
        guard reader.startReading() else { throw NativeFailure("error.reader",reader.error?.localizedDescription ?? "validation decoder unavailable") }
        var count = 0
        while let source = try samples.next() {
            try Task.checkCancellation()
            guard count < stored.timestamps.count else {
                throw NativeFailure("error.validation", "decoded frame=\(count): additional frame not present in stored ProRes samples")
            }
            let sample: CMSampleBuffer
            if let decoder, let pixels { sample = try decoder.decode(source,into:pixels) }
            else { sample = source }
            try Self.validateDecodedFrame(sample,expectedPTS:stored.timestamps[count],width:Int(dimensions.width),height:Int(dimensions.height),frame:count)
            count += 1
        }
        guard reader.status == .completed else { throw NativeFailure("error.reader",reader.error?.localizedDescription ?? "validation reader failed") }
        guard count == stored.frameCount else {
            throw NativeFailure("error.validation", "decoded frames=\(count), stored frames=\(stored.frameCount)")
        }
    }
    static func validateDecodedFrame(_ sample: CMSampleBuffer, expectedPTS: CMTime, width: Int, height: Int, frame: Int) throws {
        guard CMSampleBufferGetNumSamples(sample) == 1,
              let pixels = CMSampleBufferGetImageBuffer(sample),
              CVPixelBufferGetPixelFormatType(pixels) == kCVPixelFormatType_422YpCbCr10,
              CVPixelBufferGetWidth(pixels) == width, CVPixelBufferGetHeight(pixels) == height else {
            throw NativeFailure("error.validation", "decoded frame=\(frame): incorrect image layout or dimensions")
        }
        let pts = CMSampleBufferGetPresentationTimeStamp(sample)
        guard OutputFrameTiming.matches(pts,expectedPTS) else {
            throw NativeFailure("error.validation", "decoded frame=\(frame), PTS=\(NativeSamples.describe(pts)), storedPTS=\(NativeSamples.describe(expectedPTS))")
        }
        // Its duration is deliberately not used: the independently read MOV
        // sample timing has already been compared with the submitted duration.
    }
    func inspectStoredFrames(
        asset: AVAsset, track: AVAssetTrack, expectedPTS: [CMTime], expectedDurations: [CMTime]
    ) async throws -> StoredFrameInspection {
        let formats = try await track.load(.formatDescriptions)
        guard formats.count == 1, let format = formats.first,
              [kCMVideoCodecType_AppleProRes422,kCMVideoCodecType_AppleProRes422HQ].contains(CMFormatDescriptionGetMediaSubType(format)) else {
            throw NativeFailure("error.validation", "stored timing inspection requires intraframe ProRes 422/HQ")
        }
        let reader = try AVAssetReader(asset:asset)
        defer { reader.cancelReading() }
        let output = AVAssetReaderTrackOutput(track:track,outputSettings:nil)
        output.alwaysCopiesSampleData = false
        guard reader.canAdd(output) else { throw ValidationError.cannotAddReaderOutput }
        reader.add(output)
        guard reader.startReading() else { throw reader.error ?? ValidationError.readerFailed }
        var timing = OutputFrameTiming(expectedPTS:expectedPTS,expectedDurations:expectedDurations)
        while let sample = try NativeSamples.nextMediaSample(output) {
            try Task.checkCancellation()
            guard CMSampleBufferGetDataBuffer(sample) != nil, CMSampleBufferGetTotalSampleSize(sample) > 0 else {
                throw NativeFailure("error.validation", "stored frame=\(timing.timestamps.count): missing ProRes payload")
            }
            for index in 0..<CMSampleBufferGetNumSamples(sample) {
                var info = CMSampleTimingInfo(duration:.invalid,presentationTimeStamp:.invalid,decodeTimeStamp:.invalid)
                let status = CMSampleBufferGetSampleTimingInfo(sample,at:index,timingInfoOut:&info)
                guard status == noErr else {
                    throw NativeFailure("error.validation", "stored frame=\(timing.timestamps.count): timing status=\(status)")
                }
                // ProRes has no interframe reordering. Each stored media sample
                // is one frame, including when CoreMedia batches several frames.
                timing.append(pts:info.presentationTimeStamp,duration:info.duration)
            }
        }
        guard reader.status == .completed else { throw reader.error ?? ValidationError.readerFailed }
        return timing.result()
    }

    // MARK: - Format

    private func bitDepth(
        _ formatDescription: CMFormatDescription
    ) -> Int {

        if let value =
            CMFormatDescriptionGetExtension(
                formatDescription,
                extensionKey:
                    kCMFormatDescriptionExtension_BitsPerComponent
            ) {

            if let number = value as? NSNumber {
                return number.intValue
            }

            return Int(
                String(describing: value)
            ) ?? 0
        }

        // ProRes 422 family is 10-bit.
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

    private func pixelFormatName(
        _ formatDescription: CMFormatDescription
    ) -> String {

        let codec =
            CMFormatDescriptionGetMediaSubType(
                formatDescription
            )

        switch codec {

        case kCMVideoCodecType_AppleProRes422,
             kCMVideoCodecType_AppleProRes422HQ,
             kCMVideoCodecType_AppleProRes422LT,
             kCMVideoCodecType_AppleProRes422Proxy:

            return "10-bit 4:2:2"

        default:
            return "Unknown"
        }
    }

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
        from formatDescription: CMFormatDescription,
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

        return String(describing: value)
    }

    enum ValidationError: LocalizedError {

        case noVideoTrack
        case noFormatDescription
        case cannotAddReaderOutput
        case readerFailed

        var errorDescription: String? {

            switch self {

            case .noVideoTrack:
                return L10n.text("error.tracks")

            case .noFormatDescription:
                return L10n.text("error.sample")

            case .cannotAddReaderOutput:
                return L10n.text("error.reader")

            case .readerFailed:
                return L10n.text("error.validation")
            }
        }
    }
}























