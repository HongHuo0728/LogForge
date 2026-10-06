import Foundation

struct VideoInfo {

    var fileName: String = ""

    var width: Int = 0
    var height: Int = 0

    var frameRate: Double = 0
    var duration: Double = 0

    var codec: String = "Unknown"
    var pixelFormat: String = "Unknown"

    var colorPrimaries: String = "Unknown"
    var transferFunction: String = "Unknown"
    var matrix: String = "Unknown"

    var isProRes: Bool = false
    var isHLG: Bool = false
    var isBT2020: Bool = false

    var inputAccepted = false

    var isSupported: Bool { inputAccepted }
}
