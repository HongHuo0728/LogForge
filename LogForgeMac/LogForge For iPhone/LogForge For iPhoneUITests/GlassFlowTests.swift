import XCTest

final class GlassFlowTests: XCTestCase {
    func testImportDialogAndSettingsCancel() {
        continueAfterFailure = false
        let app = XCUIApplication()
        app.launchArguments += ["-AppleLanguages","(en)","-AppleLocale","en_US"]
        app.launch()
        XCTAssertTrue(app.buttons["importVideo"].waitForExistence(timeout:10))
        app.buttons["importVideo"].tap()
        XCTAssertTrue(app.buttons["Choose Photos"].waitForExistence(timeout:5))
        XCTAssertTrue(app.buttons["Choose Files"].exists)
        XCTAssertTrue(app.buttons["Choose Folder"].exists)
        app.buttons["Cancel"].tap()
        app.buttons["conversionSettings"].tap()
        XCTAssertTrue(app.staticTexts["Creative adjustments"].waitForExistence(timeout:5))
        app.swipeUp()
        XCTAssertTrue(app.buttons["Save"].exists)
        app.buttons["Cancel"].tap()
        XCTAssertTrue(app.buttons["languageSettings"].exists)
    }
}
