import XCTest

final class GlassFlowTests: XCTestCase {
    func testLicenseNavigationKeepsSettingsAndVersion() throws {
        continueAfterFailure = false
        let app = XCUIApplication()
        app.launchArguments += ["-AppleLanguages","(en)","-AppleLocale","en_US"]
        app.launch()
        XCTAssertTrue(app.buttons["conversionSettings"].waitForExistence(timeout:10))
        app.buttons["conversionSettings"].tap()
        XCTAssertTrue(app.staticTexts["settingsVersion"].waitForExistence(timeout:5))
        let testBundle = Bundle(for:GlassFlowTests.self)
        let version = try XCTUnwrap(testBundle.object(forInfoDictionaryKey:"CFBundleShortVersionString") as? String)
        let releaseBuild = try XCTUnwrap(testBundle.object(forInfoDictionaryKey:"LogForgeReleaseBuild") as? String)
        XCTAssertEqual(releaseBuild,"26107B")
        XCTAssertEqual(app.staticTexts["settingsVersion"].label,"\(version) (\(releaseBuild))")
        XCTAssertFalse(app.staticTexts["bundleBuild"].exists)
        for _ in 0..<3 {
            for _ in 0..<3 where !app.buttons["openSourceLicenses"].isHittable { app.swipeUp() }
            app.buttons["openSourceLicenses"].tap()
            XCTAssertTrue(app.scrollViews["licenseDocument"].waitForExistence(timeout:5))
            XCTAssertTrue(app.staticTexts.containing(NSPredicate(format:"label CONTAINS %@","GNU LESSER GENERAL PUBLIC LICENSE")).firstMatch.exists)
            app.swipeUp()
            app.navigationBars.buttons.firstMatch.tap()
            XCTAssertTrue(app.buttons["openSourceLicenses"].waitForExistence(timeout:5))
        }
    }
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
