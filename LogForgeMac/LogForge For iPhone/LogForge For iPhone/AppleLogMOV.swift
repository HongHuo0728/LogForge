import Foundation

// Operates ONLY on an unpublished MOV owned by this conversion. Media offsets never move.
enum AppleLogMOV {
    static let identifier = "com.apple.rec2020.apple-log"
    struct Box { let start: Int; let size: Int; let header: Int; let type: String }
    static func integer(_ data: Data, _ offset: Int, _ bytes: Int = 4) throws -> UInt64 {
        guard offset >= 0, offset <= data.count - bytes else { throw NativeFailure("error.metadata") }
        return data[offset..<(offset+bytes)].reduce(0) { ($0 << 8) | UInt64($1) }
    }
    static func boxes(_ data: Data, start: Int, end: Int) throws -> [Box] {
        var result: [Box] = [], p = start
        guard start >= 0, end <= data.count, start <= end else { throw NativeFailure("error.metadata") }
        while p < end {
            if end-p == 4, try integer(data,p) == 0 { break } // VisualSampleEntry terminator.
            guard end-p >= 8, result.count < 10000 else { throw NativeFailure("error.metadata") }
            let n = try integer(data,p), header = n == 1 ? 16 : 8
            let size = n == 1 ? try integer(data,p+8,8) : n
            guard size >= UInt64(header), size <= UInt64(end-p) else { throw NativeFailure("error.metadata") }
            let type = String(decoding: data[(p+4)..<(p+8)], as: UTF8.self)
            result.append(Box(start:p,size:Int(size),header:header,type:type)); p += Int(size)
        }
        return result
    }
    static func atom(_ type: String, _ payload: Data) throws -> Data {
        guard type.utf8.count == 4, payload.count <= Int(UInt32.max)-8 else { throw NativeFailure("error.metadata") }
        let n = UInt32(payload.count+8)
        var result = Data([UInt8(n>>24),UInt8((n>>16)&255),UInt8((n>>8)&255),UInt8(n&255)])
        result.append(contentsOf:type.utf8); result.append(payload); return result
    }
    private static func transform(_ data: Data, _ box: Box, count: inout Int, patch: Bool, depth: Int = 0) throws -> Data {
        guard depth <= 16 else { throw NativeFailure("error.metadata") }
        let body = box.start+box.header, end = box.start+box.size
        let containers: Set<String> = ["moov","trak","mdia","minf","stbl"]
        if containers.contains(box.type) || box.type == "stsd" {
            let prefix = box.type == "stsd" ? 8 : 0
            guard body+prefix <= end else { throw NativeFailure("error.metadata") }
            var payload = data.subdata(in:body..<(body+prefix))
            let children = try boxes(data,start:body+prefix,end:end)
            if box.type == "stsd" {
                guard try integer(data,body+4) == UInt64(children.count) else { throw NativeFailure("error.metadata") }
            }
            for child in children { payload.append(try transform(data,child,count:&count,patch:patch,depth:depth+1)) }
            return try atom(box.type,payload)
        }
        guard ["apcn","apch"].contains(box.type) else { return data.subdata(in:box.start..<end) }
        count += 1
        guard count == 1, body+78 <= end else { throw NativeFailure("error.metadata") }
        var payload = data.subdata(in:body..<(body+78))
        var logs = 0, colors = 0
        for child in try boxes(data,start:body+78,end:end) {
            let p = child.start+child.header
            if child.type == "logs" {
                logs += 1
                guard data.subdata(in:p..<(child.start+child.size)) == Data(identifier.utf8) else { throw NativeFailure("error.metadata") }
            } else if child.type == "colr" {
                colors += 1
                guard child.size >= child.header+10,
                      ["nclc","nclx"].contains(String(decoding:data[p..<(p+4)],as:UTF8.self)),
                      try integer(data,p+4,2) == 9, try integer(data,p+6,2) == 2,
                      try integer(data,p+8,2) == 9 else { throw NativeFailure("error.metadata") }
                let kind = String(decoding:data[p..<(p+4)],as:UTF8.self)
                guard child.size == child.header+(kind == "nclx" ? 11 : 10) else { throw NativeFailure("error.metadata") }
                if kind == "nclx", data[p+10] & 0x80 != 0 { throw NativeFailure("error.metadata") }
            } else {
                guard !["gama","mdcv","clli","dvcC","dvvC"].contains(child.type) else { throw NativeFailure("error.metadata") }
                payload.append(data.subdata(in:child.start..<(child.start+child.size)))
            }
        }
        guard colors <= 1, logs <= 1, patch || (colors == 1 && logs == 1) else { throw NativeFailure("error.metadata") }
        payload.append(try atom("colr",Data([110,99,108,99,0,9,0,2,0,9])))
        payload.append(try atom("logs",Data(identifier.utf8)))
        return try atom(box.type,payload)
    }
    static func process(url: URL, patch: Bool) throws {
        let file = try (patch ? FileHandle(forUpdating:url) : FileHandle(forReadingFrom:url)); defer { try? file.close() }
        let length = try file.seekToEnd()
        var offset: UInt64 = 0, movie: (UInt64, Int)?
        while offset < length {
            try Task.checkCancellation()
            try file.seek(toOffset:offset)
            guard let header = try file.read(upToCount:16), header.count >= 8 else { throw NativeFailure("error.metadata") }
            let raw = try integer(header,0), size = raw == 1 ? try integer(header,8,8) : raw
            guard size >= (raw == 1 ? 16 : 8), size <= length-offset else { throw NativeFailure("error.metadata") }
            let type = String(decoding:header[4..<8],as:UTF8.self)
            guard type != "moof" else { throw NativeFailure("error.metadata") }
            if type == "moov" {
                guard movie == nil, offset+size == length, size <= 64*1024*1024 else { throw NativeFailure("error.metadata") }
                movie = (offset,Int(size))
            }
            offset += size
        }
        guard let movie else { throw NativeFailure("error.metadata") }
        try file.seek(toOffset:movie.0)
        guard let data = try file.read(upToCount:movie.1), data.count == movie.1,
              let root = try boxes(data,start:0,end:data.count).first else { throw NativeFailure("error.metadata") }
        var count = 0
        let replacement = try transform(data,root,count:&count,patch:patch)
        guard count == 1 else { throw NativeFailure("error.metadata") }
        if patch {
            try Task.checkCancellation(); try file.seek(toOffset:movie.0); try file.write(contentsOf:replacement)
            try file.truncate(atOffset:movie.0+UInt64(replacement.count)); try file.synchronize()
        }
    }
}
