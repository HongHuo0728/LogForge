import XCTest
import UniformTypeIdentifiers
@testable import LogForge_For_iPhone

final class WorkspaceLayoutTests: XCTestCase {
    func testFlatWorkspaceAndCompactFallbackKeepBothSectionsReachable() {
        XCTAssertEqual(layout(860, 600), .horizontal)
        XCTAssertEqual(layout(600, 860), .vertical)
        XCTAssertEqual(layout(430, 680, regular: false), .single)
        XCTAssertEqual(layout(680, 430, regular: false), .single)
        XCTAssertEqual(layout(600, 400), .single)
        XCTAssertEqual(layout(860, 600, largeText: true), .single)
    }

    func testActiveFoldWinsOverAspectRatioAndLargeText() {
        // Division frames are local geometry, not hard-coded hardware dimensions.
        XCTAssertEqual(layout(860, 600, largeText: true,
            divisions: [CGRect(x: 420, y: 0, width: 20, height: 600)]), .horizontal)
        XCTAssertEqual(layout(600, 860,
            divisions: [CGRect(x: 0, y: 420, width: 600, height: 20)]), .vertical)
        XCTAssertEqual(layout(600, 860,
            divisions: [CGRect(x: 290, y: 0, width: 20, height: 860)]), .horizontal)
    }

    func testFoldOutsideOrAtWindowEdgeDoesNotCreateAnEmptyPane() {
        XCTAssertEqual(layout(400, 600, regular: false,
            divisions: [CGRect(x: -20, y: 0, width: 20, height: 600)]), .single)
        XCTAssertEqual(layout(400, 600, regular: false,
            divisions: [CGRect(x: 700, y: 0, width: 20, height: 600)]), .single)
        XCTAssertEqual(layout(0, 0), .single)
    }

    func testMultitaskingWindowSizesDoNotInheritFullScreenColumns() {
        // A fold at the edge belongs to the neighboring app, not a second pane.
        for width in [CGFloat(280), 320, 400, 480] {
            for height in [CGFloat(360), 600, 900] {
                XCTAssertEqual(layout(width, height, divisions: [
                    CGRect(x: width-8, y: 0, width: 16, height: height)
                ]), .single)
            }
        }
        XCTAssertEqual(layout(900, 280), .single) // Video + app stacked multitasking.
        XCTAssertEqual(layout(900, 600), .horizontal) // Return to full width.
    }

    func testKeyboardReducedWindowKeepsParameterRegionAccessible() {
        XCTAssertEqual(layout(860, 240), .single)
        XCTAssertEqual(layout(400, 240, regular: false), .single)
        XCTAssertEqual(layout(860, 600), .horizontal)
    }

    private func layout(_ width: CGFloat, _ height: CGFloat, regular: Bool = true,
                        largeText: Bool = false, divisions: [CGRect] = []) -> WorkspaceLayout {
        WorkspaceLayout.resolve(size: CGSize(width: width, height: height), regularWidth: regular,
            regularHeight: regular, accessibilityText: largeText, divisions: divisions)
    }
}

final class WorkspaceFileTransferTests: XCTestCase {
    @MainActor
    func testCancellingDelayedDropReleasesSharedQueueBeforeProviderCompletes() async throws {
        let queue = ConversionQueue()
        let originalIDs = queue.entries.map(\.id)
        let started = expectation(description:"Provider request started")
        let provider = NSItemProvider()
        let delayed = DelayedMovieRepresentation()
        provider.registerFileRepresentation(forTypeIdentifier:UTType.movie.identifier,
                                             fileOptions:[],visibility:.all) { completion in
            delayed.store(completion)
            started.fulfill()
            return delayed.progress
        }
        XCTAssertTrue(queue.importDroppedMovies([provider]))
        await fulfillment(of:[started],timeout:2)
        XCTAssertTrue(queue.importing)
        queue.cancel()
        let stopped = expectation(description:"Cancellation releases the shared queue")
        let observer = Task { @MainActor in
            while queue.importing && !Task.isCancelled { await Task.yield() }
            if !Task.isCancelled { stopped.fulfill() }
        }
        await fulfillment(of:[stopped],timeout:2)
        observer.cancel()
        XCTAssertFalse(queue.importing)
        XCTAssertEqual(queue.entries.map(\.id),originalIDs)
        // The provider deliberately answers after cancellation. It must not
        // resume the cancelled continuation again or admit another queue entry.
        delayed.complete(error:CancellationError())
    }

    func testCancellationBeforeProgressRegistrationCleansLateOwnedCopy() async throws {
        let request = MovieImportRequest()
        request.cancel()
        let progress = Progress(totalUnitCount:1)
        request.install(progress)
        XCTAssertTrue(progress.isCancelled)
        do {
            let _: URL = try await withCheckedThrowingContinuation { continuation in
                XCTAssertFalse(request.install(continuation))
            }
            XCTFail("Cancelled provider request must throw")
        } catch is CancellationError {} catch { XCTFail("Unexpected error: \(error)") }

        // Model cancellation winning while the provider's synchronous copy is
        // already in flight. Only the owned copy may be removed by its late result.
        let fixture = try XCTUnwrap(Bundle(for:Self.self).url(forResource:"aac-edits-timecode",withExtension:"mov"))
        let owned = try ImportStorage.copy(fixture)
        request.finish(.success(owned))
        XCTAssertFalse(FileManager.default.fileExists(atPath:owned.deletingLastPathComponent().path))
        XCTAssertTrue(FileManager.default.fileExists(atPath:fixture.path))
    }

    func testDroppedMovieOwnsItsBytesAfterProviderFileDisappears() async throws {
        let fixture = try XCTUnwrap(Bundle(for:Self.self).url(forResource:"aac-edits-timecode",withExtension:"mov"))
        let temporary = FileManager.default.temporaryDirectory.appendingPathComponent("Drop-\(UUID()).mov")
        try FileManager.default.copyItem(at:fixture,to:temporary)
        defer { try? FileManager.default.removeItem(at:temporary) }
        let provider = NSItemProvider()
        provider.registerFileRepresentation(forTypeIdentifier:UTType.movie.identifier,
                                             fileOptions:[],visibility:.all) { completion in
            completion(temporary,false,nil)
            return nil
        }
        let owned = try await ImportStorage.receiveMovie(provider)
        defer { try? FileManager.default.removeItem(at:owned.deletingLastPathComponent()) }
        try FileManager.default.removeItem(at:temporary)
        XCTAssertNotEqual(owned,temporary)
        XCTAssertEqual(try Data(contentsOf:owned),try Data(contentsOf:fixture))
    }

    @MainActor
    func testBusyQueueRejectsDropBeforeStartingProviderWork() {
        let queue = ConversionQueue()
        let provider = NSItemProvider()
        provider.registerFileRepresentation(forTypeIdentifier:UTType.movie.identifier,
                                             fileOptions:[],visibility:.all) { _ in
            XCTFail("A busy queue must not request or copy the provider's file")
            return nil
        }
        queue.running = true
        XCTAssertFalse(queue.importDroppedMovies([provider]))
        XCTAssertFalse(queue.importing)
        queue.running = false
        queue.importing = true
        XCTAssertFalse(queue.importDroppedMovies([provider]))
    }
}

private final class DelayedMovieRepresentation: @unchecked Sendable {
    let progress = Progress(totalUnitCount:1)
    private let lock = NSLock()
    private var completion: ((URL?, Bool, Error?) -> Void)?
    func store(_ completion: @escaping (URL?, Bool, Error?) -> Void) {
        lock.lock(); defer { lock.unlock() }
        self.completion = completion
    }
    func complete(error: Error) {
        lock.lock()
        let completion = self.completion
        self.completion = nil
        lock.unlock()
        completion?(nil,false,error)
    }
}
