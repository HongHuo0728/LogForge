import XCTest
import AVFoundation
import CoreVideo
import AudioToolbox
import VideoToolbox
@testable import LogForge_For_iPhone

final class NativePipelineTests: XCTestCase {
    func testLegacyQueueEntryKeepsItsAttemptIdentity() throws {
        let legacy = Data(#"{"id":"BA68BB84-165E-4C0D-A16B-255596F62A8B","source":"file:///tmp/old.mov","sourceKey":"old","status":"failed","detail":"old failure","info":"old info","attemptBuild":"1.0 (19)"}"#.utf8)
        let entry = try JSONDecoder().decode(QueueEntry.self,from:legacy)
        XCTAssertEqual(entry.attemptBuild,"1.0 (19)")
        XCTAssertNil(entry.attemptVersion)
        XCTAssertNil(entry.attemptReleaseBuild)
        XCTAssertNil(entry.attemptBundleBuild)
        let restored = try JSONDecoder().decode(QueueEntry.self,from:JSONEncoder().encode(entry))
        XCTAssertEqual(restored.attemptBuild,"1.0 (19)")
        XCTAssertEqual(restored.detail,"old failure")
    }
    func testHLGReferenceAndNegativeExtension() {
        XCTAssertEqual(ReferenceColor.inverseHLG(0),0)
        XCTAssertEqual(ReferenceColor.inverseHLG(0.5),1.0/12,accuracy:1e-12)
        XCTAssertEqual(ReferenceColor.inverseHLG(-0.2),-0.04/3,accuracy:1e-12)
        XCTAssertEqual(ReferenceColor.inverseHLG(0.75)*ReferenceColor.scale,1,accuracy:1e-12)
        XCTAssertGreaterThan(ReferenceColor.inverseHLG(1.2),1)
        XCTAssertEqual(ReferenceColor.appleLog(-0.1),0)
        XCTAssertEqual(ReferenceColor.appleLog(1),0.6945529830551911,accuracy:1e-10)
    }
    func testExposureAndCreativeDisabled() {
        let rgb = SIMD3<Double>(0.25,0.5,0.75)
        let original = ReferenceColor.transform(rgb,options:ConversionOptions())
        var options = ConversionOptions(); options.shadow = 0; options.highlight = 3; options.saturation = 0
        XCTAssertEqual(original,ReferenceColor.transform(rgb,options:options))
        options.exposure = 1
        XCTAssertGreaterThan(ReferenceColor.transform(rgb,options:options).z,original.z)
        options.creativeEnabled = true
        let gray = ReferenceColor.transform(rgb,options:options)
        XCTAssertEqual(gray.x,gray.y,accuracy:1e-12); XCTAssertEqual(gray.y,gray.z,accuracy:1e-12)
    }
    func testSplinePartitionOfUnityAndPhase() {
        for phase in [0.0,0.25,0.5,0.75] {
            XCTAssertEqual((-2...3).reduce(0.0) { $0+ReferenceColor.spline(phase-Double($1)) },1,accuracy:1e-12)
        }
        XCTAssertEqual(ReferenceColor.spline(0),1)
        XCTAssertEqual(ReferenceColor.spline(1),0)
        XCTAssertEqual(ReferenceColor.spline(3),0)
    }
    private func fill(_ pixels: CVPixelBuffer, y: UInt32) throws {
        XCTAssertEqual(CVPixelBufferLockBaseAddress(pixels,[]),kCVReturnSuccess)
        defer { CVPixelBufferUnlockBaseAddress(pixels,[]) }
        let base = try XCTUnwrap(CVPixelBufferGetBaseAddress(pixels)), stride = CVPixelBufferGetBytesPerRow(pixels)
        memset(base,0,CVPixelBufferGetHeight(pixels)*stride)
        for row in 0..<CVPixelBufferGetHeight(pixels) {
            let q = base.advanced(by:row*stride).assumingMemoryBound(to:UInt32.self)
            for g in 0..<((CVPixelBufferGetWidth(pixels)+5)/6) {
                q[g*4]=512|y<<10|512<<20; q[g*4+1]=y|512<<10|y<<20
                q[g*4+2]=512|y<<10|512<<20; q[g*4+3]=y|512<<10|y<<20
            }
        }
    }
    func testCPUV210TailsRangeAndPadding() throws {
        for width in [2,6,8,14] {
            for full in [false,true] {
                let input = try NativeSamples.buffer(width:width,height:2), output = try NativeSamples.buffer(width:width,height:2)
                let y: UInt32 = full ? 768 : 721
                try fill(input,y:y)
                for centered in [false,true] {
                    try V210Converter.convert(input:input,output:output,options:ConversionOptions(),fullRange:full,centeredChroma:centered)
                    XCTAssertEqual(CVPixelBufferLockBaseAddress(output,.readOnly),kCVReturnSuccess)
                    defer { CVPixelBufferUnlockBaseAddress(output,.readOnly) }
                    let base = try XCTUnwrap(CVPixelBufferGetBaseAddress(output)), stride = CVPixelBufferGetBytesPerRow(output)
                    let value = ReferenceColor.appleLog(ReferenceColor.inverseHLG((Double(y)-(full ? 0:64))/(full ? 1023:876))*ReferenceColor.scale)
                    let expected = UInt32((value*876+64).rounded())
                    for row in 0..<2 {
                        let q = base.advanced(by:row*stride).assumingMemoryBound(to:UInt32.self)
                        for g in 0..<((width+5)/6) {
                            XCTAssertEqual(q[g*4]&1023,512); XCTAssertEqual((q[g*4]>>10)&1023,expected)
                            XCTAssertEqual((q[g*4+3]>>20)&1023,expected)
                            XCTAssertEqual(q[g*4]>>30,0)
                        }
                    }
                }
            }
        }
    }
    func testMetalFloatQualification() async throws {
        guard MetalColorProcessor.isAvailable else { throw XCTSkip("Metal unavailable on this test destination") }
        let gpu = try MetalColorProcessor()
        for ev: Float in [-4,0,4] {
            var options = ConversionOptions(); options.exposure = ev
            try await gpu.qualify(options:options)
            options.creativeEnabled = true
            try await gpu.qualify(options:options)
        }
    }
    func testSoftwareProResProfilesAndDuration() throws {
        var firstPackets: [Data] = []
        for profile: Int32 in [2,3] {
            let encoder = try SoftwareProResEncoder(width:8,height:2,quality:profile == 3 ? .proRes422HQ : .proRes422)
            let pixels = try NativeSamples.buffer(width:8,height:2); try fill(pixels,y:721)
            for i in 0..<5 {
                // Every submitted frame, including the last, must return a packet
                // immediately. This catches accidental delayed frame threading.
                let duration = CMTime(value:Int64(1001+i*37),timescale:30000), pts = CMTime(value:Int64(9000+i*1100),timescale:30000)
                try fill(pixels,y:UInt32(64+i*150))
                let sample = try encoder.encode(pixels,pts:pts,duration:duration)
                if i == 0 {
                    let block = try XCTUnwrap(CMSampleBufferGetDataBuffer(sample))
                    var packet = Data(count:CMBlockBufferGetDataLength(block))
                    let status = packet.withUnsafeMutableBytes {
                        CMBlockBufferCopyDataBytes(block,atOffset:0,dataLength:$0.count,destination:$0.baseAddress!)
                    }
                    XCTAssertEqual(status,noErr); firstPackets.append(packet)
                }
                XCTAssertEqual(CMSampleBufferGetPresentationTimeStamp(sample),pts)
                XCTAssertEqual(CMSampleBufferGetDuration(sample),duration)
                let description = try XCTUnwrap(CMSampleBufferGetFormatDescription(sample))
                XCTAssertEqual(CMFormatDescriptionGetMediaSubType(description),profile == 3 ? kCMVideoCodecType_AppleProRes422HQ:kCMVideoCodecType_AppleProRes422)
                XCTAssertGreaterThan(CMSampleBufferGetTotalSampleSize(sample),0)
            }
        }
        XCTAssertEqual(firstPackets.count,2)
        XCTAssertNotEqual(firstPackets[0],firstPackets[1],"Standard and HQ must use distinct encoder profiles, not just distinct MOV labels")
    }
    func testPoolCannotRecycleImagesRetainedBySubmittedSamples() throws {
        let pool = try NativeSamples.pool(width:8,height:2)
        var retained: [CMSampleBuffer] = []
        for i in 0..<4 {
            let pixels = try XCTUnwrap(NativeSamples.pooledBuffer(pool))
            try fill(pixels,y:UInt32(64+i*150))
            retained.append(try NativeSamples.raw(pixels,pts:CMTime(value:Int64(i),timescale:30),duration:CMTime(value:1,timescale:30)))
        }
        XCTAssertNil(try NativeSamples.pooledBuffer(pool))
        for i in retained.indices {
            let pixels = try XCTUnwrap(CMSampleBufferGetImageBuffer(retained[i]))
            XCTAssertEqual(CVPixelBufferLockBaseAddress(pixels,.readOnly),kCVReturnSuccess)
            defer { CVPixelBufferUnlockBaseAddress(pixels,.readOnly) }
            let word = try XCTUnwrap(CVPixelBufferGetBaseAddress(pixels)).assumingMemoryBound(to:UInt32.self).pointee
            XCTAssertEqual((word>>10)&1023,UInt32(64+i*150))
        }
        retained.removeAll()
        XCTAssertNotNil(try NativeSamples.pooledBuffer(pool))
    }
    private func movie(codec: String = "apch", logs: String? = nil) throws -> Data {
        var entry = Data(repeating:0,count:78)
        entry.append(try AppleLogMOV.atom("colr",Data([110,99,108,99,0,9,0,2,0,9])))
        if let logs { entry.append(try AppleLogMOV.atom("logs",Data(logs.utf8))) }
        var stsd = Data([0,0,0,0,0,0,0,1]); stsd.append(try AppleLogMOV.atom(codec,entry))
        var nested = try AppleLogMOV.atom("stsd",stsd)
        for type in ["stbl","minf","mdia","trak","moov"] { nested = try AppleLogMOV.atom(type,nested) }
        var file = try AppleLogMOV.atom("mdat",Data(repeating:42,count:128)); file.append(nested); return file
    }
    private func temporary(_ data: Data) throws -> URL {
        let url = FileManager.default.temporaryDirectory.appendingPathComponent("test-\(UUID()).mov")
        try data.write(to:url); addTeardownBlock { try? FileManager.default.removeItem(at:url) }; return url
    }
    @MainActor
    func testQueueStartupPreservesActiveExportAndRemovesAbandoned() throws {
        let fm = FileManager.default
        let directory = fm.urls(for:.documentDirectory,in:.userDomainMask)[0].appendingPathComponent("Exports",isDirectory:true)
        try fm.createDirectory(at:directory,withIntermediateDirectories:true)
        let active = directory.appendingPathComponent(".LogForge-\(UUID()).partial.mov")
        let report = active.appendingPathExtension("json")
        let abandoned = directory.appendingPathComponent(".LogForge-\(UUID()).partial.mov")
        NativeExportFiles.shared.register([active,report])
        defer {
            NativeExportFiles.shared.release([active,report])
            for url in [active,report,abandoned] { try? fm.removeItem(at:url) }
        }
        for url in [active,report,abandoned] { try Data([1,2,3]).write(to:url) }
        _ = ConversionQueue()
        XCTAssertTrue(fm.fileExists(atPath:active.path))
        XCTAssertTrue(fm.fileExists(atPath:report.path))
        XCTAssertFalse(fm.fileExists(atPath:abandoned.path))
    }
    func testAppleLogAtomBothProfilesIdempotentAndMediaUnchanged() throws {
        for codec in ["apch","apcn"] {
            let original = try movie(codec:codec), url = try temporary(original)
            XCTAssertThrowsError(try AppleLogMOV.process(url:url,patch:false))
            try AppleLogMOV.process(url:url,patch:true)
            let once = try Data(contentsOf:url)
            XCTAssertEqual(once.prefix(136),original.prefix(136))
            XCTAssertEqual(once.count,original.count+35)
            try AppleLogMOV.process(url:url,patch:false)
            try AppleLogMOV.process(url:url,patch:true)
            XCTAssertEqual(try Data(contentsOf:url),once)
        }
    }
    func testRejectConflictingLogAndTruncatedBox() throws {
        let conflict = try temporary(movie(logs:"not.apple.log"))
        XCTAssertThrowsError(try AppleLogMOV.process(url:conflict,patch:true))
        let truncated = try temporary(Data([0,0,0,20,109,111,111,118]))
        XCTAssertThrowsError(try AppleLogMOV.process(url:truncated,patch:true))
    }
    func testSampleTimingCopyKeepsVFRDuration() throws {
        let pixels = try NativeSamples.buffer(width:8,height:2)
        try fill(pixels,y:721)
        let sample = try NativeSamples.raw(pixels,pts:CMTime(value:75,timescale:100),duration:CMTime(value:7,timescale:100))
        let copy = try NativeSamples.retime(sample,offset:CMTime(value:25,timescale:100))
        XCTAssertEqual(CMSampleBufferGetPresentationTimeStamp(copy),CMTime(value:50,timescale:100))
        XCTAssertEqual(CMSampleBufferGetDuration(copy),CMTime(value:7,timescale:100))
    }
    func testExactTimeScalePreservesNTSCAndAudioTicks() throws {
        XCTAssertEqual(try NativeSamples.exactScale([30000,48000,600]),240000)
        XCTAssertThrowsError(try NativeSamples.exactScale([0]))
        XCTAssertThrowsError(try NativeSamples.exactScale([Int32.max,Int32.max-1]))
    }
    func testZeroAndIntegralTrackStartsDoNotRequireNanosecondMediaClock() throws {
        XCTAssertEqual(try NativeSamples.requiredScale(CMTime(value:0,timescale:1_000_000_000)),1)
        XCTAssertEqual(try NativeSamples.requiredScale(CMTime(value:-2_000_000_000,timescale:1_000_000_000)),1)
        XCTAssertEqual(try NativeSamples.requiredScale(CMTime(value:10_000_000,timescale:1_000_000_000)),100)
        XCTAssertEqual(try NativeSamples.mediaScale(30000,offset:CMTime(value:0,timescale:1_000_000_000)),30000)
        XCTAssertEqual(try NativeSamples.movieScale([30000,48000],starts:[CMTime(value:0,timescale:1_000_000_000)]),240000)
        XCTAssertThrowsError(try NativeSamples.requiredScale(.invalid))
    }
    func testNanosecondVideoAnd48kHzAudioDoNotOverflowMovieClock() throws {
        XCTAssertThrowsError(try NativeSamples.exactScale([1_000_000_000,48000]))
        XCTAssertEqual(try NativeSamples.movieScale([1_000_000_000,48000],starts:[.zero]),1_000_000_000)
        XCTAssertEqual(try NativeSamples.mediaScale(1_000_000_000,offset:.zero),1_000_000_000)
        // A nonzero, reducible common offset must still preserve exact ticks.
        XCTAssertEqual(try NativeSamples.mediaScale(30000,offset:CMTime(value:-10_000_000,timescale:1_000_000_000)),30000)
        XCTAssertThrowsError(try NativeSamples.movieScale([0],starts:[.zero]))
    }
    private func hlgFixture(quality: ProResQuality, width: Int = 64, height: Int = 32,
                            videoScale: CMTimeScale = 30000, movieScale: CMTimeScale = 240000,
                            startTicks: Int64 = 301, frameTicks: [Int64] = [1001,2002,1001]) async throws -> URL {
        let url = FileManager.default.temporaryDirectory.appendingPathComponent("HLG-\(UUID()).mov")
        addTeardownBlock { try? FileManager.default.removeItem(at:url) }
        let extensions: [String:Any] = [kCMFormatDescriptionExtension_ColorPrimaries as String:kCMFormatDescriptionColorPrimaries_ITU_R_2020,
            kCMFormatDescriptionExtension_TransferFunction as String:kCMFormatDescriptionTransferFunction_ITU_R_2100_HLG,
            kCMFormatDescriptionExtension_YCbCrMatrix as String:kCMFormatDescriptionYCbCrMatrix_ITU_R_2020,
            kCMFormatDescriptionExtension_ChromaLocationTopField as String:kCMFormatDescriptionChromaLocation_Left,
            kCMFormatDescriptionExtension_FullRangeVideo as String:false,
            kCMFormatDescriptionExtension_BitsPerComponent as String:10]
        var description: CMVideoFormatDescription?
        XCTAssertEqual(CMVideoFormatDescriptionCreate(allocator:nil,codecType:quality == .proRes422HQ ? kCMVideoCodecType_AppleProRes422HQ:kCMVideoCodecType_AppleProRes422,width:Int32(width),height:Int32(height),extensions:extensions as CFDictionary,formatDescriptionOut:&description),noErr)
        let format = try XCTUnwrap(description)
        let writer = try AVAssetWriter(outputURL:url,fileType:.mov)
        defer { if writer.status == .writing { writer.cancelWriting() } }
        writer.shouldOptimizeForNetworkUse = false; writer.movieTimeScale = movieScale
        let input = AVAssetWriterInput(mediaType:.video,outputSettings:nil,sourceFormatHint:format)
        input.mediaTimeScale = videoScale
        input.transform = CGAffineTransform(a:0,b:1,c:-1,d:0,tx:CGFloat(height),ty:0)
        XCTAssertTrue(writer.canAdd(input)); writer.add(input)
        var asbd = AudioStreamBasicDescription(mSampleRate:48000,mFormatID:kAudioFormatLinearPCM,
            mFormatFlags:kAudioFormatFlagIsSignedInteger|kAudioFormatFlagIsPacked,mBytesPerPacket:4,mFramesPerPacket:1,mBytesPerFrame:4,mChannelsPerFrame:2,mBitsPerChannel:16,mReserved:0)
        var audioDescription: CMAudioFormatDescription?
        XCTAssertEqual(CMAudioFormatDescriptionCreate(allocator:nil,asbd:&asbd,layoutSize:0,layout:nil,magicCookieSize:0,magicCookie:nil,extensions:nil,formatDescriptionOut:&audioDescription),noErr)
        let audioFormat = try XCTUnwrap(audioDescription)
        let audio = AVAssetWriterInput(mediaType:.audio,outputSettings:nil,sourceFormatHint:audioFormat)
        XCTAssertTrue(writer.canAdd(audio)); writer.add(audio)
        XCTAssertTrue(writer.startWriting()); writer.startSession(atSourceTime:.zero)
        let audioCount = 7200, byteCount = audioCount*4
        var block: CMBlockBuffer?
        XCTAssertEqual(CMBlockBufferCreateWithMemoryBlock(allocator:nil,memoryBlock:nil,blockLength:byteCount,blockAllocator:nil,customBlockSource:nil,offsetToData:0,dataLength:byteCount,flags:0,blockBufferOut:&block),noErr)
        let data = try XCTUnwrap(block)
        XCTAssertEqual(CMBlockBufferFillDataBytes(with:0,blockBuffer:data,offsetIntoDestination:0,dataLength:byteCount),noErr)
        var audioTiming = CMSampleTimingInfo(duration:CMTime(value:1,timescale:48000),presentationTimeStamp:.zero,decodeTimeStamp:.invalid)
        var audioSize = 4, audioSample: CMSampleBuffer?
        XCTAssertEqual(CMSampleBufferCreateReady(allocator:nil,dataBuffer:data,formatDescription:audioFormat,sampleCount:audioCount,sampleTimingEntryCount:1,sampleTimingArray:&audioTiming,sampleSizeEntryCount:1,sampleSizeArray:&audioSize,sampleBufferOut:&audioSample),noErr)
        try await NativeSamples.ready(audio,writer:writer)
        XCTAssertTrue(audio.append(try XCTUnwrap(audioSample))); audio.markAsFinished()
        let encoder = try SoftwareProResEncoder(width:width,height:height,quality:quality)
        let pixels = try NativeSamples.buffer(width:width,height:height)
        var pts = CMTime(value:startTicks,timescale:videoScale)
        for (i,ticks) in frameTicks.enumerated() {
            try fill(pixels,y:UInt32(300+(i%3)*200))
            let duration = CMTime(value:ticks,timescale:videoScale)
            let encoded = try encoder.encode(pixels,pts:pts,duration:duration)
            var timing = CMSampleTimingInfo(duration:duration,presentationTimeStamp:pts,decodeTimeStamp:.invalid)
            var size = CMSampleBufferGetTotalSampleSize(encoded), sample: CMSampleBuffer?
            // Keep the compressed ProRes payload, replacing only the fixture's
            // format description with BT.2020 HLG, as an actual input MOV.
            XCTAssertEqual(CMSampleBufferCreateReady(allocator:nil,dataBuffer:CMSampleBufferGetDataBuffer(encoded),formatDescription:format,sampleCount:1,sampleTimingEntryCount:1,sampleTimingArray:&timing,sampleSizeEntryCount:1,sampleSizeArray:&size,sampleBufferOut:&sample),noErr)
            try await NativeSamples.ready(input,writer:writer)
            XCTAssertTrue(input.append(try XCTUnwrap(sample))); pts = pts+duration
        }
        input.markAsFinished(); await NativeSamples.finish(writer)
        XCTAssertEqual(writer.status,.completed,writer.error?.localizedDescription ?? "")
        return url
    }
    func test4K30ProResNanosecondClockAndAudioSurviveMOVWriting() async throws {
        // This regression deliberately uses compressed samples so it runs even
        // on simulators without a ProRes decoder. It tests the actual MOV clock,
        // writer and reader; the full color-conversion test remains separate.
        let source = try await hlgFixture(quality:.proRes422HQ,width:3840,height:2160,
            videoScale:1_000_000_000,movieScale:1_000_000_000,startTicks:0,
            frameTicks:[33_333_333,33_333_333,33_333_334])
        let asset = AVURLAsset(url:source), contract = try await InputContract.inspect(asset)
        XCTAssertEqual(contract.width,3840); XCTAssertEqual(contract.height,2160)
        let destination = FileManager.default.temporaryDirectory.appendingPathComponent("Clock-\(UUID()).mov")
        addTeardownBlock { try? FileManager.default.removeItem(at:destination) }
        let reader = try AVAssetReader(asset:asset), writer = try AVAssetWriter(outputURL:destination,fileType:.mov)
        defer { reader.cancelReading(); if writer.status == .writing { writer.cancelWriting() } }
        let tracks = try await asset.load(.tracks)
        var pairs: [(AVAssetReaderTrackOutput,AVAssetWriterInput)] = []
        var scales: [CMTimeScale] = [], starts: [CMTime] = []
        for track in tracks where [.video,.audio].contains(track.mediaType) {
            let scale = try await track.load(.naturalTimeScale)
            let range = try await track.load(.timeRange)
            scales.append(scale); starts.append(range.start)
            let formats = try await track.load(.formatDescriptions)
            let format = try XCTUnwrap(formats.first)
            let output = AVAssetReaderTrackOutput(track:track,outputSettings:nil)
            let input = AVAssetWriterInput(mediaType:track.mediaType,outputSettings:nil,sourceFormatHint:format)
            // Match production passthrough, including its language metadata.
            input.languageCode = try await track.load(.languageCode)
            input.extendedLanguageTag = try await track.load(.extendedLanguageTag)
            input.metadata = (try await track.load(.metadata)).filter {
                let id = ($0.identifier?.rawValue ?? "").lowercased()
                return id.contains("language") || id.contains("timecode")
            }
            if track.mediaType == .video {
                input.mediaTimeScale = try NativeSamples.mediaScale(scale,offset:.zero)
                input.transform = try await track.load(.preferredTransform)
            }
            XCTAssertTrue(reader.canAdd(output)); XCTAssertTrue(writer.canAdd(input))
            reader.add(output); writer.add(input); pairs.append((output,input))
        }
        XCTAssertTrue(scales.contains(1_000_000_000)); XCTAssertTrue(scales.contains(48000))
        writer.shouldOptimizeForNetworkUse = false
        writer.movieTimeScale = try NativeSamples.movieScale(scales,starts:starts)
        XCTAssertEqual(writer.movieTimeScale,1_000_000_000)
        XCTAssertTrue(writer.startWriting()); writer.startSession(atSourceTime:.zero)
        XCTAssertTrue(reader.startReading())
        var pending = try pairs.map { try NativeSamples.nextMediaSample($0.0) }
        for i in pending.indices where pending[i] == nil { pairs[i].1.markAsFinished() }
        while let i = pending.indices.filter({ pending[$0] != nil }).min(by:{
            CMSampleBufferGetPresentationTimeStamp(pending[$0]!) < CMSampleBufferGetPresentationTimeStamp(pending[$1]!)
        }) {
            try await NativeSamples.ready(pairs[i].1,writer:writer)
            XCTAssertTrue(pairs[i].1.append(try NativeSamples.retime(try XCTUnwrap(pending[i]),offset:.zero,context:"4K30 passthrough track \(i)")),writer.error?.localizedDescription ?? "")
            pending[i] = try NativeSamples.nextMediaSample(pairs[i].0)
            if pending[i] == nil { pairs[i].1.markAsFinished() }
        }
        XCTAssertEqual(reader.status,.completed)
        await NativeSamples.finish(writer)
        XCTAssertEqual(writer.status,.completed,writer.error?.localizedDescription ?? "")
        let result = AVURLAsset(url:destination)
        let outputTracks = try await result.loadTracks(withMediaType:.video)
        let track = try XCTUnwrap(outputTracks.first)
        let check = try AVAssetReader(asset:result), output = AVAssetReaderTrackOutput(track:track,outputSettings:nil)
        XCTAssertTrue(check.canAdd(output)); check.add(output); XCTAssertTrue(check.startReading())
        defer { check.cancelReading() }
        let expectedPTS: [Int64] = [0,33_333_333,66_666_666]
        let expectedDurations: [Int64] = [33_333_333,33_333_333,33_333_334]
        var count = 0
        while let sample = try NativeSamples.nextMediaSample(output) {
            guard count < expectedPTS.count else { XCTFail("Unexpected additional frame"); break }
            XCTAssertEqual(CMSampleBufferGetPresentationTimeStamp(sample),CMTime(value:expectedPTS[count],timescale:1_000_000_000))
            XCTAssertEqual(CMSampleBufferGetDuration(sample),CMTime(value:expectedDurations[count],timescale:1_000_000_000))
            if count == 0 {
                let decoder = try SoftwareProResDecoder(width:3840,height:2160)
                let pixels = try NativeSamples.buffer(width:3840,height:2160)
                let decoded = try decoder.decode(sample,into:pixels)
                try OutputValidator.validateDecodedFrame(decoded,expectedPTS:.zero,width:3840,height:2160,frame:0)
                XCTAssertEqual(CMSampleBufferGetDuration(decoded),CMTime(value:expectedDurations[0],timescale:1_000_000_000))
            }
            count += 1
        }
        XCTAssertEqual(count,3); XCTAssertEqual(check.status,.completed)
        let stored = try await OutputValidator().inspectStoredFrames(asset:result,track:track,
            expectedPTS:expectedPTS.map { CMTime(value:$0,timescale:1_000_000_000) },
            expectedDurations:expectedDurations.map { CMTime(value:$0,timescale:1_000_000_000) })
        XCTAssertEqual(stored.frameCount,3); XCTAssertEqual(stored.duration,0.1,accuracy:1e-9)
        XCTAssertTrue(stored.ptsContinuous,stored.issues.joined(separator:"\n"))
        let sourceAudio = try await TrackIntegrity.signatures(asset,offset:.zero)
        let outputAudio = try await TrackIntegrity.signatures(result,offset:.zero)
        XCTAssertEqual(sourceAudio,outputAudio,"Audio timing, bytes, format and language must survive passthrough: source=\(String(reflecting:sourceAudio)), output=\(String(reflecting:outputAudio))")

    }
    func testDecodedImagesWithoutDurationUseIndependentStoredTiming() throws {
        let pixels = try NativeSamples.buffer(width:8,height:2)
        try fill(pixels,y:721)
        let pts = CMTime(value:2000,timescale:60000)
        for duration in [CMTime.invalid,CMTime.zero] {
            let sample = try NativeSamples.raw(pixels,pts:pts,duration:duration)
            XCTAssertNoThrow(try OutputValidator.validateDecodedFrame(sample,expectedPTS:pts,width:8,height:2,frame:1))
            XCTAssertThrowsError(try OutputValidator.validateDecodedFrame(sample,expectedPTS:pts+CMTime(value:1,timescale:30),width:8,height:2,frame:1))
            XCTAssertThrowsError(try OutputValidator.validateDecodedFrame(sample,expectedPTS:pts,width:6,height:2,frame:1))
        }
        // Stored timing remains mandatory even when decoded duration is absent.
        for duration in [CMTime.invalid,CMTime.zero,CMTime(value:-1,timescale:30)] {
            var timing = OutputFrameTiming(expectedPTS:[pts],expectedDurations:[CMTime(value:1,timescale:30)])
            timing.append(pts:pts,duration:duration)
            let result = timing.result()
            XCTAssertFalse(result.ptsContinuous); XCTAssertFalse(result.issues.isEmpty)
        }
    }
    func testStoredTimingRejectsDuplicateBackwardAndMismatchedSamples() {
        let tick = CMTime(value:1,timescale:30)
        for pts in [[CMTime.zero,CMTime.zero],[tick,CMTime.zero],[CMTime.zero,CMTime.invalid]] {
            var timing = OutputFrameTiming(expectedPTS:[],expectedDurations:[])
            for time in pts { timing.append(pts:time,duration:tick) }
            XCTAssertFalse(timing.result().ptsContinuous)
        }
        var missing = OutputFrameTiming(expectedPTS:[.zero,tick],expectedDurations:[tick,tick])
        missing.append(pts:.zero,duration:tick)
        XCTAssertFalse(missing.result().ptsContinuous)
        var wrongDuration = OutputFrameTiming(expectedPTS:[.zero],expectedDurations:[tick])
        wrongDuration.append(pts:.zero,duration:tick+tick)
        XCTAssertFalse(wrongDuration.result().ptsContinuous)
    }
    func testStoredMOVValidationFor176FramesAndNonuniformFinalDuration() async throws {
        let scale: CMTimeScale = 60000
        let ticks = [Int64](repeating:2000,count:175)+[3190]
        var cursor: Int64 = 0
        let timestamps = ticks.map { ticks -> CMTime in
            defer { cursor += ticks }
            return CMTime(value:cursor,timescale:scale)
        }
        let durations = ticks.map { CMTime(value:$0,timescale:scale) }
        for quality in ProResQuality.allCases {
            let url = try await hlgFixture(quality:quality,videoScale:scale,movieScale:240000,startTicks:0,frameTicks:ticks)
            let asset = AVURLAsset(url:url), tracks = try await asset.loadTracks(withMediaType:.video)
            let track = try XCTUnwrap(tracks.first)
            let result = try await OutputValidator().inspectStoredFrames(asset:asset,track:track,expectedPTS:timestamps,expectedDurations:durations)
            XCTAssertEqual(result.frameCount,176)
            XCTAssertEqual(result.duration,5.8865,accuracy:1e-9)
            XCTAssertTrue(result.ptsContinuous,result.issues.joined(separator:"\n"))
            // A 1 ms timing discrepancy must still fail the actual MOV verifier.
            var wrong = durations; wrong[175] = wrong[175]+CMTime(value:1,timescale:1000)
            let rejected = try await OutputValidator().inspectStoredFrames(asset:asset,track:track,expectedPTS:timestamps,expectedDurations:wrong)
            XCTAssertFalse(rejected.ptsContinuous); XCTAssertTrue(rejected.issues.contains { $0.contains("duration mismatch") })
        }
    }
    func testZeroOffsetPassthroughRetainsOpaqueTimingAndAttachments() throws {
        // A buffer may have opaque timing. No shift should query or reconstruct
        // it; the pipeline separately checks the buffer PTS before appending.
        var block: CMBlockBuffer?
        XCTAssertEqual(CMBlockBufferCreateWithMemoryBlock(allocator:nil,memoryBlock:nil,blockLength:4,blockAllocator:nil,customBlockSource:nil,offsetToData:0,dataLength:4,flags:0,blockBufferOut:&block),noErr)
        var size = 4, sample: CMSampleBuffer?
        XCTAssertEqual(CMSampleBufferCreateReady(allocator:nil,dataBuffer:try XCTUnwrap(block),formatDescription:nil,sampleCount:1,sampleTimingEntryCount:0,sampleTimingArray:nil,sampleSizeEntryCount:1,sampleSizeArray:&size,sampleBufferOut:&sample),noErr)
        let original = try XCTUnwrap(sample)
        CMSetAttachment(original,key:kCMSampleBufferAttachmentKey_TrimDurationAtStart,value:try XCTUnwrap(CMTimeCopyAsDictionary(CMTime(value:7,timescale:48000),allocator:nil)),attachmentMode:kCMAttachmentMode_ShouldPropagate)
        let result = try NativeSamples.retime(original,offset:.zero)
        XCTAssertTrue(result === original)
        XCTAssertEqual(CMSampleBufferGetNumSamples(result),1)
        XCTAssertTrue(CMSampleBufferGetDataBuffer(result) === CMSampleBufferGetDataBuffer(original))
        XCTAssertNotNil(CMGetAttachment(result,key:kCMSampleBufferAttachmentKey_TrimDurationAtStart,attachmentModeOut:nil))
        XCTAssertThrowsError(try NativeSamples.retime(original,offset:CMTime(value:-1,timescale:48000),context:"audio regression")) { error in
            let detail = error.localizedDescription
            XCTAssertTrue(detail.contains("audio regression")); XCTAssertTrue(detail.contains("timing query status="))
        }
    }
    func testCopiedAudioTimingIsPreservedWithZeroAndNegativeOffsets() async throws {
        let url = try await hlgFixture(quality:.proRes422HQ)
        let asset = AVURLAsset(url:url), tracks = try await asset.loadTracks(withMediaType:.audio)
        let reader = try AVAssetReader(asset:asset)
        defer { reader.cancelReading() }
        let output = AVAssetReaderTrackOutput(track:try XCTUnwrap(tracks.first),outputSettings:nil)
        XCTAssertTrue(reader.canAdd(output)); reader.add(output); XCTAssertTrue(reader.startReading())
        var count = 0
        while let sample = try NativeSamples.nextMediaSample(output) {
            let original = CMSampleBufferGetPresentationTimeStamp(sample)
            XCTAssertTrue(try NativeSamples.retime(sample,offset:.zero) === sample)
            let offset = CMTime(value:-7,timescale:48000)
            let shifted = try NativeSamples.retime(sample,offset:offset)
            XCTAssertEqual(CMSampleBufferGetPresentationTimeStamp(shifted),original-offset)
            XCTAssertEqual(CMSampleBufferGetDuration(shifted),CMSampleBufferGetDuration(sample))
            XCTAssertEqual(CMSampleBufferGetNumSamples(shifted),CMSampleBufferGetNumSamples(sample))
            XCTAssertTrue(CMSampleBufferGetDataBuffer(shifted) === CMSampleBufferGetDataBuffer(sample))
            count += 1
        }
        XCTAssertGreaterThan(count,0); XCTAssertEqual(reader.status,.completed)
    }
    private func firstLuma(_ pixels: CVPixelBuffer) throws -> UInt32 {
        XCTAssertEqual(CVPixelBufferLockBaseAddress(pixels,.readOnly),kCVReturnSuccess)
        defer { CVPixelBufferUnlockBaseAddress(pixels,.readOnly) }
        let base = try XCTUnwrap(CVPixelBufferGetBaseAddress(pixels)).assumingMemoryBound(to:UInt32.self)
        return (base[0]>>10)&1023
    }
    func testSoftwareDecoderBothProfilesV210TailAndIndependentBufferOwnership() throws {
        for quality in ProResQuality.allCases {
            for width in [2,8,14,64,66] {
                let encoder = try SoftwareProResEncoder(width:width,height:2,quality:quality)
                let decoder = try SoftwareProResDecoder(width:width,height:2)
                let source = try NativeSamples.buffer(width:width,height:2)
                let first = try NativeSamples.buffer(width:width,height:2), second = try NativeSamples.buffer(width:width,height:2)
                try fill(source,y:300)
                let packet1 = try encoder.encode(source,pts:CMTime(value:77,timescale:60000),duration:CMTime(value:1001,timescale:30000))
                let sample1 = try decoder.decode(packet1,into:first)
                XCTAssertEqual(CMSampleBufferGetPresentationTimeStamp(sample1),CMSampleBufferGetPresentationTimeStamp(packet1))
                XCTAssertEqual(CMSampleBufferGetDuration(sample1),CMSampleBufferGetDuration(packet1))
                let before = try firstLuma(first)
                XCTAssertEqual(Double(before),300,accuracy:4)
                try fill(source,y:800)
                let packet2 = try encoder.encode(source,pts:CMTime(value:2079,timescale:60000),duration:CMTime(value:1,timescale:30))
                _ = try decoder.decode(packet2,into:second)
                XCTAssertEqual(try firstLuma(first),before,"Decoder-owned AVFrame memory must not overwrite an earlier CVPixelBuffer")
                XCTAssertEqual(Double(try firstLuma(second)),800,accuracy:4)
                XCTAssertEqual(CVPixelBufferLockBaseAddress(second,.readOnly),kCVReturnSuccess)
                defer { CVPixelBufferUnlockBaseAddress(second,.readOnly) }
                let base = try XCTUnwrap(CVPixelBufferGetBaseAddress(second)), stride = CVPixelBufferGetBytesPerRow(second)
                for row in 0..<2 {
                    let q = base.advanced(by:row*stride).assumingMemoryBound(to:UInt32.self)
                    for group in 0..<((width+5)/6) {
                        for word in 0..<4 { XCTAssertEqual(q[group*4+word]>>30,0) }
                    }
                    let used = ((width+5)/6)*16
                    let bytes = base.advanced(by:row*stride).assumingMemoryBound(to:UInt8.self)
                    for i in used..<stride { XCTAssertEqual(bytes[i],0) }
                }
            }
        }
    }
    func testSoftwareDecoderRejectsCorruptPacketInsteadOfReturningPreviousFrame() throws {
        let encoder = try SoftwareProResEncoder(width:8,height:2,quality:.proRes422HQ)
        let decoder = try SoftwareProResDecoder(width:8,height:2)
        let pixels = try NativeSamples.buffer(width:8,height:2); try fill(pixels,y:721)
        let packet = try encoder.encode(pixels,pts:.zero,duration:CMTime(value:1,timescale:30))
        _ = try decoder.decode(packet,into:try NativeSamples.buffer(width:8,height:2))
        let size = CMSampleBufferGetTotalSampleSize(packet)
        var block: CMBlockBuffer?
        XCTAssertEqual(CMBlockBufferCreateWithMemoryBlock(allocator:nil,memoryBlock:nil,blockLength:size,blockAllocator:nil,customBlockSource:nil,offsetToData:0,dataLength:size,flags:0,blockBufferOut:&block),noErr)
        let bytes = try XCTUnwrap(block)
        XCTAssertEqual(CMBlockBufferFillDataBytes(with:0,blockBuffer:bytes,offsetIntoDestination:0,dataLength:size),noErr)
        var timing = CMSampleTimingInfo(duration:CMTime(value:1,timescale:30),presentationTimeStamp:.zero,decodeTimeStamp:.invalid)
        var length = size, sample: CMSampleBuffer?
        XCTAssertEqual(CMSampleBufferCreateReady(allocator:nil,dataBuffer:bytes,formatDescription:CMSampleBufferGetFormatDescription(packet),sampleCount:1,sampleTimingEntryCount:1,sampleTimingArray:&timing,sampleSizeEntryCount:1,sampleSizeArray:&length,sampleBufferOut:&sample),noErr)
        XCTAssertThrowsError(try decoder.decode(try XCTUnwrap(sample),into:pixels))
        XCTAssertEqual(Double(try firstLuma(pixels)),721,accuracy:0)
    }
    func testAutomaticPipelineUsesSoftwareDecoderWhenSystemCannotDecode() async throws {
        let url = try await hlgFixture(quality:.proRes422HQ)
        let asset = AVURLAsset(url:url), contract = try await InputContract.inspect(asset)
        let probe = try AVAssetReader(asset:asset)
        let output = AVAssetReaderTrackOutput(track:contract.track,outputSettings:[kCVPixelBufferPixelFormatTypeKey as String:kCVPixelFormatType_422YpCbCr10])
        XCTAssertTrue(probe.canAdd(output)); probe.add(output)
        var systemCanDecode = false
        if probe.startReading() { systemCanDecode = try NativeSamples.nextMediaSample(output) != nil }
        probe.cancelReading()
        var options = ConversionOptions(); options.backend = .cpu
        let result = try await NativePipeline(options:options,quality:.proRes422HQ,progress:{ _,_ in }).process(url:url)
        addTeardownBlock {
            try? FileManager.default.removeItem(at:result.outputURL)
            try? FileManager.default.removeItem(at:result.outputURL.appendingPathExtension("json"))
        }
        XCTAssertEqual(result.frameCount,3)
        if !systemCanDecode {
            XCTAssertEqual(result.diagnostics.decodingBackend,"prores")
            XCTAssertTrue(result.diagnostics.warnings.contains(L10n.text("warning.decoderFallback")))
        }
        let report = try JSONSerialization.jsonObject(with:Data(contentsOf:result.outputURL.appendingPathExtension("json"))) as? [String:Any]
        XCTAssertEqual(report?["decoding"] as? String,result.diagnostics.decodingBackend)
        XCTAssertEqual(report?["appVersion"] as? String,AppBuild.version)
        XCTAssertEqual(report?["releaseBuild"] as? String,AppBuild.releaseBuild)
        XCTAssertEqual(report?["bundleBuild"] as? String,AppBuild.buildNumber)
        XCTAssertTrue(["AVFoundation","prores"].contains(report?["validationDecoding"] as? String ?? ""))
    }
    func testCPUAndSoftwareMOVIntegrationPreservesVFRAudioAndRotation() async throws {
        for quality in ProResQuality.allCases {
            let source = try await hlgFixture(quality:quality), asset = AVURLAsset(url:source)
            let contract = try await InputContract.inspect(asset)
            var options = ConversionOptions(); options.backend = .cpu
            let result = try await NativePipeline(options:options,quality:quality,progress:{ _,_ in }).run(asset:asset,contract:contract,software:true,softwareDecoding:true)
            addTeardownBlock {
                try? FileManager.default.removeItem(at:result.outputURL)
                try? FileManager.default.removeItem(at:result.outputURL.appendingPathExtension("json"))
            }
            XCTAssertEqual(result.frameCount,3)
            XCTAssertTrue(FileManager.default.fileExists(atPath:result.outputURL.appendingPathExtension("json").path))
            XCTAssertFalse(result.diagnostics.isLikelyCFR)
            XCTAssertEqual(result.diagnostics.encodingBackend,"prores_ks")
            XCTAssertEqual(result.diagnostics.decodingBackend,"prores")
            XCTAssertEqual(result.diagnostics.processingBackend,.cpu)
            try AppleLogMOV.process(url:result.outputURL,patch:false)
            let output = AVURLAsset(url:result.outputURL)
            try await TrackIntegrity.validate(source:asset,output:output,offset:.zero)
            let sourceTransform = try await contract.track.load(.preferredTransform)
            let outputTracks = try await output.loadTracks(withMediaType:.video)
            let track = try XCTUnwrap(outputTracks.first)
            let outputTransform = try await track.load(.preferredTransform)
            XCTAssertEqual(sourceTransform,outputTransform)
        }
    }
    func testMetalPackedColorChromaPhaseAndTailMatchCPU() async throws {
        guard MetalColorProcessor.isAvailable else { throw XCTSkip("Metal unavailable on this test destination") }
        let gpu = try MetalColorProcessor()
        for width in [2,6,14] {
            let input = try NativeSamples.buffer(width:width,height:2)
            let cpu = try NativeSamples.buffer(width:width,height:2), metal = try NativeSamples.buffer(width:width,height:2)
            XCTAssertEqual(CVPixelBufferLockBaseAddress(input,[]),kCVReturnSuccess)
            let base = try XCTUnwrap(CVPixelBufferGetBaseAddress(input)), stride = CVPixelBufferGetBytesPerRow(input)
            memset(base,0,stride*2)
            for y in 0..<2 {
                let row = base.advanced(by:y*stride).assumingMemoryBound(to:UInt32.self)
                for g in 0..<((width+5)/6) {
                    let l: [UInt32] = (0..<6).map { UInt32(64+(g*197+$0*83+y*41)%940) }
                    let u: [UInt32] = [200,500,800], v: [UInt32] = [750,350,600]
                    row[g*4] = u[0]|l[0]<<10|v[0]<<20; row[g*4+1] = l[1]|u[1]<<10|l[2]<<20
                    row[g*4+2] = v[1]|l[3]<<10|u[2]<<20; row[g*4+3] = l[4]|v[2]<<10|l[5]<<20
                }
            }
            CVPixelBufferUnlockBaseAddress(input,[])
            for full in [false,true] {
                for center in [false,true] {
                    var options = ConversionOptions(); options.exposure = 0.5; options.creativeEnabled = true
                    try V210Converter.convert(input:input,output:cpu,options:options,fullRange:full,centeredChroma:center)
                    _ = try await gpu.submit(inputPixelBuffer:input,outputPixelBuffer:metal,options:options,fullRange:full,centeredChroma:center).wait()
                    XCTAssertEqual(CVPixelBufferLockBaseAddress(cpu,.readOnly),kCVReturnSuccess)
                    XCTAssertEqual(CVPixelBufferLockBaseAddress(metal,.readOnly),kCVReturnSuccess)
                    defer { CVPixelBufferUnlockBaseAddress(cpu,.readOnly); CVPixelBufferUnlockBaseAddress(metal,.readOnly) }
                    let a = try XCTUnwrap(CVPixelBufferGetBaseAddress(cpu)), b = try XCTUnwrap(CVPixelBufferGetBaseAddress(metal))
                    for y in 0..<2 {
                        let aa = a.advanced(by:y*CVPixelBufferGetBytesPerRow(cpu)).assumingMemoryBound(to:UInt32.self)
                        let bb = b.advanced(by:y*CVPixelBufferGetBytesPerRow(metal)).assumingMemoryBound(to:UInt32.self)
                        for word in 0..<((width+5)/6)*4 {
                            for shift in [0,10,20] { XCTAssertLessThanOrEqual(abs(Int((aa[word]>>shift)&1023)-Int((bb[word]>>shift)&1023)),2) }
                        }
                    }
                }
            }
        }
    }
}
