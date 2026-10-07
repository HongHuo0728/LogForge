import Foundation
import CoreMedia

// AVAssetReader exposes edited samples, not the original stored AAC preroll.
// If Writer changes a retained track, copy its original samples and edit tables
// into the unpublished MOV. Video bytes and chunk offsets never move.
enum PreservedTracksMOV {
    typealias Box = AppleLogMOV.Box
    struct Movie {
        let position: UInt64
        let data: Data
        let media: [Range<UInt64>]
    }
    struct Chunk { let position: UInt64; let size: UInt64 }
    static func fail(_ detail: String) -> NativeFailure { NativeFailure("error.trackCopy", "MOV track preservation: " + detail) }
    static func number(_ data: Data, _ position: Int, _ bytes: Int = 4) throws -> UInt64 {
        try AppleLogMOV.integer(data,position,bytes)
    }
    static func bytes(_ value: UInt64, _ count: Int = 4) -> Data {
        Data((0..<count).reversed().map { UInt8(truncatingIfNeeded:value >> ($0*8)) })
    }
    static func children(_ data: Data) throws -> [Box] {
        let root = try AppleLogMOV.boxes(data,start:0,end:data.count)
        guard root.count == 1 else { throw fail("expected one atom") }
        return try AppleLogMOV.boxes(data,start:root[0].header,end:data.count)
    }
    static func child(_ data: Data, _ type: String) throws -> Data {
        let matching = try children(data).filter { $0.type == type }
        guard matching.count == 1, let box = matching.first else { throw fail("missing or duplicate " + type) }
        return data.subdata(in:box.start..<(box.start+box.size))
    }
    static func body(_ data: Data) throws -> Data {
        let roots = try AppleLogMOV.boxes(data,start:0,end:data.count)
        guard roots.count == 1 else { throw fail("invalid atom") }
        return data.subdata(in:roots[0].header..<data.count)
    }
    static func readMovie(_ file: FileHandle) throws -> Movie {
        let length = try file.seekToEnd()
        var position: UInt64 = 0, movie: Movie?, media: [Range<UInt64>] = []
        while position < length {
            try Task.checkCancellation()
            try file.seek(toOffset:position)
            guard let header = try file.read(upToCount:16), header.count >= 8 else { throw fail("truncated header") }
            let raw = try number(header,0), headerSize: UInt64 = raw == 1 ? 16 : 8
            let size = try (raw == 0 ? length-position : raw == 1 ? number(header,8,8) : raw)
            guard size >= headerSize, size <= length-position else { throw fail("invalid atom size") }
            let type = String(decoding:header[4..<8],as:UTF8.self)
            guard type != "moof" else { throw fail("fragmented input") }
            if type == "mdat" { media.append((position+headerSize)..<(position+size)) }
            if type == "moov" {
                guard movie == nil, size <= 64*1024*1024 else { throw fail("missing or oversized movie table") }
                try file.seek(toOffset:position)
                guard let data = try file.read(upToCount:Int(size)), data.count == Int(size) else { throw fail("truncated movie table") }
                movie = Movie(position:position,data:data,media:[])
            }
            position += size
        }
        guard let movie else { throw fail("missing moov") }
        return Movie(position:movie.position,data:movie.data,media:media)
    }
    static func trackID(_ track: Data) throws -> UInt32 {
        let header = try body(child(track,"tkhd"))
        guard header.count >= 24, header[0] <= 1 else { throw fail("invalid track header") }
        let id = try number(header,header[0] == 1 ? 20 : 12)
        guard id > 0 else { throw fail("zero track ID") }
        return UInt32(id)
    }
    static func headerFields(_ atom: Data) throws -> (scale: UInt32, duration: UInt64) {
        let p = try body(atom)
        guard p.count >= 20, p[0] <= 1 else { throw fail("invalid movie header") }
        let wide = p[0] == 1
        let scale = try number(p,wide ? 20 : 12)
        guard scale > 0, scale <= UInt64(Int32.max) else { throw fail("invalid movie clock") }
        return (UInt32(scale),try number(p,wide ? 24 : 16,wide ? 8 : 4))
    }
    static func scaled(_ value: UInt64, from: UInt32, to: UInt32) throws -> UInt64 {
        guard value <= UInt64(Int64.max) else { throw fail("duration overflow") }
        let time = CMTimeConvertScale(CMTime(value:Int64(value),timescale:Int32(from)),timescale:Int32(to),method:.roundHalfAwayFromZero)
        guard time.isNumeric, time.value >= 0 else { throw fail("duration conversion overflow") }
        return UInt64(time.value)
    }
    // Promote tkhd/mvhd to version 1 when a high-resolution movie clock would
    // overflow its 32-bit duration. Creation dates and remaining fields survive.
    static func header(_ atom: Data, type: String, duration: UInt64, id: UInt32? = nil) throws -> Data {
        let p = try body(atom)
        guard p.count >= 24, p[0] <= 1 else { throw fail("unknown header version") }
        let wide = p[0] == 1
        let creation = try number(p,4,wide ? 8 : 4), modification = try number(p,wide ? 12 : 8,wide ? 8 : 4)
        let field = try number(p,wide ? 20 : 12)
        let newWide = wide || duration > UInt64(UInt32.max)
        var result = Data([newWide ? 1 : 0]); result.append(p.subdata(in:1..<4))
        result.append(bytes(creation,newWide ? 8 : 4)); result.append(bytes(modification,newWide ? 8 : 4))
        result.append(bytes(id.map { UInt64($0) } ?? field))
        if type == "tkhd" { result.append(p.subdata(in:(wide ? 24 : 16)..<(wide ? 28 : 20))) }
        result.append(bytes(duration,newWide ? 8 : 4))
        let tail = type == "tkhd" ? (wide ? 36 : 24) : (wide ? 32 : 20)
        guard tail <= p.count else { throw fail("truncated header tail") }
        result.append(p.subdata(in:tail..<p.count))
        return try AppleLogMOV.atom(type,result)
    }
    static func edits(_ atom: Data, from: UInt32, to: UInt32) throws -> Data {
        let p = try body(atom)
        guard p.count >= 8, p[0] <= 1 else { throw fail("invalid edit list") }
        let wide = p[0] == 1, count = Int(try number(p,4)), stride = wide ? 20 : 12
        guard count <= 100000, p.count == 8+count*stride else { throw fail("truncated edit list") }
        var result = Data([1]); result.append(p.subdata(in:1..<8))
        var cumulative: UInt64 = 0, previous: UInt64 = 0
        for index in 0..<count {
            let at = 8+index*stride, duration = try number(p,at,wide ? 8 : 4)
            guard duration <= UInt64(Int64.max)-cumulative else { throw fail("edit duration overflow") }
            cumulative += duration
            let end = try scaled(cumulative,from:from,to:to)
            guard end >= previous else { throw fail("invalid edit boundary") }
            result.append(bytes(end-previous,8)); previous = end
            let media = try number(p,at+(wide ? 8 : 4),wide ? 8 : 4)
            result.append(bytes(wide ? media : UInt64(bitPattern:Int64(Int32(bitPattern:UInt32(media)))),8))
            result.append(p.subdata(in:(at+stride-4)..<(at+stride)))
        }
        return try AppleLogMOV.atom("elst",result)
    }
    static func chunks(_ track: Data, movie: Movie) throws -> [Chunk] {
        let media = try child(track,"mdia"), info = try child(media,"minf"), table = try child(info,"stbl")
        // Only self-contained media is eligible; never follow external files.
        let references = try body(child(child(info,"dinf"),"dref"))
        guard references.count >= 8 else { throw fail("invalid data references") }
        let refs = try AppleLogMOV.boxes(references,start:8,end:references.count)
        guard refs.count == Int(try number(references,4)), !refs.isEmpty else { throw fail("invalid data reference count") }
        for ref in refs {
            guard ["url ","alis"].contains(ref.type), ref.size == ref.header+4,
                  try number(references,ref.start+ref.header) & 1 == 1 else { throw fail("external media reference") }
        }
        let entries = try body(child(table,"stsd"))
        let descriptions = try AppleLogMOV.boxes(entries,start:8,end:entries.count)
        guard descriptions.count == Int(try number(entries,4)), !descriptions.isEmpty else { throw fail("invalid sample descriptions") }
        for entry in descriptions {
            let reference = try number(entries,entry.start+entry.header+6,2)
            guard entry.size >= entry.header+8, reference > 0, reference <= UInt64(refs.count) else { throw fail("invalid sample data reference") }
        }
        let tables = try children(table)
        let offsets = tables.filter { ["stco","co64"].contains($0.type) }
        guard offsets.count == 1, let offsetBox = offsets.first else { throw fail("missing chunk offsets") }
        let offsetData = try body(table.subdata(in:offsetBox.start..<(offsetBox.start+offsetBox.size)))
        let count = Int(try number(offsetData,4)), width = offsetBox.type == "co64" ? 8 : 4
        guard offsetData.count == 8+count*width else { throw fail("invalid chunk offset count") }
        let sizes = try body(child(table,"stsz")), fixed = try number(sizes,4), sampleCount = Int(try number(sizes,8))
        guard sizes.count == 12+(fixed == 0 ? sampleCount*4 : 0) else { throw fail("invalid sample sizes") }
        let mapping = try body(child(table,"stsc")), rows = Int(try number(mapping,4))
        guard rows > 0, mapping.count == 8+rows*12, count > 0 else { throw fail("invalid sample to chunk mapping") }
        var row = 0, sample = 0, result: [Chunk] = []
        var firstChunks: [Int] = [], samplesPerChunk: [Int] = []
        for index in 0..<rows {
            let at = 8+index*12, first = Int(try number(mapping,at)), perChunk = Int(try number(mapping,at+4))
            let description = try number(mapping,at+8)
            guard first > 0, first <= count, index == 0 ? first == 1 : first > firstChunks[index-1],
                  perChunk > 0, description > 0, description <= UInt64(descriptions.count) else { throw fail("invalid chunk mapping row") }
            firstChunks.append(first); samplesPerChunk.append(perChunk)
        }
        for index in 0..<count {
            try Task.checkCancellation()
            while row+1 < rows, firstChunks[row+1] <= index+1 { row += 1 }
            let samples = samplesPerChunk[row]
            guard samples <= sampleCount-sample else { throw fail("chunk exceeds sample count") }
            var size: UInt64 = 0
            for _ in 0..<samples {
                let value = try (fixed == 0 ? number(sizes,12+sample*4) : fixed)
                guard value <= UInt64(Int64.max)-size else { throw fail("chunk size overflow") }
                size += value; sample += 1
            }
            let position = try number(offsetData,8+index*width,width)
            guard size > 0, movie.media.contains(where:{ position >= $0.lowerBound && position < $0.upperBound && size <= $0.upperBound-position }) else { throw fail("chunk outside media data") }
            result.append(Chunk(position:position,size:size))
        }
        guard sample == sampleCount else { throw fail("unmapped samples") }
        return result
    }
    static func rewrite(_ data: Data, from: UInt32, to: UInt32, offsets: [UInt64]?, id: UInt32? = nil,
                        references: Set<UInt32>, depth: Int = 0) throws -> Data {
        guard depth <= 16, let box = try AppleLogMOV.boxes(data,start:0,end:data.count).first else { throw fail("invalid track tree") }
        if box.type == "tkhd" {
            let p = try body(data)
            guard p.count >= 24, p[0] <= 1 else { throw fail("invalid track header") }
            let wide = p[0] == 1
            let duration = try number(p,wide ? 28 : 20,wide ? 8 : 4)
            return try header(data,type:"tkhd",duration:scaled(duration,from:from,to:to),id:id)
        }
        if box.type == "elst" { return try edits(data,from:from,to:to) }
        if ["stco","co64"].contains(box.type), let offsets {
            var payload = Data(repeating:0,count:4); payload.append(bytes(UInt64(offsets.count)))
            for position in offsets { payload.append(bytes(position,8)) }
            return try AppleLogMOV.atom("co64",payload)
        }
        if box.type == "tref" {
            var payload = Data()
            for reference in try children(data) {
                guard (reference.size-reference.header)%4 == 0 else { throw fail("invalid track association") }
                var ids = Data()
                for at in stride(from:reference.start+reference.header,to:reference.start+reference.size,by:4) {
                    let target = UInt32(try number(data,at))
                    if references.contains(target) { ids.append(bytes(UInt64(target))) }
                }
                if !ids.isEmpty { payload.append(try AppleLogMOV.atom(reference.type,ids)) }
            }
            return try AppleLogMOV.atom("tref",payload)
        }
        if ["trak","mdia","minf","stbl","edts"].contains(box.type) {
            var payload = Data()
            for item in try children(data) {
                if item.type == "tref", references.isEmpty { continue }
                payload.append(try rewrite(data.subdata(in:item.start..<(item.start+item.size)),from:from,to:to,
                    offsets:offsets,id:id,references:references,depth:depth+1))
            }
            return try AppleLogMOV.atom(box.type,payload)
        }
        return data
    }
    static func restore(sourceURL: URL, outputURL: URL, videoTrackID: UInt32, retainedTrackIDs: Set<UInt32>) throws {
        guard sourceURL.standardizedFileURL != outputURL.standardizedFileURL else { throw fail("source aliases output") }
        let source = try FileHandle(forReadingFrom:sourceURL), output = try FileHandle(forUpdating:outputURL)
        defer { try? source.close(); try? output.close() }
        let original = try readMovie(source), converted = try readMovie(output)
        guard converted.position+UInt64(converted.data.count) == (try output.seekToEnd()) else { throw fail("output moov must be last") }
        let originalScale = try headerFields(child(original.data,"mvhd")).scale
        let convertedHeader = try child(converted.data,"mvhd"), convertedFields = try headerFields(convertedHeader)
        let sourceTracks = try children(original.data).filter { $0.type == "trak" }.map { original.data.subdata(in:$0.start..<($0.start+$0.size)) }
        let trackIDs = try sourceTracks.map { try trackID($0) }
        guard Set(trackIDs).count == trackIDs.count, Set(trackIDs).isSuperset(of:retainedTrackIDs.union([videoTrackID])) else { throw fail("missing original tracks") }
        let retained = try sourceTracks.filter { retainedTrackIDs.contains(try trackID($0)) }
        let video = try sourceTracks.first { try trackID($0) == videoTrackID }
        guard let video else { throw fail("missing source video") }
        let outputTracks = try children(converted.data).filter { $0.type == "trak" }.map { converted.data.subdata(in:$0.start..<($0.start+$0.size)) }
        let outputVideos = try outputTracks.filter {
            let handler = try body(child(child($0,"mdia"),"hdlr"))
            return handler.count >= 12 && String(decoding:handler[8..<12],as:UTF8.self) == "vide"
        }
        guard outputVideos.count == 1, let outputVideo = outputVideos.first else { throw fail("output video count") }
        let copied = try retained.map { try chunks($0,movie:original) }
        var mediaBytes: UInt64 = 0
        for chunk in copied.flatMap({ $0 }) {
            guard chunk.size <= UInt64(Int64.max)-mediaBytes else { throw fail("media size overflow") }
            mediaBytes += chunk.size
        }
        guard mediaBytes <= UInt64(Int64.max)-converted.position-16 else { throw fail("output offset overflow") }
        let ids = retainedTrackIDs.union([videoTrackID])
        var rewrittenVideo = try rewrite(outputVideo,from:convertedFields.scale,to:convertedFields.scale,offsets:nil,id:videoTrackID,references:[])
        // Original video associations refer to the restored original track IDs.
        let sourceReferences = try children(video).filter { $0.type == "tref" }
        if let reference = sourceReferences.first {
            var payload = try body(rewrittenVideo)
            payload.append(try rewrite(video.subdata(in:reference.start..<(reference.start+reference.size)),from:originalScale,to:convertedFields.scale,offsets:nil,references:ids))
            rewrittenVideo = try AppleLogMOV.atom("trak",payload)
        }
        var position = converted.position+16, restored: [Data] = [], movieDuration = convertedFields.duration
        for (track,index) in zip(retained,retained.indices) {
            var offsets: [UInt64] = []
            for chunk in copied[index] { offsets.append(position); position += chunk.size }
            let replacement = try rewrite(track,from:originalScale,to:convertedFields.scale,offsets:offsets,references:ids)
            let p = try body(child(replacement,"tkhd")), wide = p[0] == 1
            movieDuration = max(movieDuration,try number(p,wide ? 28 : 20,wide ? 8 : 4))
            restored.append(replacement)
        }
        var moviePayload = Data()
        for item in try children(converted.data) {
            if item.type == "trak" { continue }
            if item.type == "mvhd" {
                var h = try header(convertedHeader,type:"mvhd",duration:movieDuration)
                guard let largest = ids.max(), largest < UInt32.max else { throw fail("track ID overflow") }
                h.replaceSubrange((h.count-4)..<h.count,with:bytes(UInt64(largest+1)))
                moviePayload.append(h)
            } else { moviePayload.append(converted.data.subdata(in:item.start..<(item.start+item.size))) }
        }
        moviePayload.append(rewrittenVideo); for track in restored { moviePayload.append(track) }
        let movie = try AppleLogMOV.atom("moov",moviePayload)
        // Validate every table before touching the unpublished output. Copy only
        // retained chunks, not the source ProRes video; memory stays bounded.
        try Task.checkCancellation(); try output.seek(toOffset:converted.position)
        var mdat = bytes(1); mdat.append(Data("mdat".utf8)); mdat.append(bytes(mediaBytes+16,8))
        try output.write(contentsOf:mdat)
        for chunk in copied.flatMap({ $0 }) {
            try source.seek(toOffset:chunk.position); var remaining = chunk.size
            while remaining > 0 {
                try Task.checkCancellation()
                let count = Int(min(remaining,1024*1024))
                guard let data = try source.read(upToCount:count), data.count == count else { throw fail("truncated source chunk") }
                try output.write(contentsOf:data); remaining -= UInt64(count)
            }
        }
        try output.write(contentsOf:movie); try output.truncate(atOffset:position+UInt64(movie.count)); try output.synchronize()
    }
}
