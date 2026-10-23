# LogForge v1.3.2

**Build 261023A — iOS-only update, with Windows 1.3.0 included**

This update targets iOS. The included Windows version remains **1.3.0 (26929A)**.
Its ZIP and checksum are reused unchanged from the published **v1.3.1 Release**,
with their original filenames, versions and bytes. Historical releases are retained.

## Adaptive iPhone Duo workspace

The queue, progress and results share the primary region; conversion parameters
occupy the secondary region. Unfolded landscape uses two independently scrolling
columns, unfolded portrait stacks the regions, and compact windows use one complete
scrolling workspace. On iOS 27.1, native `ArrangementView` and active reserved
division regions keep the two work areas on separate sides of a book or tabletop
fold. The outer display uses the compact workspace in either orientation,
including when the device stands as a tent.

The background fills the window. Foreground controls respect system safe areas;
layout follows current window geometry and size classes instead of device names
or a fixed screen ratio. iOS 26 keeps the single/dual-region fallback. Queue
ownership, conversion tasks, settings drafts and presentations remain at the root
when the content arrangement changes.

Native Split View uses each window's available space, including left-side system
controls in the left app. Multiple LogForge windows share one conversion queue
and running task; each window keeps its own settings draft. The native new-window
action respects the system's changing availability on the inner and outer displays.
Hinge state transitions checkpoint the queue without using angle thresholds to
choose layouts. Movie and file-URL drops enter the existing validated import
pipeline; completed movies can be dragged out using a native file representation.

Text entry in the Files picker uses the system keyboard, including Duo's split
keyboard. Native sheets and keyboard avoidance keep search and cancellation
available when folding. The app does not replace the keyboard or change its layout
setting. See the [Duo API coverage and validation matrix](https://github.com/HongHuo0728/LogForge/blob/v1.3.2/LogForgeMac/docs/DUO_SUPPORT.md).

## Native toolbars and readable panels

Import, convert/cancel, settings and language actions use native system toolbars.
The system places them vertically at the side when appropriate and adapts their
placement for other windows and sheets. Primary actions get higher visibility
priority; secondary actions can enter the system overflow menu. Settings save and
cancel use the same native placement rules.

Import sheets no longer have a fixed height. Parameter rows reflow for long text
and accessibility sizes. Settings and license navigation retain Liquid Glass,
haptics and all five interface languages. The existing media input contract,
conversion pipeline and report structure are unchanged.

## Verification and compatibility

The release pipeline pins Xcode **27.1 (27A9275)** and the iOS **27.1 SDK**, while
retaining an iOS **26.0** deployment target. Its focused release checks cover real
MOV conversion, import ownership/cancellation, layout, toolbar/import/settings/license
operations and six actual inner/outer Duo simulator layouts. iOS 26/iPad runtime
regressions remain available as optional compatibility checks.
The device archive is built and checked first. Only one simulator is booted at a
time, and checks have bounded timeouts.
After the main build succeeds, the version tag promotes its exact artifacts;
publication rechecks source provenance and hashes without rebuilding or repeating
the simulator suite.
Appium **3.8.0** with XCUITest Driver **12.19.0** and WebDriverAgent **16.17.3**
records screenshots, accessibility
trees, hinge angle, display identity and actual window geometry. Injected geometry
unit cases are separate evidence and never substitute for simulator posture tests.
The pinned WDA release includes the upstream Duo scene-rotation fix. A separately
pinned simulator-only helper attempts FaceDown for the optional tent diagnostic;
neither automation component is included in the application packages.

The published release adds the exact run link, commit, test counts and checksums
below. A source checkout or workflow configuration alone does not establish that
all tests passed. Failed core layout or build checks block formal publication.
Tent, Split View, split keyboard and multiple-window diagnostics are attempted once
and reported individually; an unverified diagnostic is not presented as a pass.
The longer appearance/file-drop/conversion-transition suite remains available with
`LOGFORGE_DUO_SUITE=full`, but is not part of this focused release run. A manual
workflow run can enable `extended_checks` to run the full suites and iOS 26/iPad checks.
Diagnostic artifacts are retained for 14 days. Simulator verification does not
establish physical-device performance, thermal behavior, haptics or editor recognition.

An additional AAC edit/timecode pipeline test passed in [run 18](https://github.com/HongHuo0728/LogForge/actions/runs/40006976918)
and exceeded XCTest's execution-time allowance in [run 19](https://github.com/HongHuo0728/LogForge/actions/runs/40011526515)
with unchanged application sources. That diagnostic is retained and the test
remains in the optional full suite; it is not reported as a pass in the final
focused run. The 14 focused checks retain real 4K ProRes MOV/audio validation.

## Download and installation

- `LogForge-1.3.2-iOS-261023A-unsigned.ipa` and its `.sha256`.
- `LogForge-1.3.2-iOS-261023A-RelinkKit.zip` and its `.sha256`.
- `LogForge-1.3.0-Windows-x64.zip` and its `.sha256` (from v1.3.1).

The IPA is an unsigned arm64 device build. Install it after signing with your own
valid Apple identity and provisioning profile. No App Store or TestFlight release
is configured. Keep the matching RelinkKit, codec sources and licenses with any
redistributed signed app. Signing changes the IPA checksum. Windows installation
and its application version are unchanged.

The app displays **1.3.2 (261023A)**. Reports continue to retain each attempt's
release identity and numeric Apple bundle build; old attempts retain their original
identity. Packaging accepts historical five-digit and new six-digit release build
codes, while `CFBundleVersion` remains numeric.

Implementation references: [Apple adaptive layouts](https://developer.apple.com/videos/play/tech-talks/111463/),
[Apple native toolbars](https://developer.apple.com/videos/play/tech-talks/111462/),
[GitHub Xcode 27 runner image](https://github.com/actions/runner-images/blob/main/images/macos/xcode-27-arm64-Readme.md).
