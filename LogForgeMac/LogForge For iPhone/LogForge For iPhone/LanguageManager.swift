import SwiftUI
import UIKit

@MainActor
final class LanguageManager: ObservableObject {
    static let shared = LanguageManager()
    enum AppLanguage: String { case chinese = "zh-Hans", traditionalChinese = "zh-Hant", english = "en", french = "fr", spanish = "es" }
    var language: AppLanguage { AppLanguage(rawValue:Bundle.main.preferredLocalizations.first ?? "en") ?? .english }
    var locale: Locale { Locale(identifier:language.rawValue) }
    func openSettings() {
        guard let url = URL(string:UIApplication.openSettingsURLString) else { return }
        UIApplication.shared.open(url)
    }
    func toggle() { openSettings() }
}
