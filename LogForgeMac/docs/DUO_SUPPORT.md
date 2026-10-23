# iPhone Duo API coverage for LogForge 1.3.2

This inventory covers the public Duo features relevant to a native video-file
conversion workspace. Source integration and successful execution are separate:
the current commit's Actions results, screenshots and `verification.json` establish
what was actually verified. A source change alone does not mark a row as passed.

| Capability | Implementation and purpose | Evidence to inspect |
|---|---|---|
| Full inner/outer display | Build with iOS 27.1 SDK; background fills the window; content uses its current GeometryReader size, size classes and safe areas. No fixed device aspect ratio. | Native window/display bounds and screenshots, landscape and portrait. |
| Fold-aware two-region workspace | `ArrangementView` with `.split`; active `.division` reserved regions determine whether the fold divides the local workspace. Independent scroll containers. iOS 26 uses adaptive stacks. | Unfolded, book, tabletop and outer-display posture tests; separate geometry unit cases. |
| Camera and system occlusion | NavigationStack, ScrollView, native toolbars, menus and sheets retain system handling of occlusion regions. Only the decorative background ignores safe areas. | Screen inspection, menus/import/settings/license regressions. |
| Native vertical bars and overflow | `.topBarPinnedTrailing`, visibility priorities, standard primary/cancel/confirmation placements, and `.axisBehavior(.verticalPreferred)` for the custom native window menu. System chooses the correct edge, including the left app in Split View. | Real toolbar interaction and screenshots in each form, narrow windows and large text. |
| Split View and stacked multitasking | `UIRequiresFullScreen=false`, all orientations, layout based on the local window. Narrow panes contain both queue and parameters in a complete single column. | Actual Split View with Safari, move between sides, draft/queue retention and full-screen restoration. The stacked video/app layout uses the same resizability path. |
| Multiple LogForge windows | `UIApplicationSupportsMultipleScenes`, WindowGroup, app-owned shared ConversionQueue, independent root sheet/draft state. `UIWindowScene.ActivationAction` and system window availability handle opening additional scenes. | Two native scenes must report distinct scene identities and the same queue identity; fold to the outer screen without replacing the queue. |
| Hinge changes and scene lifecycle | `onHingeChange` on iOS 27.1 checkpoints on hinge status transitions; scene inactivity checkpoints too. Layout never branches on hinge angles. | Real task identity/progress while folding; continue and complete an actual ProRes conversion. |
| System split keyboard | Files search remains native; no custom input view or keyboard extension. Native presentation respects keyboard-safe areas. Split/Full selection belongs to the user/system keyboard. | Activate Split through the real keyboard menu; inspect separated key frames, enter text, fold/rotate and cancel without losing queue state. |
| Cross-app file import | Native Files/Photos pickers remain available in resized panes. Movie/file-URL drops use NSItemProvider and copy inside its temporary-file callback before applying the existing input contract; busy queues reject new drops. | Actual Photos-to-LogForge drop in Split View; native provider-lifetime/busy-queue tests. Unsupported media does not expand the media contract. |
| Sharing results | System ShareLink for converted MOV files and reports adapts to the active scene; completed movies also export a draggable FileRepresentation. | Complete actual conversion, open the system share sheet and inspect it; inspect the exported file representation. |
| Dynamic Type, appearance and transparency | Reflowing label/control rows, standard environment values, existing Liquid Glass and five languages. | Largest accessibility text, light/dark and Reduce Transparency screenshots; all five languages. |

## Scope decisions

The queue, current task, destination and task journal are shared between windows.
Saved conversion defaults synchronize between windows, while an open settings
sheet keeps its own draft until Save or Cancel. No fold event starts or restarts
processing. Ordinary background execution still follows the existing iOS task
lifecycle; merely supporting multiple windows does not grant unlimited background time.

`CameraCaptureAccessory` requires an active camera capture session and presents
camera companion content on the outer display. LogForge converts imported files
and does not capture camera video, so registering that accessory would not add a
working conversion feature. Camera controls, custom keyboard-extension APIs and
hinge-driven musical/game effects are likewise outside the current app's function.
External-display scene accessories are not required to show the same app while
switching between Duo's inner and outer screens; that transition is system managed.

Native safe areas and containers already consume occlusion information. Duplicating
their insets with manually guessed camera rectangles would reduce useful space.
The explicit reserved-region query is for custom two-region fold layout.

## Evidence and release gate

`tools/test_duo.py` runs Appium 3.8.0 / XCUITest Driver 12.19.0 against the actual
iOS 27.1 Duo simulator. Each scenario records actual hinge angles, selected display,
accessibility trees and PNG screenshots. Screen selection only controls capture
and input; it cannot prove that the app moved screens. Tent mode passes only when
the app's real window occupies the outer display in that posture.

Geometric unit cases use synthetic dimensions and are labeled separately. They do
not stand in for a simulator Split View, keyboard split, hinge pose or second scene.
Independent UI scenarios reinstall the test app between tests; continuity assertions occur inside
each scenario without restarting the app. Diagnostics are retained even on failure.
The focused release gate requires a successful device archive/package audit, native
import/layout/UI checks and real MOV conversion on Duo, plus
unfolded landscape/portrait, book, tabletop and closed outer portrait/landscape.
Tent, Split View, split keyboard and multiple windows are each attempted once as
additional diagnostics. Their actual outcomes are retained in `verification.json`
and release notes; an unavailable simulator posture is never recorded as a pass.
Use `LOGFORGE_DUO_SUITE=full` for the extended appearance, cross-app drop and running
conversion transitions, and `LOGFORGE_NATIVE_SUITE=full` for the full native test suite.
These extended suites are not repeatedly run as part of the focused release.
The manual workflow's `extended_checks` option also enables iOS 26/iPad runtime
regressions; the default release run stays focused on Duo. Minimum deployment
remains iOS 26, with SDK availability checks and adaptive fallback source retained.
A candidate archive is uploaded before simulator checks and is labeled as a candidate.
The version tag promotes artifacts from a successful main push of the exact same
commit. The publisher verifies the run's repository, branch, workflow, status,
commit and package hashes, then reads back all six uploaded assets. It does not
rebuild or repeat simulator checks solely because the tag was pushed.

## Primary references

- [Apple: Prepare for iPhone Duo](https://developer.apple.com/iphone-duo/prepare/)
- [Apple: Designing for iPhone Duo](https://developer.apple.com/design/human-interface-guidelines/designing-for-iphone-duo)
- [Apple: Adaptive layouts and reserved regions](https://developer.apple.com/videos/play/tech-talks/111463/)
- [Apple: Native toolbar behavior](https://developer.apple.com/videos/play/tech-talks/111462/)
- [Apple: Hinge, scenes, multitasking and camera accessories](https://developer.apple.com/videos/play/tech-talks/111464/)
- [Apple Support: Duo Split View](https://support.apple.com/guide/iphone/open-two-apps-in-split-view-on-iphone-duo-iphrj13y9uxz/ios)
- [Apple Support: System split keyboard](https://support.apple.com/guide/iphone/type-with-the-onscreen-keyboard-iph3c50f96e/ios)
- [Appium: Real Duo simulator control](https://appium.github.io/appium-xcuitest-driver/12.19/guides/duo/)
