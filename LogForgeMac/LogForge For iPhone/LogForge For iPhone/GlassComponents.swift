import SwiftUI
import CoreHaptics
import UIKit

enum HapticKind: Equatable { case small, medium, large, success, failure }
@MainActor
enum Haptics {
    private static var engine: CHHapticEngine?
    private static var lastTick = Date.distantPast
    static func play(_ kind: HapticKind) {
        guard UserDefaults.standard.object(forKey:"hapticsEnabled") as? Bool != false else { return }
        if kind == .small {
            guard Date().timeIntervalSince(lastTick) > 0.08 else { return }; lastTick = Date()
        }
        guard CHHapticEngine.capabilitiesForHardware().supportsHaptics else { return }
        do {
            if engine == nil {
                let e = try CHHapticEngine(); e.isAutoShutdownEnabled = true
                e.resetHandler = { Task { @MainActor in engine = nil } }
                e.stoppedHandler = { _ in Task { @MainActor in engine = nil } }
                engine = e
            }
            guard let engine else { return }; try engine.start()
            let intensity: Float = kind == .small ? 0.25 : kind == .medium ? 0.45 : 0.7
            let sharpness: Float = kind == .small ? 0.35 : kind == .medium ? 0.5 : 0.65
            var events = [CHHapticEvent(eventType:.hapticTransient,parameters:[.init(parameterID:.hapticIntensity,value:intensity),.init(parameterID:.hapticSharpness,value:sharpness)],relativeTime:0)]
            if kind == .success || kind == .failure {
                events.append(CHHapticEvent(eventType:.hapticTransient,parameters:[.init(parameterID:.hapticIntensity,value:kind == .success ? 0.5 : 0.85)],relativeTime:kind == .success ? 0.12 : 0.2))
            }
            let player = try engine.makePlayer(with:CHHapticPattern(events:events,parameters:[])); try player.start(atTime:CHHapticTimeImmediate)
        } catch {
            if kind == .success || kind == .failure {
                UINotificationFeedbackGenerator().notificationOccurred(kind == .success ? .success : .error)
            } else { UIImpactFeedbackGenerator(style:kind == .small ? .light : kind == .medium ? .medium : .heavy).impactOccurred() }
        }
    }
}

struct ClearGlass: ViewModifier {
    @Environment(\.accessibilityReduceTransparency) private var reduceTransparency
    @Environment(\.colorSchemeContrast) private var contrast
    @Environment(\.colorScheme) private var scheme
    let interactive: Bool
    let radius: CGFloat
    func body(content: Content) -> some View {
        let shape = RoundedRectangle(cornerRadius:radius,style:.continuous)
        // The scrim sits BELOW clear glass, never changes text opacity or substitutes ordinary Material.
        let scrim = reduceTransparency ? 0.78 : contrast == .increased ? 0.32 : 0.14
        content
            .background((scheme == .dark ? Color.black : Color.white).opacity(scrim),in:shape)
            .glassEffect((reduceTransparency ? Glass.regular : Glass.clear).interactive(interactive),in:shape)
    }
}
extension View {
    func clearGlass(interactive: Bool = false, radius: CGFloat = 24) -> some View { modifier(ClearGlass(interactive:interactive,radius:radius)) }
}

struct GlassActionStyle: ButtonStyle {
    var compact = false
    @Environment(\.isEnabled) private var enabled
    @Environment(\.accessibilityReduceMotion) private var reduceMotion
    func makeBody(configuration: Configuration) -> some View {
        configuration.label.padding(.horizontal,compact ? 12 : 18).padding(.vertical,compact ? 10 : 14)
            .frame(minWidth:44,minHeight:44)
            .foregroundStyle(enabled ? Color.primary : Color.secondary)
            .clearGlass(interactive:enabled,radius:compact ? 16 : 20)
            .scaleEffect(configuration.isPressed && !reduceMotion ? 0.97 : 1)
            .animation(reduceMotion ? nil : .snappy(duration:0.18),value:configuration.isPressed)
    }
}

struct GlassCard<Content: View>: View {
    let title: String
    let content: Content
    init(_ title: String, @ViewBuilder content: () -> Content) { self.title = title; self.content = content() }
    var body: some View {
        VStack(alignment:.leading,spacing:16) {
            if !title.isEmpty { Text(title).font(.headline) }
            content
        }.frame(maxWidth:.infinity,alignment:.leading).padding(20).clearGlass()
    }
}
