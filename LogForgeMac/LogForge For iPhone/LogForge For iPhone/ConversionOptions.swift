import Foundation
import CoreVideo

enum ColorBackend: String, CaseIterable, Identifiable, Codable {
    case automatic, metal, cpu
    var id: String { rawValue }
    var title: String { L10n.text("backend.\(rawValue)") }
}

struct ConversionOptions: Codable, Equatable {
    var backend: ColorBackend = .automatic
    var exposure: Float = 0
    var creativeEnabled = false
    var shadow: Float = 3
    var highlight: Float = 1
    var saturation: Float = 0.85
    static let exposureChoices: [Float] = [-4, -3, -2, -1, -0.5, 0, 0.5, 1, 2, 3, 4]
    static let toneChoices: [Float] = [0, 0.5, 1, 1.5, 2, 2.5, 3]
    static let saturationChoices: [Float] = [0, 0.25, 0.5, 0.65, 0.75, 0.85, 1, 1.15, 1.3, 1.5]
    func validate() throws {
        guard Self.exposureChoices.contains(exposure), Self.toneChoices.contains(shadow),
              Self.toneChoices.contains(highlight), Self.saturationChoices.contains(saturation) else {
            throw NativeFailure("error.options")
        }
    }
    // Six contiguous floats, identical to buffer(6) in Metal. No Swift Bool ABI crosses the GPU boundary.
    func parameters(fullRange: Bool, centeredChroma: Bool) -> [Float] {
        [exposure, creativeEnabled ? shadow : 0, creativeEnabled ? highlight : 0,
         creativeEnabled ? saturation : 1, fullRange ? 1 : 0, centeredChroma ? 1 : 0]
    }
}

struct NativeFailure: LocalizedError {
    let key: String
    let detail: String
    init(_ key: String, _ detail: String = "") { self.key = key; self.detail = detail }
    var errorDescription: String? { L10n.text(key) + (detail.isEmpty ? "" : "\n" + detail) }
}

enum AppBuild {
    static var version: String { Bundle.main.object(forInfoDictionaryKey:"CFBundleShortVersionString") as? String ?? "unknown" }
    static var buildNumber: String { Bundle.main.object(forInfoDictionaryKey:"CFBundleVersion") as? String ?? "unknown" }
    static var label: String {
        version
    }
    static var diagnosticLabel: String { "\(label) [build \(buildNumber)]" }
}

enum L10n {
    static func text(_ key: String) -> String { NSLocalizedString(key, comment: "") }
}

enum ReferenceColor {
    static let a = 0.17883277
    static let b = 1 - 4 * a
    static let c = 0.5 - a * log(4 * a)
    static let scale = 1 / inverseHLG(0.75)
    static func inverseHLG(_ value: Double) -> Double {
        if value < 0 { return -(value * value) / 3 }
        return value <= 0.5 ? value * value / 3 : (exp((value - c) / a) + b) / 12
    }
    static func appleLog(_ value: Double) -> Double {
        if value >= 0.01 { return 0.08550479 * log2(value + 0.00964052) + 0.69336945 }
        if value >= -0.05641088 { return 47.28711236 * pow(value + 0.05641088, 2) }
        return 0
    }
    static func transform(_ rgb: SIMD3<Double>, options: ConversionOptions) -> SIMD3<Double> {
        var linear = SIMD3(inverseHLG(rgb.x), inverseHLG(rgb.y), inverseHLG(rgb.z)) * (scale * exp2(Double(options.exposure)))
        if options.creativeEnabled {
            let y = linear.x * 0.2627 + linear.y * 0.678 + linear.z * 0.0593
            let x = y > 0 ? log2(y / 0.18) : -6
            let t = min(abs(x) / 6, 1)
            let w = t * t * (3 - 2 * t)
            let gain = exp2((x < 0 ? Double(options.shadow) : -Double(options.highlight)) * w)
            linear = (SIMD3<Double>(repeating: y) + (linear - SIMD3<Double>(repeating: y)) * Double(options.saturation)) * gain
        }
        return SIMD3(appleLog(linear.x), appleLog(linear.y), appleLog(linear.z))
    }
    // Spline36 interpolation, phase-aware and shared with the Metal implementation.
    static func spline(_ value: Double) -> Double {
        let x = abs(value)
        if x < 1 { return ((13.0 / 11 * x - 453.0 / 209) * x - 3.0 / 209) * x + 1 }
        if x < 2 { let t = x - 1; return ((-6.0 / 11 * t + 270.0 / 209) * t - 156.0 / 209) * t }
        if x < 3 { let t = x - 2; return ((1.0 / 11 * t - 45.0 / 209) * t + 26.0 / 209) * t }
        return 0
    }
}
