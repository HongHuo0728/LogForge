import Foundation
import CoreVideo

extension V210Converter {
    static func convert(input: CVPixelBuffer, output: CVPixelBuffer, options: ConversionOptions,
                        fullRange: Bool, centeredChroma: Bool) throws {
        let w = CVPixelBufferGetWidth(input), h = CVPixelBufferGetHeight(input)
        guard w > 0, h > 0, w % 2 == 0, isSupported(input), isSupported(output),
              CVPixelBufferGetWidth(output) == w, CVPixelBufferGetHeight(output) == h else { throw V210Error.sizeMismatch }
        let si = CVPixelBufferGetBytesPerRow(input), so = CVPixelBufferGetBytesPerRow(output)
        guard si >= minimumBytesPerRow(width: w), so >= minimumBytesPerRow(width: w) else { throw V210Error.strideTooSmall }
        guard CVPixelBufferLockBaseAddress(input, .readOnly) == kCVReturnSuccess else { throw V210Error.lockFailed }
        defer { CVPixelBufferUnlockBaseAddress(input, .readOnly) }
        guard CVPixelBufferLockBaseAddress(output, []) == kCVReturnSuccess else { throw V210Error.lockFailed }
        defer { CVPixelBufferUnlockBaseAddress(output, []) }
        guard let ib = CVPixelBufferGetBaseAddress(input), let ob = CVPixelBufferGetBaseAddress(output) else { throw V210Error.baseAddressUnavailable }
        let chromaCount = w / 2
        let phaseWeights: [[Double]] = [0, 1].map { parity in
            let pos = (Double(parity) - (centeredChroma ? 0.5 : 0)) / 2
            let fraction = pos - floor(pos)
            return (-2...3).map { ReferenceColor.spline(fraction - Double($0)) }
        }
        let outputWeights = (-5...5).map { ReferenceColor.spline(Double($0)/2) }
        let outputWeightSum = outputWeights.reduce(0,+)
        var luma = [Double](repeating:0,count:w)
        var cb = [Double](repeating:0,count:chromaCount), cr = cb
        var converted = [SIMD3<Double>](repeating:SIMD3(repeating:0),count:w)
        func code(_ v: Double, _ chroma: Bool) -> UInt32 {
            UInt32(min(max((v * (chroma ? 896 : 876) + (chroma ? 512 : 64)).rounded(), 0), 1023))
        }
        for y in 0..<h {
            if y % 8 == 0 { try Task.checkCancellation() }
            let src = ib.advanced(by:y*si).assumingMemoryBound(to:UInt32.self)
            let dst = ob.advanced(by:y*so).assumingMemoryBound(to:UInt32.self)
            memset(dst,0,so)
            for g in 0..<((w+5)/6) {
                let q = [src[g*4],src[g*4+1],src[g*4+2],src[g*4+3]]
                let yy = [q[0]>>10,q[1],q[1]>>20,q[2]>>10,q[3],q[3]>>20]
                let uu = [q[0],q[1]>>10,q[2]>>20], vv = [q[0]>>20,q[2],q[3]>>10]
                for k in 0..<6 where g*6+k < w { luma[g*6+k] = Double(yy[k]&1023) }
                for k in 0..<3 where g*3+k < chromaCount { cb[g*3+k] = Double(uu[k]&1023); cr[g*3+k] = Double(vv[k]&1023) }
            }
            for x in 0..<w {
                let base = Int(floor((Double(x)-(centeredChroma ? 0.5:0))/2))
                let weights = phaseWeights[x%2]
                var u = 0.0, v = 0.0, total = 0.0
                for k in 0..<6 {
                    let j = min(max(base+k-2,0),chromaCount-1), weight = weights[k]
                    u += cb[j]*weight; v += cr[j]*weight; total += weight
                }
                u = (u/total-512)/(fullRange ? 1023:896)
                v = (v/total-512)/(fullRange ? 1023:896)
                let yy = (luma[x]-(fullRange ? 0:64))/(fullRange ? 1023:876)
                let rgb = ReferenceColor.transform(SIMD3(yy+1.4746*v,yy-(0.0593*1.8814/0.678)*u-(0.2627*1.4746/0.678)*v,yy+1.8814*u),options:options)
                let l = rgb.x*0.2627+rgb.y*0.678+rgb.z*0.0593
                converted[x] = SIMD3(l,(rgb.z-l)/1.8814,(rgb.x-l)/1.4746)
            }
            for g in 0..<((w+5)/6) {
                var yy = [UInt32](repeating:0,count:6), uu = [UInt32](repeating:0,count:3), vv = uu
                for k in 0..<6 { yy[k] = code(converted[min(g*6+k,w-1)].x,false) }
                for k in 0..<3 {
                    let x = min(g*6+k*2,w-2)
                    var value = SIMD3<Double>(repeating:0)
                    for j in -5...5 { value += converted[min(max(x+j,0),w-1)]*outputWeights[j+5] }
                    uu[k] = code(value.y/outputWeightSum,true); vv[k] = code(value.z/outputWeightSum,true)
                }
                dst[g*4] = uu[0]|yy[0]<<10|vv[0]<<20
                dst[g*4+1] = yy[1]|uu[1]<<10|yy[2]<<20
                dst[g*4+2] = vv[1]|yy[3]<<10|uu[2]<<20
                dst[g*4+3] = yy[4]|vv[2]<<10|yy[5]<<20
            }
        }
    }
}
