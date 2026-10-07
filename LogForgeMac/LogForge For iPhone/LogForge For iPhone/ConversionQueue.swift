import SwiftUI
import UIKit
import AVFoundation
import PhotosUI
import UniformTypeIdentifiers
import CoreTransferable
import CryptoKit

struct ImportedMovie: Transferable {
    let url: URL
    static var transferRepresentation: some TransferRepresentation {
        FileRepresentation(contentType:.movie) { movie in SentTransferredFile(movie.url) } importing: { received in
            ImportedMovie(url:try ImportStorage.copy(received.file))
        }
    }
}

enum ImportStorage {
    static func copy(_ source: URL) throws -> URL {
        let fm = FileManager.default
        let root = fm.urls(for:.documentDirectory,in:.userDomainMask)[0].appendingPathComponent("Imports/\(UUID())",isDirectory:true)
        try fm.createDirectory(at:root,withIntermediateDirectories:true)
        let target = root.appendingPathComponent(source.lastPathComponent)
        var complete = false
        defer { if !complete { try? fm.removeItem(at:root) } }
        try Task.checkCancellation()
        let access = source.startAccessingSecurityScopedResource()
        defer { if access { source.stopAccessingSecurityScopedResource() } }
        var coordinatorError: NSError?, copyError: Error?
        NSFileCoordinator().coordinate(readingItemAt:source,options:[],error:&coordinatorError) { coordinated in
            do { try fm.copyItem(at:coordinated,to:target) } catch { copyError = error }
        }
        if let error = coordinatorError ?? copyError as NSError? { try? fm.removeItem(at:root); throw error }
        try Task.checkCancellation(); complete = true
        return target
    }
    static func scan(_ sources: [URL], recursive: Bool) throws -> [URL] {
        var found: [URL] = []
        for source in sources {
            let access = source.startAccessingSecurityScopedResource()
            defer { if access { source.stopAccessingSecurityScopedResource() } }
            let values = try source.resourceValues(forKeys:[.isDirectoryKey,.isSymbolicLinkKey])
            guard values.isSymbolicLink != true else { continue }
            if values.isDirectory == true {
                var scanError: Error?
                guard recursive, let enumerator = FileManager.default.enumerator(at:source,includingPropertiesForKeys:[.isRegularFileKey,.isSymbolicLinkKey],options:[.skipsHiddenFiles],errorHandler:{ _,error in scanError = error; return false }) else { throw NativeFailure("error.folder") }
                while let file = enumerator.nextObject() as? URL {
                    try Task.checkCancellation()
                    let info = try file.resourceValues(forKeys:[.isRegularFileKey,.isSymbolicLinkKey])
                    if info.isSymbolicLink == true { enumerator.skipDescendants(); continue }
                    if info.isRegularFile == true { found.append(file) }
                }
                if let scanError { throw scanError }
            } else { found.append(source) }
        }
        return found.sorted { $0.path.localizedStandardCompare($1.path) == .orderedAscending }
    }
}

struct QueueEntry: Identifiable, Codable {
    enum Status: String, Codable { case queued, running, complete, failed }
    let id: UUID
    var source: URL
    let sourceKey: String
    var status: Status = .queued
    var output: URL?
    var detail = ""
    var info = ""
    var attemptBuild: String?
    var attemptVersion: String?
    var attemptReleaseBuild: String?
    var attemptBundleBuild: String?
}

@MainActor
final class ConversionQueue: ObservableObject {
    @Published var entries: [QueueEntry] = []
    @Published var messages: [String] = []
    @Published var importing = false
    @Published var running = false
    @Published var progress = 0.0
    @Published var frameCount = 0
    @Published var capability = L10n.text("capability.checking")
    @Published var activeID: UUID?
    @Published var destination: URL?
    @Published var batchReport: URL?
    private var task: Task<Void,Never>?
    private var importTask: Task<Void,Never>?
    private var elapsedStart = Date()
    private var background: UIBackgroundTaskIdentifier = .invalid
    var elapsed: Double { Date().timeIntervalSince(elapsedStart) }
    var remaining: Double? { progress > 0.02 && progress < 0.94 ? elapsed * (0.94/progress-1) : nil }
    private var journal: URL {
        FileManager.default.urls(for:.applicationSupportDirectory,in:.userDomainMask)[0].appendingPathComponent("queue.json")
    }
    init() {
        let documents = FileManager.default.urls(for:.documentDirectory,in:.userDomainMask)[0]
        if let data = try? Data(contentsOf:journal), let restored = try? JSONDecoder().decode([QueueEntry].self,from:data) {
            entries = restored.map { entry in
                var e = entry
                // The sandbox's absolute container path may change after an update.
                let folder = e.source.deletingLastPathComponent().lastPathComponent
                if UUID(uuidString:folder) != nil {
                    e.source = documents.appendingPathComponent("Imports/\(folder)").appendingPathComponent(e.source.lastPathComponent)
                }
                if let output = e.output { e.output = documents.appendingPathComponent("Exports").appendingPathComponent(output.lastPathComponent) }
                if e.status == .running { e.status = .queued }
                return e
            }
        }
        let exports = FileManager.default.urls(for:.documentDirectory,in:.userDomainMask)[0].appendingPathComponent("Exports")
        if let files = try? FileManager.default.contentsOfDirectory(at:exports,includingPropertiesForKeys:nil) {
            for file in files where file.lastPathComponent.hasPrefix(".LogForge-") && (file.lastPathComponent.hasSuffix(".partial.mov") || file.lastPathComponent.hasSuffix(".partial.mov.json")) {
                NativeExportFiles.shared.removeAbandoned(file)
            }
        }
        if let data = UserDefaults.standard.data(forKey:"outputFolderBookmark") {
            var stale = false
            if let url = try? URL(resolvingBookmarkData:data,options:[],relativeTo:nil,bookmarkDataIsStale:&stale), !stale { destination = url }
        }
        let report = journal.deletingLastPathComponent().appendingPathComponent("batch-report.json")
        if FileManager.default.fileExists(atPath:report.path) { batchReport = report }
    }
    func assess() async {
        let hq = await NativeCapabilities.shared.supports(width:64,height:64,quality:.proRes422HQ)
        let standard = await NativeCapabilities.shared.supports(width:64,height:64,quality:.proRes422)
        capability = [L10n.text(MetalColorProcessor.isAvailable ? "capability.metal" : "capability.cpu"),
            L10n.text(hq && standard ? "capability.system" : "capability.software")].joined(separator:" · ")
    }
    func persist() {
        do {
            try FileManager.default.createDirectory(at:journal.deletingLastPathComponent(),withIntermediateDirectories:true)
            try JSONEncoder().encode(entries).write(to:journal,options:.atomic)
        } catch { messages.append(error.localizedDescription) }
    }
    func chooseDestination(_ url: URL) {
        let access = url.startAccessingSecurityScopedResource(); defer { if access { url.stopAccessingSecurityScopedResource() } }
        do {
            let bookmark = try url.bookmarkData(options:.minimalBookmark,includingResourceValuesForKeys:nil,relativeTo:nil)
            UserDefaults.standard.set(bookmark,forKey:"outputFolderBookmark"); destination = url
        } catch { messages.append(error.localizedDescription) }
    }
    func importURLs(_ sources: [URL], recursive: Bool) {
        guard !running, !importing else { return }
        importing = true
        importTask = Task {
            // Hold the selected root's scope until every nested source has been copied.
            let scopes = sources.filter { $0.startAccessingSecurityScopedResource() }
            defer { scopes.forEach { $0.stopAccessingSecurityScopedResource() }; importing = false; importTask = nil; persist() }
            do {
                let scanner = Task.detached { try ImportStorage.scan(sources,recursive:recursive) }
                let urls = try await withTaskCancellationHandler(operation:{ try await scanner.value },onCancel:{ scanner.cancel() })
                for source in urls {
                    try Task.checkCancellation()
                    let key = source.standardizedFileURL.path
                    if entries.contains(where:{ $0.sourceKey == key }) { continue }
                    guard source.pathExtension.lowercased() == "mov" else { messages.append(source.lastPathComponent+": "+L10n.text("error.input")); continue }
                    do {
                        let copy = Task.detached { try ImportStorage.copy(source) }
                        let url = try await withTaskCancellationHandler(operation:{ try await copy.value },onCancel:{ copy.cancel() })
                        await admit(url,key:key)
                    } catch { messages.append(source.lastPathComponent+": "+error.localizedDescription) }
                }
            } catch is CancellationError {} catch { messages.append(error.localizedDescription) }
        }
    }
    func importPhotos(_ items: [PhotosPickerItem]) {
        guard !running, !importing else { return }
        importing = true
        importTask = Task {
            defer { importing = false; importTask = nil; persist() }
            for item in items {
                if Task.isCancelled { break }
                let key = "photos:" + (item.itemIdentifier ?? UUID().uuidString)
                if entries.contains(where:{ $0.sourceKey == key }) { continue }
                do {
                    guard let movie = try await item.loadTransferable(type:ImportedMovie.self) else { throw NativeFailure("error.input") }
                    await admit(movie.url,key:key)
                } catch { messages.append(error.localizedDescription) }
            }
        }
    }
    private func admit(_ url: URL, key: String) async {
        do {
            try Task.checkCancellation()
            let contract = try await InputContract.inspect(AVURLAsset(url:url))
            let info = try await VideoAnalyzer().analyze(url:url)
            try Task.checkCancellation()
            entries.append(QueueEntry(id:UUID(),source:url,sourceKey:key,detail:contract.warnings.joined(separator:"\n"),
                info:"\(info.width) × \(info.height) · \(info.codec) · \(String(format:"%.2f",info.frameRate)) fps · \(String(format:"%.1f",info.duration)) s"))
            persist()
        } catch {
            messages.append(url.lastPathComponent+": "+error.localizedDescription)
            try? FileManager.default.removeItem(at:url.deletingLastPathComponent())
        }
    }
    func remove(_ id: UUID) {
        guard !running, !importing, let i = entries.firstIndex(where:{ $0.id == id }) else { return }
        let url = entries.remove(at:i).source
        let managedRoot = FileManager.default.urls(for:.documentDirectory,in:.userDomainMask)[0].appendingPathComponent("Imports").standardizedFileURL.path + "/"
        if url.standardizedFileURL.path.hasPrefix(managedRoot) { try? FileManager.default.removeItem(at:url.deletingLastPathComponent()) }
        persist()
    }
    func retryFailures() { guard !running else { return }; for i in entries.indices where entries[i].status == .failed { entries[i].status = .queued }; persist() }
    func cancel() { task?.cancel(); importTask?.cancel() }
    func start(quality: ProResQuality, options: ConversionOptions) {
        guard !running, !importing, entries.contains(where:{ $0.status == .queued }) else { return }
        running = true
        batchReport = nil
        let exportDirectory = destination
        background = UIApplication.shared.beginBackgroundTask(withName:"LogForge conversion") { [weak self] in Task { @MainActor in self?.cancel() } }
        task = Task {
            defer {
                running = false; activeID = nil; task = nil
                if background != .invalid { UIApplication.shared.endBackgroundTask(background); background = .invalid }
                persist()
            }
            for i in entries.indices where entries[i].status == .queued {
                if Task.isCancelled { break }
                entries[i].attemptBuild = AppBuild.diagnosticLabel
                entries[i].attemptVersion = AppBuild.version
                entries[i].attemptReleaseBuild = nil
                entries[i].attemptBundleBuild = AppBuild.buildNumber
                entries[i].status = .running; activeID = entries[i].id; progress = 0; frameCount = 0; elapsedStart = Date(); persist()
                let source = entries[i].source, jobID = entries[i].id
                do {
                    let worker = Task.detached(priority:.userInitiated) {
                        try await VideoProcessor().process(url:source,quality:quality,options:options) { fraction,count in
                            Task { @MainActor in if self.activeID == jobID { self.progress = fraction; self.frameCount = count } }
                        }
                    }
                    let result = try await withTaskCancellationHandler(operation:{ try await worker.value },onCancel:{ worker.cancel() })
                    entries[i].output = result.outputURL
                    entries[i].status = .complete; entries[i].detail = result.diagnostics.warnings.joined(separator:"\n")
                    if let folder = exportDirectory {
                        do {
                            let exporter = Task.detached { try Self.export(result.outputURL,to:folder) }
                            _ = try await withTaskCancellationHandler(operation:{ try await exporter.value },onCancel:{ exporter.cancel() })
                        }
                        catch { entries[i].detail += "\n"+L10n.text("warning.export")+"\n"+error.localizedDescription }
                    }
                    Haptics.play(.success)
                } catch is CancellationError { entries[i].status = .queued; entries[i].detail = L10n.text("status.cancelled"); break }
                catch { entries[i].status = .failed; entries[i].detail = error.localizedDescription; Haptics.play(.failure) }
                persist()
            }
            let report = entries.map { ["name":$0.source.lastPathComponent,"status":$0.status.rawValue,"detail":$0.detail,"build":$0.attemptBuild ?? "unknown",
                "appVersion":$0.attemptVersion ?? "unknown","releaseBuild":$0.attemptReleaseBuild ?? "unknown","bundleBuild":$0.attemptBundleBuild ?? "unknown"] }
            do {
                let reportURL = journal.deletingLastPathComponent().appendingPathComponent("batch-report.json")
                try JSONSerialization.data(withJSONObject:report,options:.prettyPrinted).write(to:reportURL,options:.atomic)
                batchReport = reportURL
            }
            catch { messages.append(error.localizedDescription) }
        }
    }
    nonisolated static func export(_ source: URL, to folder: URL) throws -> URL {
        try Task.checkCancellation()
        let access = folder.startAccessingSecurityScopedResource(); defer { if access { folder.stopAccessingSecurityScopedResource() } }
        var output = folder.appendingPathComponent(source.lastPathComponent), suffix = 0
        while FileManager.default.fileExists(atPath:output.path) {
            suffix += 1; output = folder.appendingPathComponent(source.deletingPathExtension().lastPathComponent+"_\(suffix).mov")
        }
        let partial = folder.appendingPathComponent(".LogForge-\(UUID()).partial.mov")
        defer { try? FileManager.default.removeItem(at:partial) }
        var coordinationError: NSError?, failure: Error?
        NSFileCoordinator().coordinate(writingItemAt:folder,options:[],error:&coordinationError) { coordinated in
            let owned = coordinated.appendingPathComponent(partial.lastPathComponent)
            defer { try? FileManager.default.removeItem(at:owned) }
            do {
                try Task.checkCancellation()
                try FileManager.default.copyItem(at:source,to:owned)
                guard try digest(source) == digest(owned) else { throw NativeFailure("error.validation") }
                try Task.checkCancellation()
                try FileManager.default.moveItem(at:owned,to:coordinated.appendingPathComponent(output.lastPathComponent))
                output = coordinated.appendingPathComponent(output.lastPathComponent)
            } catch { failure = error }
        }
        if let error = coordinationError ?? failure as NSError? { throw error }
        return output
    }
    nonisolated private static func digest(_ url: URL) throws -> Data {
        let file = try FileHandle(forReadingFrom:url); defer { try? file.close() }
        var hash = SHA256()
        while let chunk = try file.read(upToCount:1024*1024), !chunk.isEmpty { try Task.checkCancellation(); hash.update(data:chunk) }
        return Data(hash.finalize())
    }

}
