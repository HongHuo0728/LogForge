import SwiftUI

/// Decisions use the local container, never a device model, screen or hinge angle.
enum WorkspaceLayout: String {
    case single, horizontal, vertical

    static func resolve(size: CGSize, regularWidth: Bool, regularHeight: Bool,
                        accessibilityText: Bool, divisions: [CGRect] = []) -> Self {
        let bounds = CGRect(origin: .zero, size: size)
        for division in divisions {
            let frame = division.intersection(bounds)
            guard !frame.isNull else { continue }
            if frame.height > frame.width, frame.minX > 1, frame.maxX < size.width - 1 {
                return .horizontal
            }
            if frame.width > frame.height, frame.minY > 1, frame.maxY < size.height - 1 {
                return .vertical
            }
        }
        // Two readable panes, rather than a table of known iPhone dimensions.
        guard regularWidth, regularHeight, !accessibilityText else { return .single }
        if size.width >= size.height {
            return size.width >= 640 && size.height >= 300 ? .horizontal : .single
        }
        return size.width >= 500 && size.height >= 680 ? .vertical : .single
    }
}

struct AdaptiveWorkspace<Primary: View, Secondary: View, Compact: View>: View {
    @Environment(\.horizontalSizeClass) private var horizontalSizeClass
    @Environment(\.verticalSizeClass) private var verticalSizeClass
    @Environment(\.dynamicTypeSize) private var dynamicTypeSize
    let primary: Primary
    let secondary: Secondary
    let compact: Compact

    init(@ViewBuilder primary: () -> Primary, @ViewBuilder secondary: () -> Secondary,
         @ViewBuilder compact: () -> Compact) {
        self.primary = primary(); self.secondary = secondary(); self.compact = compact()
    }

    var body: some View {
        GeometryReader { proxy in
            if #available(iOS 27.1, *) {
                let divisions = proxy.reservedRegions(kind: .division).map(\.frame)
                let layout = resolve(proxy.size, divisions: divisions)
                Group {
                    if layout == .single {
                        compact
                    } else {
                        // Native arrangements track the actual fold, including its margins.
                        // Each child owns its ScrollView; no scroll view encloses the arrangement.
                        ArrangementView {
                            primary
                        } secondary: {
                            secondary
                        }.arrangementViewStyle(.split)
                    }
                }
                .workspaceDiagnostics(layout, size: proxy.size, divisions: divisions)
            } else {
                let layout = resolve(proxy.size)
                Group {
                    if layout == .single { compact }
                    else {
                        let stack = layout == .horizontal
                            ? AnyLayout(HStackLayout(alignment: .top, spacing: 0))
                            : AnyLayout(VStackLayout(spacing: 0))
                        stack {
                            primary.frame(maxWidth: .infinity, maxHeight: .infinity)
                            secondary.frame(maxWidth: .infinity, maxHeight: .infinity)
                        }
                    }
                }.workspaceDiagnostics(layout, size: proxy.size, divisions: [])
            }
        }
    }

    private func resolve(_ size: CGSize, divisions: [CGRect] = []) -> WorkspaceLayout {
        WorkspaceLayout.resolve(size: size, regularWidth: horizontalSizeClass == .regular,
            regularHeight: verticalSizeClass == .regular,
            accessibilityText: dynamicTypeSize.isAccessibilitySize, divisions: divisions)
    }
}

/// Reflows label/control rows instead of truncating long paths or large text.
struct AdaptiveControlRow<Control: View>: View {
    @Environment(\.dynamicTypeSize) private var dynamicTypeSize
    let title: String
    let control: Control
    init(_ title: String, @ViewBuilder control: () -> Control) {
        self.title = title; self.control = control()
    }
    var body: some View {
        ViewThatFits(in: .horizontal) {
            if !dynamicTypeSize.isAccessibilitySize {
                HStack(spacing: 16) {
                    Text(title).fixedSize(horizontal: true, vertical: false)
                    Spacer(minLength: 0)
                    control.fixedSize(horizontal: true, vertical: false)
                }
            }
            VStack(alignment: .leading, spacing: 10) {
                Text(title)
                control.fixedSize(horizontal: false, vertical: true)
            }.frame(maxWidth: .infinity, alignment: .leading)
        }
    }
}

extension View {
    @ViewBuilder
    fileprivate func workspaceDiagnostics(_ layout: WorkspaceLayout, size: CGSize,
                                           divisions: [CGRect]) -> some View {
        #if DEBUG
        if ProcessInfo.processInfo.arguments.contains("--ui-testing") {
            self.overlay(alignment: .bottomLeading) {
                // Test-only telemetry reports real geometry. It never overrides the layout.
                Text("\(layout.rawValue)|\(Int(size.width))|\(Int(size.height))|\(divisions.count)")
                    .font(.system(size: 1)).frame(width: 1, height: 1).clipped()
                    .accessibilityIdentifier("workspaceGeometry")
                    .allowsHitTesting(false)
            }
        } else { self }
        #else
        self
        #endif
    }
}
