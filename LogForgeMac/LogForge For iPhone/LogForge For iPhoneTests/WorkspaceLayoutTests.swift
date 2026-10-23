import XCTest
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

    private func layout(_ width: CGFloat, _ height: CGFloat, regular: Bool = true,
                        largeText: Bool = false, divisions: [CGRect] = []) -> WorkspaceLayout {
        WorkspaceLayout.resolve(size: CGSize(width: width, height: height), regularWidth: regular,
            regularHeight: regular, accessibilityText: largeText, divisions: divisions)
    }
}
