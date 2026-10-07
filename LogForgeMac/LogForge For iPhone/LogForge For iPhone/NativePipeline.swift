import Foundation
import AVFoundation
import CoreMedia
import CoreVideo
import CryptoKit
import AudioToolbox

// Scene/queue restoration can occur while another scene or a test is exporting.
// Keep startup cleanup from deleting files owned by any active pipeline.
final class NativeExportFiles: @unchecked Sendable {
    static let shared = NativeExportFiles()
    private let lock = NSLock()
    private var active: Set<String> = []
    func register(_ urls: [URL]) {
        lock.lock(); defer { lock.unlock() }
        active.formUnion(urls.map { $0.standardizedFileURL.path })
    }
    func release(_ urls: [URL]) {
        lock.lock(); defer { lock.unlock() }
        active.subtract(urls.map { $0.standardizedFileURL.path })
    }
    func removeAbandoned(_ url: URL) {
        lock.lock(); defer { lock.unlock() }
        guard !active.contains(url.standardizedFileURL.path) else { return }
        try? FileManager.default.removeItem(at:url)
    }
}

struct InputContract {
    let track: AVAssetTrack
    let description: CMFormatDescription
    let width: Int
    let height: Int
    let fullRange: Bool
    let centeredChroma: Bool
    let warnings: [String]
    func decodedLayout(_ sample: CMSampleBuffer) throws -> (fullRange: Bool, centeredChroma: Bool) {
        guard let pixels = CMSampleBufferGetImageBuffer(sample),
              CVPixelBufferGetPixelFormatType(pixels) == kCVPixelFormatType_422YpCbCr10,
              CVPixelBufferGetWidth(pixels) == width, CVPixelBufferGetHeight(pixels) == height,
              let format = CMSampleBufferGetFormatDescription(sample) else { throw NativeFailure("error.sample") }
        func value(_ key: CFString) -> String? { CMFormatDescriptionGetExtension(format,extensionKey:key).map { String(describing:$0) } }
        for (key,expected) in [(kCMFormatDescriptionExtension_ColorPrimaries,kCMFormatDescriptionColorPrimaries_ITU_R_2020),
                               (kCMFormatDescriptionExtension_YCbCrMatrix,kCMFormatDescriptionYCbCrMatrix_ITU_R_2020),
                               (kCMFormatDescriptionExtension_TransferFunction,kCMFormatDescriptionTransferFunction_ITU_R_2100_HLG)] {
            if let actual = value(key), actual != String(describing:expected) { throw NativeFailure("error.color") }
        }
        if let fields = CMFormatDescriptionGetExtension(format,extensionKey:kCMFormatDescriptionExtension_FieldCount) as? NSNumber,
           fields.intValue != 1 { throw NativeFailure("error.interlaced") }
        let range = CMFormatDescriptionGetExtension(format,extensionKey:kCMFormatDescriptionExtension_FullRangeVideo) as? NSNumber
        let top = value(kCMFormatDescriptionExtension_ChromaLocationTopField)
        let left = String(describing:kCMFormatDescriptionChromaLocation_Left), center = String(describing:kCMFormatDescriptionChromaLocation_Center)
        guard top == nil || top == left || top == center else { throw NativeFailure("error.chroma") }
        // Decoders can normalize range and chroma phase. Prefer the actual decoded
        // sample's description, using the inspected source only when unspecified.
        return (range?.boolValue ?? fullRange,top.map { $0 == center } ?? centeredChroma)
    }
    static func inspect(_ asset: AVURLAsset) async throws -> InputContract {
        guard asset.url.pathExtension.lowercased() == "mov" else { throw NativeFailure("error.input") }
        let videos = try await asset.loadTracks(withMediaType:.video)
        guard videos.count == 1, let track = videos.first else { throw NativeFailure("error.tracks") }
        let descriptions = try await track.load(.formatDescriptions)
        guard descriptions.count == 1, let d = descriptions.first,
              [kCMVideoCodecType_AppleProRes422,kCMVideoCodecType_AppleProRes422HQ].contains(CMFormatDescriptionGetMediaSubType(d)) else {
            throw NativeFailure("error.input", "format descriptions=\(descriptions.count), codecs=\(descriptions.map { CMFormatDescriptionGetMediaSubType($0) })")
        }
        func value(_ key: CFString) -> String? { CMFormatDescriptionGetExtension(d,extensionKey:key).map { String(describing:$0) } }
        let primaries = value(kCMFormatDescriptionExtension_ColorPrimaries), matrix = value(kCMFormatDescriptionExtension_YCbCrMatrix)
        let transfer = value(kCMFormatDescriptionExtension_TransferFunction)
        guard transfer == String(describing:kCMFormatDescriptionTransferFunction_ITU_R_2100_HLG),
              primaries == nil || primaries == String(describing:kCMFormatDescriptionColorPrimaries_ITU_R_2020),
              matrix == nil || matrix == String(describing:kCMFormatDescriptionYCbCrMatrix_ITU_R_2020) else {
            throw NativeFailure("error.color","primaries=\(primaries ?? "unspecified"), transfer=\(transfer ?? "unspecified"), matrix=\(matrix ?? "unspecified")")
        }
        var warnings: [String] = []
        if primaries == nil || matrix == nil { warnings.append(L10n.text("warning.2020")) }
        let range = CMFormatDescriptionGetExtension(d,extensionKey:kCMFormatDescriptionExtension_FullRangeVideo) as? NSNumber
        if range == nil { warnings.append(L10n.text("warning.range")) }
        let top = value(kCMFormatDescriptionExtension_ChromaLocationTopField), bottom = value(kCMFormatDescriptionExtension_ChromaLocationBottomField)
        let left = String(describing:kCMFormatDescriptionChromaLocation_Left), center = String(describing:kCMFormatDescriptionChromaLocation_Center)
        guard (top == nil || top == left || top == center), (bottom == nil || bottom == top || (top == nil && bottom == left)) else { throw NativeFailure("error.chroma") }
        if top == nil { warnings.append(L10n.text("warning.chroma")) }
        if let fields = CMFormatDescriptionGetExtension(d,extensionKey:kCMFormatDescriptionExtension_FieldCount) as? NSNumber, fields.intValue != 1 { throw NativeFailure("error.interlaced") }
        // CoreMedia reports 12 here even for our independently generated 10-bit
        // prores_ks 422/HQ fixtures. This codec description is not the original
        // camera's sample precision. The decoded working format is checked as
        // 10-bit V210 in decodedLayout; retain the bounded ProRes-only contract.
        if let bits = CMFormatDescriptionGetExtension(d,extensionKey:kCMFormatDescriptionExtension_BitsPerComponent) as? NSNumber,
           ![10,12].contains(bits.intValue) {
            throw NativeFailure("error.input", "BitsPerComponent=\(bits)")
        }
        let dimensions = CMVideoFormatDescriptionGetDimensions(d)
        guard dimensions.width > 0, dimensions.height > 0, dimensions.width % 2 == 0 else { throw NativeFailure("error.size") }
        return InputContract(track:track,description:d,width:Int(dimensions.width),height:Int(dimensions.height),fullRange:range?.boolValue ?? false,centeredChroma:top == center,warnings:warnings)
    }
}

final class SoftwareProResEncoder {
    private let handle: OpaquePointer
    init(width: Int, height: Int, quality: ProResQuality) throws {
        guard let handle = LFEncoderCreate(Int32(width),Int32(height),quality == .proRes422HQ ? 3 : 2) else {
            throw NativeFailure("error.encoder",String(cString:LFEncoderLastError(nil)))
        }
        self.handle = handle
    }
    deinit { LFEncoderDestroy(handle) }
    func encode(_ pixels: CVPixelBuffer, pts: CMTime, duration: CMTime) throws -> CMSampleBuffer {
        guard let sample = LFEncoderEncode(handle,pixels,pts,duration) else {
            throw NativeFailure("error.encoder",String(cString:LFEncoderLastError(handle)))
        }
        return sample
    }
}

final class SoftwareProResDecoder {
    private let handle: OpaquePointer
    init(width: Int, height: Int) throws {
        guard let decoder = LFDecoderCreate(Int32(width),Int32(height)) else { throw NativeFailure("error.decoderSoftware",String(cString:LFDecoderLastError(nil))) }
        handle = decoder
    }
    deinit { LFDecoderDestroy(handle) }
    // The caller owns the pixel buffer and may recycle it only after processing
    // finishes. No AVFrame pointer escapes the C decoder.
    func decode(_ source: CMSampleBuffer, into pixels: CVPixelBuffer) throws -> CMSampleBuffer {
        try Task.checkCancellation()
        guard LFDecoderDecode(handle,source,pixels) != 0 else { throw NativeFailure("error.decoderSoftware",String(cString:LFDecoderLastError(handle))) }
        guard let sourceFormat = CMSampleBufferGetFormatDescription(source) else { throw NativeFailure("error.sample") }
        CVBufferRemoveAllAttachments(pixels)
        for (formatKey,pixelKey) in [(kCMFormatDescriptionExtension_ColorPrimaries,kCVImageBufferColorPrimariesKey),
            (kCMFormatDescriptionExtension_TransferFunction,kCVImageBufferTransferFunctionKey),
            (kCMFormatDescriptionExtension_LogTransferFunction,kCVImageBufferLogTransferFunctionKey),
            (kCMFormatDescriptionExtension_YCbCrMatrix,kCVImageBufferYCbCrMatrixKey),
            (kCMFormatDescriptionExtension_ChromaLocationTopField,kCVImageBufferChromaLocationTopFieldKey),
            (kCMFormatDescriptionExtension_ChromaLocationBottomField,kCVImageBufferChromaLocationBottomFieldKey)] {
            if let value = CMFormatDescriptionGetExtension(sourceFormat,extensionKey:formatKey) { CVBufferSetAttachment(pixels,pixelKey,value as CFTypeRef,.shouldPropagate) }
        }
        var format: CMVideoFormatDescription?
        let formatStatus = CMVideoFormatDescriptionCreateForImageBuffer(allocator:nil,imageBuffer:pixels,formatDescriptionOut:&format)
        guard formatStatus == noErr, let format else { throw NativeFailure("error.sample","software decoded format status=\(formatStatus)") }
        var timing = CMSampleTimingInfo(duration:CMSampleBufferGetDuration(source),presentationTimeStamp:CMSampleBufferGetPresentationTimeStamp(source),decodeTimeStamp:CMSampleBufferGetDecodeTimeStamp(source))
        var sample: CMSampleBuffer?
        let status = CMSampleBufferCreateReadyWithImageBuffer(allocator:nil,imageBuffer:pixels,formatDescription:format,sampleTiming:&timing,sampleBufferOut:&sample)
        guard status == noErr, let sample else { throw NativeFailure("error.sample","software decoded sample status=\(status)") }
        return sample
    }
}

final class VideoSampleReader {
    let output: AVAssetReaderTrackOutput
    private var buffered: CMSampleBuffer?
    private var index = 0
    init(_ output: AVAssetReaderTrackOutput) { self.output = output }
    func next() throws -> CMSampleBuffer? {
        try Task.checkCancellation()
        if buffered == nil { buffered = try NativeSamples.nextMediaSample(output); index = 0 }
        guard let source = buffered else { return nil }
        let count = CMSampleBufferGetNumSamples(source)
        if count == 1 { buffered = nil; return source }
        var sample: CMSampleBuffer?
        let status = CMSampleBufferCopySampleBufferForRange(allocator:nil,sampleBuffer:source,sampleRange:CFRange(location:index,length:1),sampleBufferOut:&sample)
        guard status == noErr, let sample else { throw NativeFailure("error.sample","ProRes packet split status=\(status), index=\(index)") }
        index += 1
        if index == count { buffered = nil }
        return sample
    }
}

enum NativeSamples {
    static func nextMediaSample(_ output: AVAssetReaderTrackOutput) throws -> CMSampleBuffer? {
        while let sample = output.copyNextSampleBuffer() {
            try Task.checkCancellation()
            if CMSampleBufferGetNumSamples(sample) > 0 { return sample }
            // AVFoundation emits zero-sample control buffers for empty edits and
            // boundaries (often at the same PTS as a real first frame). They have
            // no media payload. Real sample PTS still describes every gap; do not
            // feed these controls to an encoder, retime them or count them as frames.
            guard CMSampleBufferGetTotalSampleSize(sample) == 0,
                  CMSampleBufferGetImageBuffer(sample) == nil else {
                throw NativeFailure("error.sample", "zero-sample buffer contains media at \(describe(CMSampleBufferGetPresentationTimeStamp(sample)))")
            }
        }
        return nil
    }
    static func describe(_ time: CMTime) -> String {
        "\(time.value)/\(time.timescale) (flags=\(time.flags.rawValue), epoch=\(time.epoch))"
    }
    // A zero or integral track start does not require its original denominator.
    // Reduce the rational value without changing the time or using Double.
    static func requiredScale(_ time: CMTime) throws -> CMTimeScale {
        guard time.isNumeric, time.timescale > 0, time.epoch == 0 else {
            throw NativeFailure("error.timeScale", "track start: \(describe(time))")
        }
        var a = time.value.magnitude, b = UInt64(time.timescale)
        while b != 0 { let remainder = a % b; a = b; b = remainder }
        return CMTimeScale(UInt64(time.timescale) / a)
    }
    static func movieScale(_ trackScales: [CMTimeScale], starts: [CMTime]) throws -> CMTimeScale {
        guard !trackScales.isEmpty, trackScales.allSatisfy({ $0 > 0 }) else {
            throw NativeFailure("error.timeScale", "track scales: \(trackScales)")
        }
        let scales = try trackScales + starts.map { try requiredScale($0) }
        if let exact = try? exactScale(scales) { return exact }
        // The MOV header has one clock; each media track has its own clock.
        // Nanosecond video plus 48 kHz audio needs an LCM of 3,000,000,000,
        // which cannot fit CMTimeScale. Use a high-resolution header instead;
        // retain media clocks and submitted PTS/durations unchanged. Header
        // rounding is at most half a nanosecond, checked again after writing.
        return max(1_000_000_000,scales.max()!)
    }
    static func mediaScale(_ naturalScale: CMTimeScale, offset: CMTime) throws -> CMTimeScale {
        try exactScale([naturalScale,requiredScale(offset)])
    }
    static func exactScale(_ scales: [CMTimeScale]) throws -> CMTimeScale {
        func gcd(_ a: Int64, _ b: Int64) -> Int64 { var x = a, y = b; while y != 0 { let r = x % y; x = y; y = r }; return x }
        var result: Int64 = 1
        for scale in scales {
            guard scale > 0 else { throw NativeFailure("error.timeScale", "track scales: \(scales)") }
            let next = result / gcd(result,Int64(scale)) * Int64(scale)
            guard next <= Int64(Int32.max) else { throw NativeFailure("error.timeScale", "LCM=\(next), limit=\(Int32.max), scales=\(scales)") }
            result = next
        }
        return CMTimeScale(result)
    }
    static func pool(width: Int, height: Int) throws -> CVPixelBufferPool {
        var pool: CVPixelBufferPool?
        let attributes: [String:Any] = [kCVPixelBufferWidthKey as String:width,
            kCVPixelBufferHeightKey as String:height,
            kCVPixelBufferPixelFormatTypeKey as String:kCVPixelFormatType_422YpCbCr10,
            kCVPixelBufferIOSurfacePropertiesKey as String:[:],
            kCVPixelBufferBytesPerRowAlignmentKey as String:128]
        guard CVPixelBufferPoolCreate(nil,nil,attributes as CFDictionary,&pool) == kCVReturnSuccess, let pool else { throw NativeFailure("error.memory") }
        return pool
    }
    static func pooledBuffer(_ pool: CVPixelBufferPool) throws -> CVPixelBuffer? {
        var pixel: CVPixelBuffer?
        let attributes = [kCVPixelBufferPoolAllocationThresholdKey as String:4] as CFDictionary
        let status = CVPixelBufferPoolCreatePixelBufferWithAuxAttributes(nil,pool,attributes,&pixel)
        if status == kCVReturnWouldExceedAllocationThreshold { return nil }
        guard status == kCVReturnSuccess, let pixel else { throw NativeFailure("error.memory") }
        return pixel
    }
    static func pooledBuffer(_ pool: CVPixelBufferPool, writer: AVAssetWriter) async throws -> CVPixelBuffer {
        while true {
            try Task.checkCancellation()
            guard writer.status == .writing else { throw NativeFailure("error.writer",writer.error?.localizedDescription ?? "") }
            if let pixel = try pooledBuffer(pool) { return pixel }
            // AVAssetWriter retains submitted images until encoding completes. Only
            // the pool may recycle them; never overwrite a previously appended frame.
            try await Task.sleep(nanoseconds:1_000_000)
        }
    }
    static func buffer(width: Int, height: Int) throws -> CVPixelBuffer {
        var pixel: CVPixelBuffer?
        let settings: [String:Any] = [kCVPixelBufferIOSurfacePropertiesKey as String:[:],kCVPixelBufferBytesPerRowAlignmentKey as String:128]
        guard CVPixelBufferCreate(nil,width,height,kCVPixelFormatType_422YpCbCr10,settings as CFDictionary,&pixel) == kCVReturnSuccess, let pixel else { throw NativeFailure("error.memory") }
        return pixel
    }
    static func tag(_ pixels: CVPixelBuffer) {
        CVBufferRemoveAllAttachments(pixels)
        for (key,value) in [(kCVImageBufferColorPrimariesKey,kCVImageBufferColorPrimaries_ITU_R_2020),
                            (kCVImageBufferYCbCrMatrixKey,kCVImageBufferYCbCrMatrix_ITU_R_2020),
                            (kCVImageBufferLogTransferFunctionKey,kCVImageBufferLogTransferFunction_AppleLog),
                            (kCVImageBufferChromaLocationTopFieldKey,kCVImageBufferChromaLocation_Left),
                            (kCVImageBufferChromaLocationBottomFieldKey,kCVImageBufferChromaLocation_Left)] {
            CVBufferSetAttachment(pixels,key,value,.shouldPropagate)
        }
    }
    static func raw(_ pixels: CVPixelBuffer, pts: CMTime, duration: CMTime) throws -> CMSampleBuffer {
        tag(pixels)
        var format: CMVideoFormatDescription?
        guard CMVideoFormatDescriptionCreateForImageBuffer(allocator:nil,imageBuffer:pixels,formatDescriptionOut:&format) == noErr, let format else { throw NativeFailure("error.sample") }
        var timing = CMSampleTimingInfo(duration:duration,presentationTimeStamp:pts,decodeTimeStamp:.invalid)
        var sample: CMSampleBuffer?
        guard CMSampleBufferCreateReadyWithImageBuffer(allocator:nil,imageBuffer:pixels,formatDescription:format,sampleTiming:&timing,sampleBufferOut:&sample) == noErr, let sample else { throw NativeFailure("error.sample") }
        return sample
    }
    static func retime(_ sample: CMSampleBuffer, offset: CMTime, context: String = "copied sample") throws -> CMSampleBuffer {
        guard offset.isNumeric else { throw NativeFailure("error.timing", "\(context): invalid offset \(describe(offset))") }
        // With no timeline shift, passthrough must retain the original buffer,
        // including opaque audio/auxiliary timing and sample attachments.
        // The caller still validates its buffer PTS before submitting it.
        if offset == .zero { return sample }
        var count = 0
        let queryStatus = CMSampleBufferGetSampleTimingInfoArray(sample,entryCount:0,arrayToFill:nil,entriesNeededOut:&count)
        guard queryStatus == noErr, count > 0 else {
            throw NativeFailure("error.timing", "\(context): timing query status=\(queryStatus), entries=\(count), samples=\(CMSampleBufferGetNumSamples(sample)), PTS=\(describe(CMSampleBufferGetPresentationTimeStamp(sample))), offset=\(describe(offset))")
        }
        var timings = [CMSampleTimingInfo](repeating:CMSampleTimingInfo(duration:.invalid,presentationTimeStamp:.invalid,decodeTimeStamp:.invalid),count:count)
        let readStatus = CMSampleBufferGetSampleTimingInfoArray(sample,entryCount:count,arrayToFill:&timings,entriesNeededOut:&count)
        guard readStatus == noErr else {
            throw NativeFailure("error.timing", "\(context): timing read status=\(readStatus), entries=\(count)")
        }
        for i in timings.indices {
            guard timings[i].presentationTimeStamp.isNumeric else {
                throw NativeFailure("error.timing", "\(context): timing entry=\(i), PTS=\(describe(timings[i].presentationTimeStamp)), DTS=\(describe(timings[i].decodeTimeStamp)), duration=\(describe(timings[i].duration)), offset=\(describe(offset))")
            }
            timings[i].presentationTimeStamp = timings[i].presentationTimeStamp-offset
            if timings[i].decodeTimeStamp.isNumeric { timings[i].decodeTimeStamp = timings[i].decodeTimeStamp-offset }
        }
        var result: CMSampleBuffer?
        let copyStatus = CMSampleBufferCreateCopyWithNewTiming(allocator:nil,sampleBuffer:sample,sampleTimingEntryCount:count,sampleTimingArray:&timings,sampleBufferOut:&result)
        guard copyStatus == noErr, let result else { throw NativeFailure("error.sample", "\(context): timing copy status=\(copyStatus)") }
        return result
    }
    static func ready(_ input: AVAssetWriterInput, writer: AVAssetWriter) async throws {
        while !input.isReadyForMoreMediaData {
            try Task.checkCancellation()
            guard writer.status == .writing else { throw NativeFailure("error.writer",writer.error?.localizedDescription ?? "") }
            try await Task.sleep(nanoseconds:1_000_000)
        }
    }
    static func finish(_ writer: AVAssetWriter) async { await withCheckedContinuation { c in writer.finishWriting { c.resume() } } }
    static func settings(_ width: Int, _ height: Int, _ quality: ProResQuality) -> [String:Any] {
        [AVVideoCodecKey:quality.codec,AVVideoWidthKey:width,AVVideoHeightKey:height,
         AVVideoColorPropertiesKey:[AVVideoColorPrimariesKey:AVVideoColorPrimaries_ITU_R_2020,
                                    AVVideoYCbCrMatrixKey:AVVideoYCbCrMatrix_ITU_R_2020]]
    }
}

actor NativeCapabilities {
    static let shared = NativeCapabilities()
    private var probes: [String:Task<Bool,Never>] = [:]
    func supports(width: Int, height: Int, quality: ProResQuality) async -> Bool {
        let key = "\(width)x\(height)-\(quality.rawValue)"
        if let probe = probes[key] { return await probe.value }
        let probe = Task { await Self.probe(width:width,height:height,quality:quality) }
        probes[key] = probe
        return await probe.value
    }
    private static func probe(width: Int, height: Int, quality: ProResQuality) async -> Bool {
        let url = FileManager.default.temporaryDirectory.appendingPathComponent("LogForge-probe-\(UUID()).mov")
        var writer: AVAssetWriter?
        defer { if writer?.status == .writing { writer?.cancelWriting() }; try? FileManager.default.removeItem(at:url) }
        do {
            let w = try AVAssetWriter(outputURL:url,fileType:.mov); writer = w
            w.shouldOptimizeForNetworkUse = false
            let settings = NativeSamples.settings(width,height,quality)
            guard w.canApply(outputSettings:settings,forMediaType:.video) else { return false }
            let input = AVAssetWriterInput(mediaType:.video,outputSettings:settings)
            guard w.canAdd(input) else { return false }; w.add(input)
            let pixels = try NativeSamples.buffer(width:width,height:height)
            guard CVPixelBufferLockBaseAddress(pixels,[]) == kCVReturnSuccess else { return false }
            guard let base = CVPixelBufferGetBaseAddress(pixels) else { CVPixelBufferUnlockBaseAddress(pixels,[]); return false }
            let stride = CVPixelBufferGetBytesPerRow(pixels)
            memset(base,0,stride*height)
            for y in 0..<height {
                let row = base.advanced(by:y*stride).assumingMemoryBound(to:UInt32.self)
                for g in 0..<((width+5)/6) {
                    let l: UInt32 = UInt32(64+(g%8)*110)
                    row[g*4] = 512 | l<<10 | 512<<20; row[g*4+1] = l | 512<<10 | l<<20
                    row[g*4+2] = 512 | l<<10 | 512<<20; row[g*4+3] = l | 512<<10 | l<<20
                }
            }
            CVPixelBufferUnlockBaseAddress(pixels,[])
            guard w.startWriting() else { return false }; w.startSession(atSourceTime:.zero)
            for i in 0..<3 {
                try await NativeSamples.ready(input,writer:w)
                guard input.append(try NativeSamples.raw(pixels,pts:CMTime(value:Int64(i),timescale:30),duration:CMTime(value:1,timescale:30))) else { return false }
            }
            input.markAsFinished(); await NativeSamples.finish(w)
            guard w.status == .completed else { return false }
            try AppleLogMOV.process(url:url,patch:true)
            let result = try await OutputValidator().validate(outputURL:url,expectedFrameCount:3,expectedDuration:0.1)
            return result.passed
        } catch { return false }
    }
}

struct NativePipeline {
    let options: ConversionOptions
    let quality: ProResQuality
    let progress: VideoProcessor.ProgressHandler
    func process(url: URL) async throws -> VideoProcessingResult {
        try options.validate()
        let asset = AVURLAsset(url:url), contract = try await InputContract.inspect(asset)
        try Task.checkCancellation()
        let native = await NativeCapabilities.shared.supports(width:contract.width,height:contract.height,quality:quality)
        try Task.checkCancellation()
        var softwareEncoding = !native, softwareDecoding = false
        var fallbackWarnings: [String] = []
        // At most one restart for each unavailable system stage. Failed partial
        // exports are cleaned up by run; the original source timeline is reused.
        for _ in 0..<3 {
            do { return try await run(asset:asset,contract:contract,software:softwareEncoding,extraWarnings:fallbackWarnings,softwareDecoding:softwareDecoding) }
            catch {
                if error is CancellationError { throw error }
                guard let failure = error as? NativeFailure else { throw error }
                if failure.key == "error.reader", !softwareDecoding {
                    softwareDecoding = true; fallbackWarnings.append(L10n.text("warning.decoderFallback")); continue
                }
                if failure.key == "error.writer", !softwareEncoding {
                    softwareEncoding = true; fallbackWarnings.append(L10n.text("warning.encoderFallback")); continue
                }
                throw error
            }
        }
        throw NativeFailure("error.reader","system fallbacks exhausted")
    }

    func run(asset: AVURLAsset, contract: InputContract, software: Bool, extraWarnings: [String] = [], softwareDecoding: Bool = false) async throws -> VideoProcessingResult {
        try Task.checkCancellation()
        let fm = FileManager.default
        let directory = fm.urls(for:.documentDirectory,in:.userDomainMask)[0].appendingPathComponent("Exports",isDirectory:true)
        try fm.createDirectory(at:directory,withIntermediateDirectories:true)
        let partial = directory.appendingPathComponent(".LogForge-\(UUID()).partial.mov")
        var published = false
        let partialReport = partial.appendingPathExtension("json")
        NativeExportFiles.shared.register([partial,partialReport])
        defer { NativeExportFiles.shared.release([partial,partialReport]) }
        let reader = try AVAssetReader(asset:asset), writer = try AVAssetWriter(outputURL:partial,fileType:.mov)
        defer {
            if reader.status == .reading { reader.cancelReading() }
            if writer.status == .writing || writer.status == .unknown { writer.cancelWriting() }
            if !published, fm.fileExists(atPath:partial.path) { try? fm.removeItem(at:partial) }
            if fm.fileExists(atPath:partialReport.path) { try? fm.removeItem(at:partialReport) }
        }
        writer.shouldOptimizeForNetworkUse = false
        var warnings = contract.warnings
        warnings.append(contentsOf:extraWarnings)
        let metadata = try await asset.load(.metadata)
        writer.metadata = metadata.filter { item in
            let name = (item.identifier?.rawValue ?? String(describing:item.key)).lowercased()
            return !name.contains("location") && !name.contains("gps") && ["creationdate","creationtime","make","model","language"].contains { name.contains($0) }
        }
        let decoder = try (softwareDecoding ? SoftwareProResDecoder(width:contract.width,height:contract.height) : nil)
        let decodedPixels = try (softwareDecoding ? NativeSamples.buffer(width:contract.width,height:contract.height) : nil)
        let videoOutput = AVAssetReaderTrackOutput(track:contract.track,outputSettings:softwareDecoding ? nil : [kCVPixelBufferPixelFormatTypeKey as String:kCVPixelFormatType_422YpCbCr10])
        let videoSamples = VideoSampleReader(videoOutput)
        videoOutput.alwaysCopiesSampleData = false
        guard reader.canAdd(videoOutput) else { throw NativeFailure("error.reader") }; reader.add(videoOutput)
        let encoder: SoftwareProResEncoder? = try (software ? SoftwareProResEncoder(width:contract.width,height:contract.height,quality:quality) : nil)
        var compressedFormat: CMVideoFormatDescription?
        if software {
            let ext: [String:Any] = [kCMFormatDescriptionExtension_ColorPrimaries as String:kCMFormatDescriptionColorPrimaries_ITU_R_2020,
                kCMFormatDescriptionExtension_YCbCrMatrix as String:kCMFormatDescriptionYCbCrMatrix_ITU_R_2020,
                kCMFormatDescriptionExtension_LogTransferFunction as String:kCMFormatDescriptionLogTransferFunction_AppleLog,
                kCMFormatDescriptionExtension_FullRangeVideo as String:false]
            guard CMVideoFormatDescriptionCreate(allocator:nil,codecType:quality == .proRes422HQ ? kCMVideoCodecType_AppleProRes422HQ : kCMVideoCodecType_AppleProRes422,width:Int32(contract.width),height:Int32(contract.height),extensions:ext as CFDictionary,formatDescriptionOut:&compressedFormat) == noErr else { throw NativeFailure("error.sample") }
        }
        let videoInput = AVAssetWriterInput(mediaType:.video,outputSettings:software ? nil : NativeSamples.settings(contract.width,contract.height,quality),sourceFormatHint:compressedFormat)
        videoInput.transform = try await contract.track.load(.preferredTransform)
        videoInput.expectsMediaDataInRealTime = false
        guard writer.canAdd(videoInput) else { throw NativeFailure("error.writer") }; writer.add(videoInput)
        let allTracks = try await asset.load(.tracks)
        var pairs: [(AVAssetReaderTrackOutput,AVAssetWriterInput)] = []
        let chapterTracks = try await contract.track.loadAssociatedTracks(ofType:.chapterList)
        let chapterIDs = Set(chapterTracks.map { $0.trackID })
        var trackInputs: [CMPersistentTrackID:AVAssetWriterInput] = [contract.track.trackID:videoInput]
        var trackScales: [CMPersistentTrackID:CMTimeScale] = [:]
        var movieScales: [CMTimeScale] = []
        var trackStarts: [CMTime] = []
        var offset = CMTime.zero
        for track in allTracks {
            let retained = track.trackID == contract.track.trackID || [.audio,.timecode].contains(track.mediaType) || chapterIDs.contains(track.trackID)
            if retained {
                let range = try await track.load(.timeRange)
                if range.start.isNumeric { offset = min(offset,range.start) }
                let scale = try await track.load(.naturalTimeScale)
                trackScales[track.trackID] = scale
                movieScales.append(scale)
                if range.start.isNumeric { trackStarts.append(range.start) }
            }
            guard track.trackID != contract.track.trackID else { continue }
            if [.audio,.timecode].contains(track.mediaType) || chapterIDs.contains(track.trackID) {
                let formats = try await track.load(.formatDescriptions)
                guard let hint = formats.first else { throw NativeFailure("error.trackCopy") }
                let output = AVAssetReaderTrackOutput(track:track,outputSettings:nil)
                output.alwaysCopiesSampleData = false
                let input = AVAssetWriterInput(mediaType:track.mediaType,outputSettings:nil,sourceFormatHint:hint)
                input.expectsMediaDataInRealTime = false
                input.languageCode = try await track.load(.languageCode)
                input.extendedLanguageTag = try await track.load(.extendedLanguageTag)
                input.metadata = (try await track.load(.metadata)).filter { item in
                    let id = (item.identifier?.rawValue ?? "").lowercased()
                    return id.contains("language") || id.contains("timecode")
                }
                guard reader.canAdd(output), writer.canAdd(input) else { throw NativeFailure("error.trackCopy") }
                reader.add(output); writer.add(input); pairs.append((output,input)); trackInputs[track.trackID] = input
            } else { warnings.append(L10n.text("warning.track") + " " + track.mediaType.rawValue) }
        }
        writer.movieTimeScale = try NativeSamples.movieScale(movieScales,starts:trackStarts)
        for track in allTracks where track.mediaType != .audio {
            if let input = trackInputs[track.trackID], let scale = trackScales[track.trackID] {
                input.mediaTimeScale = try NativeSamples.mediaScale(scale,offset:offset)
            }
        }
        for track in allTracks {
            guard let input = trackInputs[track.trackID] else { continue }
            for type in [AVAssetTrack.AssociationType.chapterList, .timecode] {
                for associated in try await track.loadAssociatedTracks(ofType:type) {
                    guard let target = trackInputs[associated.trackID], input.canAddTrackAssociation(withTrackOf:target,type:type.rawValue) else { throw NativeFailure("error.trackCopy") }
                    input.addTrackAssociation(withTrackOf:target,type:type.rawValue)
                }
            }
        }
        let duration = try await asset.load(.duration)
        let videoRange = try await contract.track.load(.timeRange)
        guard duration.isNumeric, duration.seconds > 0 else {
            throw NativeFailure("error.timing", "asset duration: \(NativeSamples.describe(duration))")
        }
        var metal: MetalColorProcessor?
        if options.backend != .cpu {
            do { let gpu = try MetalColorProcessor(); try await gpu.qualify(options:options); metal = gpu }
            catch { if options.backend == .metal { throw error }; warnings.append(L10n.text("warning.metal")) }
        }
        let outputPool = try NativeSamples.pool(width:contract.width,height:contract.height)
        var cpuReference: CVPixelBuffer? = try (metal == nil ? nil : NativeSamples.buffer(width:contract.width,height:contract.height))
        var qualified = false
        guard writer.startWriting() else { throw NativeFailure("error.writer",writer.error?.localizedDescription ?? "") }
        writer.startSession(atSourceTime:.zero)
        guard reader.startReading() else { throw NativeFailure("error.reader",reader.error?.localizedDescription ?? "") }
        var pendingVideo = try videoSamples.next()
        var pending = try pairs.map { try NativeSamples.nextMediaSample($0.0) }
        var finishedTracks = pairs.map { _ in false }
        for i in pending.indices where pending[i] == nil { pairs[i].1.markAsFinished(); finishedTracks[i] = true }
        var videoFinished = pendingVideo == nil
        if videoFinished { videoInput.markAsFinished() }
        var previous: CMTime?, count = 0, first: CMTime?, videoEnd = CMTime.zero
        var timestamps: [CMTime] = [], durations: [CMTime] = []
        while pendingVideo != nil || pending.contains(where: { $0 != nil }) {
            try Task.checkCancellation()
            var chosen: Int? = nil
            var earliest = pendingVideo.map { CMSampleBufferGetPresentationTimeStamp($0) }
            for i in pending.indices {
                if let sample = pending[i] {
                    let pts = CMSampleBufferGetPresentationTimeStamp(sample)
                    guard pts.isNumeric else {
                        throw NativeFailure("error.timing", "copied track \(i), PTS: \(NativeSamples.describe(pts))")
                    }
                    if earliest == nil || pts < earliest! { chosen = i; earliest = pts }
                }
            }
            if let i = chosen, let sample = pending[i] {
                try await NativeSamples.ready(pairs[i].1,writer:writer)
                guard pairs[i].1.append(try NativeSamples.retime(sample,offset:offset,context:"track=\(pairs[i].0.track.trackID), type=\(pairs[i].0.track.mediaType.rawValue)")) else { throw NativeFailure("error.trackCopy",writer.error?.localizedDescription ?? "") }
                pending[i] = try NativeSamples.nextMediaSample(pairs[i].0)
                if pending[i] == nil { pairs[i].1.markAsFinished(); finishedTracks[i] = true }
                continue
            }
            guard let sourceSample = pendingVideo else { throw NativeFailure("error.sample") }
            let sample: CMSampleBuffer
            if let decoder, let decodedPixels { sample = try decoder.decode(sourceSample,into:decodedPixels) }
            else { sample = sourceSample }
            guard let pixels = CMSampleBufferGetImageBuffer(sample) else { throw NativeFailure("error.sample") }
            // Software ProRes decoding only unpacks DCT samples. It performs no
            // range or chroma-phase normalization, so retain the source contract.
            let layout = try (softwareDecoding ? (fullRange:contract.fullRange,centeredChroma:contract.centeredChroma) : contract.decodedLayout(sample))
            let sourcePTS = CMSampleBufferGetPresentationTimeStamp(sample), pts = sourcePTS-offset
            guard pts.isNumeric, pts.seconds.isFinite, pts >= .zero, previous == nil || pts > previous! else {
                throw NativeFailure("error.timing", "frame=\(count), sourcePTS=\(NativeSamples.describe(sourcePTS)), offset=\(NativeSamples.describe(offset)), PTS=\(NativeSamples.describe(pts)), previous=\(previous.map { NativeSamples.describe($0) } ?? "none")")
            }
            pendingVideo = try videoSamples.next()
            var frameDuration = CMSampleBufferGetDuration(sample)
            if !frameDuration.isNumeric || frameDuration <= .zero {
                frameDuration = pendingVideo.map { CMSampleBufferGetPresentationTimeStamp($0)-sourcePTS } ?? (videoRange.end-sourcePTS)
            }
            guard frameDuration.isNumeric, frameDuration > .zero else {
                throw NativeFailure("error.timing", "frame=\(count), duration=\(NativeSamples.describe(frameDuration)), sourcePTS=\(NativeSamples.describe(sourcePTS)), trackEnd=\(NativeSamples.describe(videoRange.end))")
            }
            let outputPixels = try await NativeSamples.pooledBuffer(outputPool,writer:writer)
            var gpuSucceeded = false
            if let gpu = metal {
                do {
                    _ = try await gpu.submit(inputPixelBuffer:pixels,outputPixelBuffer:outputPixels,options:options,fullRange:layout.fullRange,centeredChroma:layout.centeredChroma).wait()
                    if !qualified, let reference = cpuReference {
                        try V210Converter.convert(input:pixels,output:reference,options:options,fullRange:layout.fullRange,centeredChroma:layout.centeredChroma)
                        guard try Self.compare(reference,outputPixels) <= 2 else { throw NativeFailure("error.gpuPrecision") }
                        qualified = true
                        cpuReference = nil
                    }
                    gpuSucceeded = true
                } catch {
                    if error is CancellationError { throw error }
                    if options.backend == .metal { throw error }
                    metal = nil; cpuReference = nil; warnings.append(L10n.text("warning.metal"))
                }
            }
            if !gpuSucceeded { try V210Converter.convert(input:pixels,output:outputPixels,options:options,fullRange:layout.fullRange,centeredChroma:layout.centeredChroma) }
            let output: CMSampleBuffer
            if let encoder {
                output = try encoder.encode(outputPixels,pts:pts,duration:frameDuration)
            } else { output = try NativeSamples.raw(outputPixels,pts:pts,duration:frameDuration) }
            try await NativeSamples.ready(videoInput,writer:writer)
            guard videoInput.append(output) else { throw NativeFailure("error.writer",writer.error?.localizedDescription ?? "") }
            if first == nil { first = pts }
            count += 1; previous = pts; videoEnd = pts+frameDuration
            if pendingVideo == nil { videoInput.markAsFinished(); videoFinished = true }
            timestamps.append(pts); durations.append(frameDuration)
            progress(min(0.94,max(0,videoEnd.seconds / (duration-offset).seconds * 0.94)),count)

        }
        guard count > 0, reader.status == .completed else { throw NativeFailure("error.reader",reader.error?.localizedDescription ?? "") }
        if !videoFinished { videoInput.markAsFinished() }; for i in pairs.indices where !finishedTracks[i] { pairs[i].1.markAsFinished() }
        await NativeSamples.finish(writer)
        try Task.checkCancellation()
        guard writer.status == .completed else { throw NativeFailure("error.writer",writer.error?.localizedDescription ?? "") }
        try AppleLogMOV.process(url:partial,patch:true)
        progress(0.96,count)
        let actualDuration = (videoEnd-(first ?? .zero)).seconds
        var validation = try await OutputValidator().validate(outputURL:partial,expectedFrameCount:count,expectedDuration:actualDuration,
            expectedPTS:timestamps,expectedDurations:durations,expectedCodec:quality.codec,forceSoftwareDecoding:softwareDecoding)
        guard validation.passed else { throw NativeFailure("error.validation", String(describing:validation)) }
        let retained = Set(trackInputs.keys.filter { $0 != contract.track.trackID }.map { UInt32(bitPattern:$0) })
        let restoredTrackTables = try await TrackIntegrity.preserve(source:asset,outputURL:partial,offset:offset,
            videoTrackID:UInt32(bitPattern:contract.track.trackID),retainedTrackIDs:retained)
        if restoredTrackTables {
            try AppleLogMOV.process(url:partial,patch:false)
            validation = try await OutputValidator().validate(outputURL:partial,expectedFrameCount:count,expectedDuration:actualDuration,
                expectedPTS:timestamps,expectedDurations:durations,expectedCodec:quality.codec,forceSoftwareDecoding:softwareDecoding)
            guard validation.passed else { throw NativeFailure("error.validation",String(describing:validation)) }
        }
        try Task.checkCancellation()
        let base = asset.url.deletingPathExtension().lastPathComponent + "_AppleLog_" + (quality == .proRes422HQ ? "ProRes422HQ" : "ProRes422")
        var suffix = 0, destination = directory.appendingPathComponent(base+".mov")
        while fm.fileExists(atPath:destination.path) || fm.fileExists(atPath:destination.appendingPathExtension("json").path) { suffix += 1; destination = directory.appendingPathComponent(base+"_\(suffix).mov") }
        let rate = Double(try await contract.track.load(.nominalFrameRate))
        let cfr = timestamps.count < 3 || zip(timestamps.dropFirst(),timestamps).allSatisfy { abs(($0-$1).seconds-durations[0].seconds) < 0.0005 }
        warnings.append(L10n.text(softwareDecoding ? "decoding.software" : "decoding.system"))
        warnings.append(L10n.text(software ? "encoding.software" : "encoding.system"))
        warnings.append(L10n.text(metal == nil ? "backend.cpu" : "backend.metal"))
        let diagnostics = VideoDiagnostics(nominalFrameRate:rate,detectedFrameRate:VideoProcessor.detectRationalFrameRate(rate),isLikelyCFR:cfr,
            colorPrimaries:String(describing:kCMFormatDescriptionColorPrimaries_ITU_R_2020),
            transferFunction:String(describing:kCMFormatDescriptionTransferFunction_ITU_R_2100_HLG),
            yCbCrMatrix:String(describing:kCMFormatDescriptionYCbCrMatrix_ITU_R_2020),colorInterpretation:.bt2020HLG,warnings:warnings,processingBackend:metal == nil ? .cpu : .metal,encodingBackend:software ? "prores_ks" : "AVFoundation",decodingBackend:softwareDecoding ? "prores" : "AVFoundation")
        let result = VideoProcessingResult(outputURL:destination,frameCount:count,duration:validation.duration,diagnostics:diagnostics)
        let report: [String:Any] = ["frames":count,"duration":validation.duration,"warnings":warnings,"validated":true,
            "appVersion":AppBuild.version,"bundleBuild":AppBuild.buildNumber,"restoredOriginalTrackTables":restoredTrackTables,
            "decoding":softwareDecoding ? "prores" : "AVFoundation","validationDecoding":validation.decodingBackend,
            "encoding":software ? "prores_ks" : "AVFoundation","color":metal == nil ? "CPU" : "Metal",
            "source":asset.url.lastPathComponent,"profile":quality.rawValue,"commonTimingOffsetSeconds":offset.seconds,
            "movieTimeScale":writer.movieTimeScale,"sourceTrackTimeScales":movieScales,
            "softwareCodecVersion":String(cString:LFCodecVersion()),
            "options":try JSONSerialization.jsonObject(with:JSONEncoder().encode(options))]
        try JSONSerialization.data(withJSONObject:report,options:[.prettyPrinted,.sortedKeys]).write(to:partialReport,options:.atomic)
        let reportURL = destination.appendingPathExtension("json")
        try fm.moveItem(at:partialReport,to:reportURL)
        do { try fm.moveItem(at:partial,to:destination); published = true }
        catch { try? fm.removeItem(at:reportURL); throw error }
        progress(1,count); return result
    }
    private static func compare(_ a: CVPixelBuffer, _ b: CVPixelBuffer) throws -> Int {
        guard CVPixelBufferLockBaseAddress(a,.readOnly) == kCVReturnSuccess else { throw NativeFailure("error.memory") }
        defer { CVPixelBufferUnlockBaseAddress(a,.readOnly) }
        guard CVPixelBufferLockBaseAddress(b,.readOnly) == kCVReturnSuccess else { throw NativeFailure("error.memory") }
        defer { CVPixelBufferUnlockBaseAddress(b,.readOnly) }
        guard let pa = CVPixelBufferGetBaseAddress(a), let pb = CVPixelBufferGetBaseAddress(b) else { throw NativeFailure("error.memory") }
        var error = 0
        let w = CVPixelBufferGetWidth(a), h = CVPixelBufferGetHeight(a)
        for y in 0..<h {
            let aa = pa.advanced(by:y*CVPixelBufferGetBytesPerRow(a)).assumingMemoryBound(to:UInt32.self)
            let bb = pb.advanced(by:y*CVPixelBufferGetBytesPerRow(b)).assumingMemoryBound(to:UInt32.self)
            for x in 0..<((w+5)/6)*4 { for shift in [0,10,20] { error = max(error,abs(Int((aa[x]>>shift)&1023)-Int((bb[x]>>shift)&1023))) } }
        }
        return error
    }
}

enum TrackIntegrity {
    static func preserve(source: AVURLAsset, outputURL: URL, offset: CMTime, videoTrackID: UInt32,
                         retainedTrackIDs: Set<UInt32>) async throws -> Bool {
        do { try await validate(source:source,output:AVURLAsset(url:outputURL),offset:offset); return false }
        catch let failure as NativeFailure {
            // Passthrough can change AAC preroll/edits or timecode duration.
            // Restoration must still pass the original strict validator.
            guard failure.key == "error.trackCopy", offset == .zero else { throw failure }
            try PreservedTracksMOV.restore(sourceURL:source.url,outputURL:outputURL,
                videoTrackID:videoTrackID,retainedTrackIDs:retainedTrackIDs)
            try await validate(source:source,output:AVURLAsset(url:outputURL),offset:offset)
            return true
        }
    }
    struct Interval: Equatable { var start: CMTime; var end: CMTime }
    struct Signature: Equatable {
        let media: AVMediaType
        let codec: FourCharCode
        let digest: Data
        let sampleCount: Int
        let intervals: [Interval]
        let audioFormat: [Double]
        let layout: Data
        let language: String?
    }
    static func signatures(_ asset: AVAsset, offset: CMTime) async throws -> [Signature] {
        let tracks = try await asset.load(.tracks)
        let videos = try await asset.loadTracks(withMediaType:.video)
        let chapters = try await videos.first?.loadAssociatedTracks(ofType:.chapterList) ?? []
        let chapterIDs = Set(chapters.map { $0.trackID })
        var result: [Signature] = []
        for track in tracks where [.audio,.timecode].contains(track.mediaType) || chapterIDs.contains(track.trackID) {
            let reader = try AVAssetReader(asset:asset), output = AVAssetReaderTrackOutput(track:track,outputSettings:nil)
            let context = "track=\(track.trackID), media=\(track.mediaType.rawValue)"
            guard reader.canAdd(output) else { throw NativeFailure("error.trackCopy", "\(context): cannot add reader output") }; reader.add(output)
            guard reader.startReading() else { throw NativeFailure("error.trackCopy", "\(context): startReading, \(String(reflecting:reader.error))") }
            defer { reader.cancelReading() }
            var hash = SHA256(), intervals: [Interval] = [], sampleCount = 0
            while let sample = try NativeSamples.nextMediaSample(output) {
                try Task.checkCancellation()
                guard let block = CMSampleBufferGetDataBuffer(sample) else { throw NativeFailure("error.trackCopy", "\(context): missing data buffer, samples=\(CMSampleBufferGetNumSamples(sample))") }
                let length = CMBlockBufferGetDataLength(block)
                guard length > 0 else { throw NativeFailure("error.trackCopy", "\(context): empty data buffer") }
                var data = Data(count:length)
                let status = data.withUnsafeMutableBytes { CMBlockBufferCopyDataBytes(block,atOffset:0,dataLength:length,destination:$0.baseAddress!) }
                guard status == noErr else { throw NativeFailure("error.trackCopy", "\(context): CMBlockBufferCopyDataBytes=\(status)") }
                hash.update(data:data); sampleCount += CMSampleBufferGetNumSamples(sample)
                let pts = CMSampleBufferGetPresentationTimeStamp(sample)-offset, duration = CMSampleBufferGetDuration(sample)
                guard pts.isNumeric, duration.isNumeric, duration >= .zero else {
                    throw NativeFailure("error.trackCopy", "\(context): samples=\(CMSampleBufferGetNumSamples(sample)), PTS=\(NativeSamples.describe(pts)), duration=\(NativeSamples.describe(duration))")
                }
                let interval = Interval(start:pts,end:pts+duration)
                if let last = intervals.last, abs((last.end-pts).seconds) <= 0.000002 { intervals[intervals.count-1].end = interval.end }
                else { intervals.append(interval) }
            }
            guard reader.status == .completed else {
                throw NativeFailure("error.trackCopy", "\(context): reader status=\(reader.status.rawValue), error=\(String(reflecting:reader.error)), samples=\(sampleCount)")
            }
            guard let d = try await track.load(.formatDescriptions).first else { throw NativeFailure("error.trackCopy", "\(context): missing format description") }
            var audio: [Double] = [], layout = Data()
            if track.mediaType == .audio {
                guard let pointer = CMAudioFormatDescriptionGetStreamBasicDescription(d) else { throw NativeFailure("error.trackCopy", "\(context): missing audio ASBD, codec=\(CMFormatDescriptionGetMediaSubType(d))") }
                let f = pointer.pointee
                audio = [f.mSampleRate,Double(f.mFormatID),Double(f.mFormatFlags),Double(f.mBytesPerPacket),Double(f.mFramesPerPacket),Double(f.mBytesPerFrame),Double(f.mChannelsPerFrame),Double(f.mBitsPerChannel)]
                var length = 0
                if let pointer = CMAudioFormatDescriptionGetChannelLayout(d,sizeOut:&length) { layout = Data(bytes:pointer,count:length) }
            }
            result.append(Signature(media:track.mediaType,codec:CMFormatDescriptionGetMediaSubType(d),digest:Data(hash.finalize()),sampleCount:sampleCount,intervals:intervals,audioFormat:audio,layout:layout,language:try await track.load(.extendedLanguageTag)))
        }
        return result
    }
    static func validate(source: AVAsset, output: AVAsset, offset: CMTime) async throws {
        let a = try await signatures(source,offset:offset), b = try await signatures(output,offset:.zero)
        guard a == b else {
            throw NativeFailure("error.trackCopy", "source=\(String(reflecting:a)), output=\(String(reflecting:b))")
        }
    }
}
