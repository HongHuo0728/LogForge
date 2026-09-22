# Architecture

The C++20 core contains no GUI framework or linked multimedia library. Win32 is currently the only process, network and file-system backend. Pure color math and JSON parsing are independent of the window layer; future ports should replace the platform layer, not rewrite the color definition.

| Module | Responsibility |
| --- | --- |
| `color/AppleLog.cpp`, `color/HLG.cpp` | Double reference equations, float frame transformation |
| `Platform.cpp` | UTF-8/UTF-16, RAII handles, quoting, supervised child processes, logs, BCrypt SHA-256 |
| `FFmpeg.cpp` | Ordered discovery, runtime capability checks, provider abstraction, WinHTTP installer |
| `Media.cpp` | ffprobe parser, input acceptance, packet cadence checks, metadata policy, output validation, bounded MOV atom reader |
| `Transcode.cpp` | Job lifecycle, float decode/transform/encode bridge, progress, orientation, validation and publication |
| `MainWindow.cpp`, `SettingsWindow.cpp`, `Ui.cpp` | Native controls, settings, dark/light palettes, drop handling, DPI scaling and background-task events |
| `Settings.cpp` | Atomic per-user JSON preferences; preserve unrelated settings and migrate the legacy FFmpeg field |
| `Localization.cpp`, `Messages.inc` | English/Chinese messages with stable keys and checked argument placeholders |
| `Discovery.cpp` | Cancellable local-volume traversal, exclusions, real counts and injectable test roots |
| `Cli.cpp` | Scriptable access to the same production core |

## Ownership and threads

- One GUI task worker owns a detection, probe, download or transcode operation. Controls that could conflict with it are disabled.
- Worker progress/results are heap-owned event messages with ownership transferred through `PostMessage`; the UI consumes them with `unique_ptr`.
- Every child process starts suspended, is assigned to a kill-on-close Windows Job Object, then resumes. `STARTUPINFOEX` restricts handle inheritance to the three standard I/O handles; no other decoder/encoder pipes accidentally stay open.
- The transcode worker reads a whole float frame, transforms it, and writes it to the encoder. Separate reader threads drain decoder stderr, encoder stderr and encoder progress to prevent pipe deadlocks.
- A cancellation watcher terminates both supervised processes when requested; readers unblock, all threads join, and only the job's uniquely named partial output is removed. Closing the UI uses the same cancellation path.
- The parent-process handle/job lifetime prevents orphan FFmpeg processes even if the main application exits unexpectedly.

## Timing and publication

The raw float pipe does not carry timestamps. V1 therefore checks the complete video packet cadence before conversion and supports only a fixed-rate, monotonic stream. The rational input rate generates rawvideo timestamps. Original audio timestamps are offset by the input video start; output offsets/durations are checked. This is intentionally narrower than silently rebuilding VFR timing.

When the reported average and nominal rates agree within 0.1%, the nominal rate is the candidate cadence. Every packet duration, PTS interval and cumulative PTS phase must then agree within 1.05 source time-base ticks. This accommodates a one-tick camera clock correction while preventing drift from accumulating silently; the raw average remains in the probe report. Output uses the verified cadence, so a 24 fps recording remains 24 fps instead of acquiring an arbitrary mean-rate fraction. V1 can normalize source timing by up to this one-tick tolerance.

Audio uses stream copy and `-guess_layout_max 0` on the source input and any rotation remux. An absent channel-layout declaration stays absent; two unlabelled channels are not automatically declared stereo. The validator continues to compare codec, channel count, sample rate and layout strictly, with a field-specific diagnostic. Regression tests compare the complete audio payload hash for both unrotated and rotated unlabelled PCM input.

The selected destination is never overwritten. A unique partial MOV in the destination directory is written, probed and atom-checked. A same-directory rename publishes it only after validation. Cancellation or a failed validator removes the partial. A local JSON report records the evidence.

Creative controls are captured on the UI thread into immutable job options before worker dispatch. The Settings dialog and exposure selector are disabled during work. The worker applies the documented scene-linear grade only when enabled, before the separate Apple Log encoder, and records the settings in metadata and the validation report. All transformed components contribute to signal-range counters; warnings are returned to the GUI rather than discarded after a successful format check. These counters are explicitly scoped to the float signal before final quantization and compression.

`-progress` measurements come from the encoder, using real frames and timestamps. Indeterminate phases have a named stage and no invented percent. Estimated remaining time is shown only if FFmpeg has reported a usable speed after measurable work; fast/short jobs may have no ETA.

## Source and binary boundaries

FFmpeg remains a separate GPL executable. LogForge invokes its CLI and uses documented rawvideo/progress interfaces; no FFmpeg source is compiled or linked into the MIT executable. The only bundled source dependency is nlohmann/json 3.12.0 (MIT).

Release uses `/MT`, the OS's common controls, WinHTTP, BCrypt and WIC. CPack includes the GUI executable and required license/documentation files, excluding CLI/test executables, media fixtures, compiler artifacts, caches and FFmpeg.

## Preferences and localization

The first-run defaults are English, dark theme and disabled creative adjustment. `settings.json` stores `language`, `theme`, the `creative` object, `ffmpeg` (manual) and `detected_ffmpeg` (automatic). Writes merge owned fields, use a same-directory temporary file and `MoveFileExW` replacement, and are protected by an in-process mutex. Missing or malformed preferences fall back to defaults; recovery is reported in Details. Settings Save applies language/theme immediately; Cancel never writes the draft. Simultaneous independent app instances are not a synchronized settings editor: the last complete write wins.

User-facing errors and progress carry a stable `TextId` plus arguments across worker events. GUI translation happens on the UI thread. Logs and validation descriptions remain English, with stable diagnostic codes in JSON. Raw FFmpeg/OS diagnostics are retained rather than translated speculatively. `ValidateTranslations` checks complete entries, unique keys and matching placeholder sets.

The UI uses documented DWM caption attributes where available and owner-drawn native controls for both themes; it needs no framework or private Windows dark-mode entry points. DPI-aware layout and scrollable content preserve access at 100%, 150% and 200% sizing. System high-contrast colors take precedence when detected at appearance application. Settings do not change Windows' global theme or language.

## FFmpeg discovery

Quick candidates are checked before all accessible local-volume roots. Every accepted candidate must have a sibling ffprobe and pass the production version/capability/synthetic codec tests. Invalid candidates are logged and traversal continues. The scan uses directory enumeration with cancellation at each entry, skips reparse subdirectories to avoid loops, and runs at background thread/I/O priority. Progress reports the drive and actual folder/candidate/skip counts; there is no invented percentage.

A verified path is cached but is checked again on every launch. Download/manual buttons are visible only after completed discovery finds no usable pair, and hidden after success. Cancelled discovery leaves a rescan action. The installer uses the same capability checks before reporting success. Traversal tests inject small explicit directory roots; an additional test uses the real FFmpeg binaries after invalid fixtures and verifies cached rediscovery.

## Version and resources

CMake defines version 1.0.0 and build 26922A. Generated headers feed both the C++ display/logs/metadata and the Windows VERSIONINFO resource. The manifest uses the four-part assembly version. Both executables embed the same nine-size icon. Portable packaging uses an explicit document/image allowlist; an independent ZIP audit rejects unexpected files and compares the packaged executable to the tested Release binary.
