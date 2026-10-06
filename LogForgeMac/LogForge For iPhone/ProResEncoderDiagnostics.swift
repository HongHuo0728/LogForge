import Foundation
import VideoToolbox
import CoreMedia
import AVFoundation

enum ProResEncoderDiagnostics {

    // MARK: - Encoder Information

    struct EncoderInfo: Identifiable {

        let id = UUID()

        let codec: CMVideoCodecType
        let codecName: String
        let encoderID: String
        let encoderName: String
        let displayName: String
        let hardwareAccelerated: Bool
    }

    // MARK: - Codec Test

    struct CodecTestResult {

        let codecName: String

        let status: OSStatus

        let encoderID: String?

        let supportedPropertiesCount: Int

        var isAvailable: Bool {
            status == noErr
        }
    }

    // MARK: - Full Report

    struct Report {

        let encoders: [EncoderInfo]

        let proRes422Test: CodecTestResult

        let proRes422HQTest: CodecTestResult

        let writer422Supported: Bool

        let writer422HQSupported: Bool

        let writer422Error: String?

        let writer422HQError: String?

        var hasProRes422: Bool {
            encoders.contains {
                $0.codec == kCMVideoCodecType_AppleProRes422
            }
        }

        var hasProRes422HQ: Bool {
            encoders.contains {
                $0.codec == kCMVideoCodecType_AppleProRes422HQ
            }
        }
    }

    // MARK: - Main Diagnostic

    static func run() -> Report {

        let encoders = inspectEncoderList()

        let proRes422Test = testEncoder(
            codec: kCMVideoCodecType_AppleProRes422,
            name: "Apple ProRes 422"
        )

        let proRes422HQTest = testEncoder(
            codec: kCMVideoCodecType_AppleProRes422HQ,
            name: "Apple ProRes 422 HQ"
        )

        let writer422 = testAssetWriter(
            codec: AVVideoCodecType.proRes422
        )

        let writer422HQ = testAssetWriter(
            codec: AVVideoCodecType.proRes422HQ
        )

        return Report(
            encoders: encoders,
            proRes422Test: proRes422Test,
            proRes422HQTest: proRes422HQTest,
            writer422Supported: writer422.supported,
            writer422HQSupported: writer422HQ.supported,
            writer422Error: writer422.error,
            writer422HQError: writer422HQ.error
        )
    }

    // MARK: - Encoder List

    private static func inspectEncoderList()
        -> [EncoderInfo]
    {

        var encoderList: CFArray?

        let status = VTCopyVideoEncoderList(
            nil,
            &encoderList
        )

        guard status == noErr else {

            return []
        }

        guard let encoderList else {

            return []
        }

        /*
         IMPORTANT

         Do NOT call CFRelease here.

         In the Swift SDK used by the project,
         CFRelease is imported as unavailable for
         this automatically managed Core Foundation
         value.
         */

        let array = encoderList as NSArray

        var result: [EncoderInfo] = []

        for case let dictionary as NSDictionary in array {

            guard
                let codecNumber =
                    dictionary[
                        kVTVideoEncoderList_CodecType
                    ] as? NSNumber
            else {
                continue
            }

            let codec =
                CMVideoCodecType(
                    codecNumber.uint32Value
                )

            guard
                codec ==
                    kCMVideoCodecType_AppleProRes422
                ||
                codec ==
                    kCMVideoCodecType_AppleProRes422HQ
            else {
                continue
            }

            let codecName =
                dictionary[
                    kVTVideoEncoderList_CodecName
                ] as? String
                ?? "Unknown"

            let encoderID =
                dictionary[
                    kVTVideoEncoderList_EncoderID
                ] as? String
                ?? "Unknown"

            let encoderName =
                dictionary[
                    kVTVideoEncoderList_EncoderName
                ] as? String
                ?? "Unknown"

            let displayName =
                dictionary[
                    kVTVideoEncoderList_DisplayName
                ] as? String
                ?? "Unknown"

            let hardwareAccelerated: Bool

            if let number =
                dictionary[
                    kVTVideoEncoderList_IsHardwareAccelerated
                ] as? NSNumber {

                hardwareAccelerated =
                    number.boolValue

            } else if let bool =
                        dictionary[
                            kVTVideoEncoderList_IsHardwareAccelerated
                        ] as? Bool {

                hardwareAccelerated = bool

            } else {

                hardwareAccelerated = false
            }

            result.append(
                EncoderInfo(
                    codec: codec,
                    codecName: codecName,
                    encoderID: encoderID,
                    encoderName: encoderName,
                    displayName: displayName,
                    hardwareAccelerated:
                        hardwareAccelerated
                )
            )
        }

        return result
    }

    // MARK: - Exact 3840 × 2160 Encoder Test

    private static func testEncoder(
        codec: CMVideoCodecType,
        name: String
    ) -> CodecTestResult {

        var encoderID: CFString?
        var supportedProperties: CFDictionary?

        let status =
            VTCopySupportedPropertyDictionaryForEncoder(
                width: 3840,
                height: 2160,
                codecType: codec,
                encoderSpecification: nil,
                encoderIDOut: &encoderID,
                supportedPropertiesOut:
                    &supportedProperties
            )

        let encoderIDString: String?

        if let encoderID {

            encoderIDString =
                encoderID as String

        } else {

            encoderIDString = nil
        }

        let propertiesCount: Int

        if let supportedProperties {

            propertiesCount =
                (supportedProperties as NSDictionary).count

        } else {

            propertiesCount = 0
        }

        return CodecTestResult(
            codecName: name,
            status: status,
            encoderID: encoderIDString,
            supportedPropertiesCount:
                propertiesCount
        )
    }

    // MARK: - AVAssetWriter Test

    private static func testAssetWriter(
        codec: AVVideoCodecType
    ) -> (
        supported: Bool,
        error: String?
    ) {

        let temporaryURL =
            FileManager.default.temporaryDirectory
                .appendingPathComponent(
                    "LogForge_Diagnostic_\(UUID().uuidString).mov"
                )

        do {

            let writer =
                try AVAssetWriter(
                    outputURL: temporaryURL,
                    fileType: .mov
                )

            let settings: [String: Any] = [

                AVVideoCodecKey: codec,

                AVVideoWidthKey: 3840,

                AVVideoHeightKey: 2160
            ]

            let supported =
                writer.canApply(
                    outputSettings: settings,
                    forMediaType: .video
                )

            if !supported {

                writer.cancelWriting()

                try? FileManager.default.removeItem(
                    at: temporaryURL
                )

                return (
                    false,
                    "AVAssetWriter.canApply = false"
                )
            }

            try? FileManager.default.removeItem(
                at: temporaryURL
            )

            return (
                true,
                nil
            )

        } catch {

            try? FileManager.default.removeItem(
                at: temporaryURL
            )

            return (
                false,
                error.localizedDescription
            )
        }
    }

    // MARK: - Human Readable Report

    static func makeReport() -> String {

        let report = run()

        var lines: [String] = []

        lines.append(
            "LogForge ProRes Encoder Diagnostics"
        )

        lines.append(
            "================================"
        )

        lines.append("")

        // MARK: Encoder List

        lines.append(
            "【VideoToolbox 编码器列表】"
        )

        if report.encoders.isEmpty {

            lines.append(
                "❌ 没有找到 ProRes 编码器"
            )

        } else {

            for encoder in report.encoders {

                let codecName: String

                switch encoder.codec {

                case kCMVideoCodecType_AppleProRes422:

                    codecName =
                        "Apple ProRes 422"

                case kCMVideoCodecType_AppleProRes422HQ:

                    codecName =
                        "Apple ProRes 422 HQ"

                default:

                    codecName =
                        encoder.codecName
                }

                lines.append("")
                lines.append(
                    "Codec: \(codecName)"
                )

                lines.append(
                    "Encoder ID: \(encoder.encoderID)"
                )

                lines.append(
                    "Encoder Name: \(encoder.encoderName)"
                )

                lines.append(
                    "Display Name: \(encoder.displayName)"
                )

                lines.append(
                    "Hardware: " +
                    (
                        encoder.hardwareAccelerated
                        ? "YES"
                        : "NO"
                    )
                )
            }
        }

        lines.append("")

        // MARK: 3840 × 2160

        lines.append(
            "【3840 × 2160 精确编码器测试】"
        )

        lines.append("")

        lines.append(
            "ProRes 422:"
        )

        if report.proRes422Test.isAvailable {

            lines.append(
                "✅ AVAILABLE"
            )

            if let id =
                report.proRes422Test.encoderID {

                lines.append(
                    "Encoder ID: \(id)"
                )
            }

        } else {

            lines.append(
                "❌ NOT AVAILABLE"
            )

            lines.append(
                "OSStatus: " +
                "\(report.proRes422Test.status)"
            )
        }

        lines.append("")

        lines.append(
            "ProRes 422 HQ:"
        )

        if report.proRes422HQTest.isAvailable {

            lines.append(
                "✅ AVAILABLE"
            )

            if let id =
                report.proRes422HQTest.encoderID {

                lines.append(
                    "Encoder ID: \(id)"
                )
            }

        } else {

            lines.append(
                "❌ NOT AVAILABLE"
            )

            lines.append(
                "OSStatus: " +
                "\(report.proRes422HQTest.status)"
            )
        }

        lines.append("")

        // MARK: AVAssetWriter

        lines.append(
            "【AVAssetWriter 3840×2160 测试】"
        )

        lines.append("")

        lines.append(
            "ProRes 422:"
        )

        lines.append(
            report.writer422Supported
            ? "✅ canApply = TRUE"
            : "❌ canApply = FALSE"
        )

        if let error =
            report.writer422Error {

            lines.append(
                "Error: \(error)"
            )
        }

        lines.append("")

        lines.append(
            "ProRes 422 HQ:"
        )

        lines.append(
            report.writer422HQSupported
            ? "✅ canApply = TRUE"
            : "❌ canApply = FALSE"
        )

        if let error =
            report.writer422HQError {

            lines.append(
                "Error: \(error)"
            )
        }

        lines.append("")

        // MARK: Conclusion

        lines.append(
            "【诊断结论】"
        )

        lines.append("")

        if !report.hasProRes422 &&
            !report.hasProRes422HQ {

            lines.append(
                "❌ 系统没有暴露 ProRes 编码器。"
            )

            lines.append(
                "当前应重点检查设备 / iOS / VideoToolbox。"
            )

        } else if
            report.hasProRes422 &&
            !report.hasProRes422HQ {

            lines.append(
                "⚠️ 系统发现 ProRes 422，"
            )

            lines.append(
                "但没有发现 ProRes 422 HQ。"
            )

            lines.append(
                "这与当前 -11834 错误高度相关。"
            )

        } else if
            report.hasProRes422HQ &&
            !report.writer422HQSupported {

            lines.append(
                "⚠️ 系统存在 ProRes 422 HQ encoder，"
            )

            lines.append(
                "但 AVAssetWriter 不接受当前"
            )

            lines.append(
                "3840×2160 ProRes 422 HQ 设置。"
            )

        } else if
            report.hasProRes422HQ &&
            report.writer422HQSupported {

            lines.append(
                "✅ 系统存在 ProRes 422 HQ encoder。"
            )

            lines.append(
                "✅ 3840×2160 ProRes 422 HQ"
            )

            lines.append(
                "通过 AVAssetWriter 基础设置检查。"
            )

            lines.append("")

            lines.append(
                "下一步应检查实际写入阶段的"
            )

            lines.append(
                "PixelBuffer / source format /"
            )

            lines.append(
                "Apple Log metadata / encoder input"
            )

            lines.append(
                "组合，而不是 Metal 转换本身。"
            )

        } else {

            lines.append(
                "⚠️ 检测结果需要进一步分析。"
            )
        }

        lines.append("")

        lines.append(
            "================================"
        )

        return lines.joined(
            separator: "\n"
        )
    }
}
