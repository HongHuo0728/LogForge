import SwiftUI
import PhotosUI
import UniformTypeIdentifiers

struct ContentView: View {
    @StateObject private var queue = ConversionQueue()
    @AppStorage("proResQuality") private var quality: ProResQuality = .proRes422HQ
    @State private var options = ConversionOptions()
    @State private var draft = ConversionOptions()
    @State private var draftAppearance = "system"
    @State private var draftHaptics = true
    @AppStorage("conversionOptions") private var savedOptions = Data()
    @State private var showSources = false
    @State private var pendingSourceAction: (() -> Void)? = nil
    @State private var showPhotos = false
    @State private var showFiles = false
    @State private var showSettings = false
    @State private var photos: [PhotosPickerItem] = []
    @State private var picker: PickerPurpose = .files
    @AppStorage("appearance") private var appearance = "system"
    @AppStorage("hapticsEnabled") private var haptics = true
    @Environment(\.colorScheme) private var scheme
    private enum PickerPurpose { case files, folder, destination }
    private var busy: Bool { queue.running || queue.importing }
    private var appBackground: some View {
        LinearGradient(colors:scheme == .dark ? [Color(red:0.025,green:0.06,blue:0.12),Color(red:0.05,green:0.12,blue:0.2),Color(red:0.10,green:0.08,blue:0.18)] : [Color(red:0.90,green:0.96,blue:0.99),Color(red:0.84,green:0.92,blue:0.97),Color(red:0.94,green:0.91,blue:0.99)],startPoint:.topLeading,endPoint:.bottomTrailing).ignoresSafeArea()
    }
    var body: some View {
        NavigationStack {
            ZStack {
                appBackground
                ScrollView {
                    GlassEffectContainer(spacing:12) {
                        VStack(spacing:18) {
                            header
                            importButton
                            conversionCard
                            activityCard
                            queueCard
                            noticesCard
                        }.padding(.horizontal,20).padding(.top,16).padding(.bottom,110).frame(maxWidth:900)
                    }.frame(maxWidth:.infinity)
                }
            }
            .toolbar {
                ToolbarItem(placement:.topBarLeading) {
                    Button { LanguageManager.shared.openSettings(); Haptics.play(.small) } label: { Image(systemName:"globe") }
                        .buttonStyle(GlassActionStyle(compact:true))
                        .accessibilityLabel(L10n.text("language")).accessibilityIdentifier("languageSettings")
                }.sharedBackgroundVisibility(.hidden)
                ToolbarItem(placement:.topBarTrailing) {
                    Button { draft = options; draftAppearance = appearance; draftHaptics = haptics; showSettings = true; Haptics.play(.small) } label: { Image(systemName:"slider.horizontal.3") }
                        .buttonStyle(GlassActionStyle(compact:true))
                        .disabled(busy).accessibilityLabel(L10n.text("settings")).accessibilityIdentifier("conversionSettings")
                }.sharedBackgroundVisibility(.hidden)
            }
            .safeAreaInset(edge:.bottom) {
                if !busy {
                    Button { queue.start(quality:quality,options:options); Haptics.play(.large) } label: {
                        Label(L10n.text("convert"),systemImage:"arrow.triangle.2.circlepath").font(.headline).frame(maxWidth:.infinity)
                    }.buttonStyle(GlassActionStyle()).disabled(!queue.entries.contains(where:{ $0.status == .queued }))
                        .padding(.horizontal,20).padding(.bottom,10).frame(maxWidth:900)
                }
            }
        }
        .preferredColorScheme(appearance == "system" ? nil : appearance == "dark" ? .dark : .light)
        .task { if let restored = try? JSONDecoder().decode(ConversionOptions.self,from:savedOptions), (try? restored.validate()) != nil { options = restored }; await queue.assess() }
        .onChange(of:options) { _, value in savedOptions = (try? JSONEncoder().encode(value)) ?? Data() }
        .photosPicker(isPresented:$showPhotos,selection:$photos,matching:.videos,preferredItemEncoding:.current)
        .onChange(of:photos) { _, values in if !values.isEmpty { queue.importPhotos(values); photos = [] } }
        .fileImporter(isPresented:$showFiles,allowedContentTypes:picker == .files ? [.movie] : [.folder],allowsMultipleSelection:picker == .files) { result in
            switch result {
            case .success(let urls):
                if picker == .destination, let url = urls.first { queue.chooseDestination(url) }
                else { queue.importURLs(urls,recursive:picker == .folder) }
            case .failure(let error): queue.messages.append(error.localizedDescription)
            }
        }
        .sheet(isPresented:$showSources,onDismiss:{
            let action = pendingSourceAction; pendingSourceAction = nil; action?()
        }) {
            GlassEffectContainer(spacing:12) {
                VStack(spacing:16) {
                    Text(L10n.text("import.title")).font(.headline)
                    sourceButton("import.photos","photo.on.rectangle") { showPhotos = true }
                    sourceButton("import.files","doc") { picker = .files; showFiles = true }
                    sourceButton("import.folder","folder") { picker = .folder; showFiles = true }
                    sourceButton("cancel","xmark") {}
                }.padding(24).clearGlass().padding(16)
            }.presentationDetents([.height(380)]).presentationBackground(.clear)
        }
        .sheet(isPresented:$showSettings) { settings }
    }
    private var header: some View {
        GlassCard("") {
            Text("LogForge").font(.largeTitle.bold())
            Text(AppBuild.label).font(.caption).foregroundStyle(.secondary).accessibilityIdentifier("appBuild")
            Text(L10n.text("subtitle")).font(.subheadline)
            Text(queue.capability).font(.footnote).foregroundStyle(.secondary).accessibilityIdentifier("capability")
        }
    }
    private var importButton: some View {
        Button { Haptics.play(.large); showSources = true } label: {
            VStack(spacing:12) {
                Image(systemName:"video.badge.plus").font(.system(size:36,weight:.light))
                Text(L10n.text("import.title")).font(.headline)
                Text(L10n.text("import.hint")).font(.footnote)
            }.frame(maxWidth:.infinity).padding(.vertical,18)
        }.buttonStyle(GlassActionStyle()).disabled(busy).accessibilityIdentifier("importVideo")
    }
    private var conversionCard: some View {
        GlassCard(L10n.text("conversion.title")) {
            HStack {
                Text(L10n.text("quality")); Spacer()
                Menu { ForEach(ProResQuality.allCases) { q in Button(q.displayName) { quality = q; Haptics.play(.small) } } } label: { Label(quality.displayName,systemImage:"chevron.down").padding(12).clearGlass(interactive:true,radius:16) }
            }
            HStack {
                Text(L10n.text("backend")); Spacer()
                Menu { ForEach(ColorBackend.allCases) { b in Button(b.title) { options.backend = b; Haptics.play(.small) } } } label: { Label(options.backend.title,systemImage:"chevron.down").padding(12).clearGlass(interactive:true,radius:16) }
            }
            Text(L10n.text("encoding.autoHint")).font(.footnote).foregroundStyle(.secondary)
            HStack {
                Text(L10n.text("destination")); Spacer()
                Button { picker = .destination; showFiles = true; Haptics.play(.medium) } label: {
                    Label(queue.destination?.lastPathComponent ?? L10n.text("destination.default"),systemImage:"folder")
                }.buttonStyle(GlassActionStyle())
            }
        }.disabled(busy)
    }
    @ViewBuilder
    private var activityCard: some View {
        if queue.running || queue.importing {
            GlassCard(L10n.text(queue.importing ? "status.importing" : queue.progress >= 0.95 ? "status.validating" : "status.running")) {
                if queue.importing { ProgressView() } else { ProgressView(value:queue.progress).tint(.primary) }
                if queue.running {
                    Text("\(Int(queue.progress*100))% · \(queue.frameCount) " + L10n.text("frames")).monospacedDigit()
                    if let remaining = queue.remaining { Text(L10n.text("remaining")+" "+Duration.seconds(remaining).formatted(.time(pattern:.minuteSecond))).font(.footnote).monospacedDigit() }
                }
                Button { queue.cancel(); Haptics.play(.medium) } label: { Label(L10n.text("cancel"),systemImage:"xmark") }.buttonStyle(GlassActionStyle())
            }
        }
    }
    @ViewBuilder
    private var queueCard: some View {
        if !queue.entries.isEmpty {
            GlassCard(L10n.text("queue.title")) {
                ForEach(queue.entries) { entry in
                    VStack(alignment:.leading,spacing:10) {
                        HStack {
                            Text(entry.source.lastPathComponent).font(.headline).lineLimit(3); Spacer()
                            if !busy {
                                Button { queue.remove(entry.id); Haptics.play(.small) } label: { Image(systemName:"minus.circle") }
                                    .buttonStyle(GlassActionStyle()).accessibilityLabel(L10n.text("remove"))
                            }
                        }
                        Text(entry.info).font(.footnote).textSelection(.enabled)
                        Text(L10n.text("status.\(entry.status.rawValue)")).font(.subheadline.bold())
                        if !entry.detail.isEmpty { Text(entry.detail).font(.footnote).textSelection(.enabled) }
                        if let output = entry.output {
                            ShareLink(item:output) { Label(L10n.text("share"),systemImage:"square.and.arrow.up") }.buttonStyle(GlassActionStyle())
                                .simultaneousGesture(TapGesture().onEnded { Haptics.play(.medium) })
                            ShareLink(item:output.appendingPathExtension("json")) { Label(L10n.text("report"),systemImage:"doc.text") }.buttonStyle(GlassActionStyle())
                                .simultaneousGesture(TapGesture().onEnded { Haptics.play(.small) })
                        }
                    }.frame(maxWidth:.infinity,alignment:.leading).padding(16).clearGlass(radius:18)
                }
                if queue.entries.contains(where:{ $0.status == .failed }) {
                    Button { queue.retryFailures(); Haptics.play(.medium) } label: { Text(L10n.text("retry")) }.buttonStyle(GlassActionStyle()).disabled(busy)
                }
                if let report = queue.batchReport, !busy {
                    ShareLink(item:report) { Label(L10n.text("report.batch"),systemImage:"doc.text") }.buttonStyle(GlassActionStyle())
                        .simultaneousGesture(TapGesture().onEnded { Haptics.play(.small) })
                }
            }
        }
    }
    @ViewBuilder
    private var noticesCard: some View {
        if !queue.messages.isEmpty {
            GlassCard(L10n.text("messages")) {
                ForEach(Array(queue.messages.enumerated()),id:\.offset) { _, message in Text(message).font(.footnote).textSelection(.enabled) }
                Button { queue.messages.removeAll(); Haptics.play(.small) } label: { Text(L10n.text("dismiss")) }.buttonStyle(GlassActionStyle())
            }
        }
    }
    private func sourceButton(_ key: String, _ icon: String, action: @escaping () -> Void) -> some View {
        Button {
            pendingSourceAction = action; showSources = false; Haptics.play(.medium)
        } label: { Label(L10n.text(key),systemImage:icon).frame(maxWidth:.infinity) }.buttonStyle(GlassActionStyle())
    }
    private var settings: some View {
        NavigationStack {
            ScrollView {
                GlassEffectContainer(spacing:12) {
                    VStack(spacing:18) {
                        GlassCard(L10n.text("version.title")) {
                            Text(AppBuild.label).font(.headline).textSelection(.enabled).accessibilityIdentifier("settingsVersion")
                        }
                        GlassCard(L10n.text("exposure")) { optionMenu("exposure",value:$draft.exposure,choices:ConversionOptions.exposureChoices,percentage:false) }
                        GlassCard(L10n.text("creative")) {
                            Button { draft.creativeEnabled.toggle(); Haptics.play(.medium) } label: {
                                Label(L10n.text(draft.creativeEnabled ? "enabled" : "disabled"),systemImage:draft.creativeEnabled ? "checkmark.circle" : "circle").frame(maxWidth:.infinity)
                            }.buttonStyle(GlassActionStyle())
                            optionMenu("shadow",value:$draft.shadow,choices:ConversionOptions.toneChoices,percentage:false).disabled(!draft.creativeEnabled)
                            optionMenu("highlight",value:$draft.highlight,choices:ConversionOptions.toneChoices,percentage:false).disabled(!draft.creativeEnabled)
                            optionMenu("saturation",value:$draft.saturation,choices:ConversionOptions.saturationChoices,percentage:true).disabled(!draft.creativeEnabled)
                        }
                        GlassCard(L10n.text("appearance")) {
                            Menu { ForEach(["system","light","dark"],id:\.self) { v in Button(L10n.text("appearance.\(v)")) { draftAppearance = v; Haptics.play(.small) } } } label: { Label(L10n.text("appearance.\(draftAppearance)"),systemImage:"chevron.down").frame(maxWidth:.infinity).padding(14).clearGlass(interactive:true,radius:18) }
                            Button { draftHaptics.toggle(); Haptics.play(.small) } label: {
                                Label(L10n.text("haptics")+": "+L10n.text(draftHaptics ? "enabled" : "disabled"),systemImage:"waveform")
                            }.buttonStyle(GlassActionStyle())
                        }
                        NavigationLink { LicenseView() } label: { Label(L10n.text("licenses"),systemImage:"doc.text") }.buttonStyle(GlassActionStyle()).accessibilityIdentifier("openSourceLicenses")
                            .simultaneousGesture(TapGesture().onEnded { Haptics.play(.small) })
                        HStack {
                            Button { showSettings = false; Haptics.play(.medium) } label: { Text(L10n.text("cancel")) }.buttonStyle(GlassActionStyle()); Spacer()
                            Button { options = draft; savedOptions = (try? JSONEncoder().encode(draft)) ?? Data(); appearance = draftAppearance; haptics = draftHaptics; showSettings = false; Haptics.play(.medium) } label: { Text(L10n.text("save")) }.buttonStyle(GlassActionStyle())
                        }
                    }.padding(20).frame(maxWidth:700)
                }.frame(maxWidth:.infinity)
            }.navigationTitle(L10n.text("settings"))
        }.presentationBackground { appBackground }
    }
    private func optionMenu(_ key: String, value: Binding<Float>, choices: [Float], percentage: Bool) -> some View {
        func label(_ v: Float) -> String { percentage ? "\(Int((v*100).rounded()))%" : String(format:"%g EV",v) }
        return HStack {
            Text(L10n.text(key)); Spacer()
            Menu { ForEach(choices,id:\.self) { v in Button(label(v)) { value.wrappedValue = v; Haptics.play(.small) } } } label: { Label(label(value.wrappedValue),systemImage:"chevron.down").padding(14).clearGlass(interactive:true,radius:18) }
        }
    }
}

private struct LicenseView: View {
    // Read once, outside body/layout. Break up the long legal text so navigation
    // does not repeatedly load and measure the entire document as a single Text.
    private static let paragraphs = ["NOTICE","SoftwareCodec-LICENSE"].compactMap { name in
        Bundle.main.url(forResource:name,withExtension:"txt").flatMap { try? String(contentsOf:$0,encoding:.utf8) }
    }.joined(separator:"\n\n").components(separatedBy:"\n\n").filter { !$0.isEmpty }
    var body: some View {
        GlassEffectContainer {
            ScrollView {
                LazyVStack(alignment:.leading,spacing:12) {
                    ForEach(Array(Self.paragraphs.enumerated()),id:\.offset) { _, paragraph in
                        Text(paragraph).font(.footnote).textSelection(.enabled).frame(maxWidth:.infinity,alignment:.leading)
                    }
                }.padding(20)
            }.accessibilityIdentifier("licenseDocument")
                // Glass belongs to the fixed viewport, not the changing height
                // of the long document. Scrolling cannot resize its white scrim.
                .frame(maxWidth:700,maxHeight:.infinity).clearGlass().padding(20)
        }.frame(maxWidth:.infinity,maxHeight:.infinity)
            .navigationTitle(L10n.text("licenses")).navigationBarTitleDisplayMode(.inline)
    }
}
