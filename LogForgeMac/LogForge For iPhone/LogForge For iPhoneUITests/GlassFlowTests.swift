import XCTest

final class GlassFlowTests: XCTestCase {
    private func launch(queue: Bool = false) -> XCUIApplication {
        continueAfterFailure = false
        let app = XCUIApplication()
        app.launchArguments = ["-AppleLanguages", "(en)", "-AppleLocale", "en_US", "--ui-testing"]
        if queue { app.launchArguments.append("--ui-queue") }
        app.launch()
        return app
    }

    private func tap(_ identifier: String, in app: XCUIApplication) {
        let button = app.buttons[identifier]
        if !button.isHittable {
            let overflow = app.buttons.matching(NSPredicate(format:
                "label == 'More' OR label == 'More Actions' OR identifier == 'ToolbarOverflowMenu'")).firstMatch
            if overflow.exists { overflow.tap() }
        }
        XCTAssertTrue(button.waitForExistence(timeout: 10), identifier)
        XCTAssertTrue(button.isHittable, identifier)
        button.tap()
    }

    func testLicenseNavigationKeepsSettingsAndVersion() throws {
        let app = launch()
        tap("conversionSettings", in: app)
        XCTAssertTrue(app.staticTexts["settingsVersion"].waitForExistence(timeout: 5))
        let bundle = Bundle(for: GlassFlowTests.self)
        let version = try XCTUnwrap(bundle.object(forInfoDictionaryKey: "CFBundleShortVersionString") as? String)
        let build = try XCTUnwrap(bundle.object(forInfoDictionaryKey: "LogForgeReleaseBuild") as? String)
        XCTAssertEqual(version, "1.3.2")
        XCTAssertEqual(build, "261023A")
        XCTAssertEqual(app.staticTexts["settingsVersion"].label, "\(version) (\(build))")
        for _ in 0..<3 {
            for _ in 0..<8 where !app.buttons["openSourceLicenses"].isHittable {
                app.scrollViews["settingsContent"].swipeUp()
            }
            tap("openSourceLicenses", in: app)
            XCTAssertTrue(app.scrollViews["licenseDocument"].waitForExistence(timeout: 5))
            XCTAssertTrue(app.staticTexts.containing(NSPredicate(format:
                "label CONTAINS %@", "GNU LESSER GENERAL PUBLIC LICENSE")).firstMatch.exists)
            app.scrollViews["licenseDocument"].swipeUp()
            let back = app.buttons["Settings"]
            if back.isHittable { back.tap() }
            else { app.navigationBars.buttons.firstMatch.tap() }
            XCTAssertTrue(app.scrollViews["settingsContent"].waitForExistence(timeout: 5))
        }
    }

    func testImportDialogAndSettingsCancel() {
        let app = launch()
        tap("importVideo", in: app)
        XCTAssertTrue(app.buttons["Choose Photos"].waitForExistence(timeout: 5))
        XCTAssertTrue(app.buttons["Choose Files"].exists)
        XCTAssertTrue(app.buttons["Choose Folder"].exists)
        tap("cancelImport", in: app)
        tap("conversionSettings", in: app)
        XCTAssertTrue(app.staticTexts["settingsVersion"].waitForExistence(timeout: 5))
        tap("cancelSettings", in: app)
        XCTAssertTrue(app.buttons["conversionAction"].waitForExistence(timeout: 5))
        XCTAssertFalse(app.buttons["conversionAction"].isEnabled)
    }

    func testSettingsDraftSurvivesRotationAndCanBeSaved() {
        let app = launch()
        tap("conversionSettings", in: app)
        tap("option-exposure", in: app)
        app.buttons["1 EV"].tap()
        XCUIDevice.shared.orientation = .landscapeLeft
        defer { XCUIDevice.shared.orientation = .portrait }
        XCTAssertTrue(app.buttons["saveSettings"].waitForExistence(timeout: 5))
        XCTAssertTrue(app.buttons["option-exposure"].label.contains("1 EV"))
        tap("saveSettings", in: app)
        tap("conversionSettings", in: app)
        XCTAssertTrue(app.buttons["option-exposure"].label.contains("1 EV"))
        tap("option-exposure", in: app)
        app.buttons["0 EV"].tap()
        tap("saveSettings", in: app)
    }

    func testQueuedItemsSurviveResizing() {
        let app = launch(queue: true)
        let first = app.staticTexts.containing(NSPredicate(format:
            "label BEGINSWITH %@", "1-Long-camera-recording-")).firstMatch
        XCTAssertTrue(first.waitForExistence(timeout: 10))
        let original = first.label
        XCUIDevice.shared.orientation = .landscapeLeft
        XCTAssertTrue(first.waitForExistence(timeout: 5))
        XCTAssertEqual(first.label, original)
        XCTAssertTrue(app.buttons["conversionAction"].isEnabled)
        XCUIDevice.shared.orientation = .portrait
        XCTAssertEqual(first.label, original)
        let shot = XCTAttachment(screenshot: app.screenshot())
        shot.name = "Queue after rotation"; shot.lifetime = .keepAlways
        add(shot)
    }
}
