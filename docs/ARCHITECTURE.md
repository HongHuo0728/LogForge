# Architecture

The C++20 core contains no GUI framework or linked multimedia library. Win32 is currently the only process, network and file-system backend. Pure color math and JSON parsing are independent of the window layer; future ports should replace the platform layer, not rewrite the color definition.

| Module | Responsibility |
| --- | --- |
| `color/AppleLog.cpp`, `color/HLG.cpp` | Double reference equations, float frame transformation |
| `Platform.cpp` | UTF-8/UTF-16, RAII handles, quoting, supervised child processes, logs, BCrypt SHA-256 |
| `FFmpeg.cpp` | Ordered discovery, runtime capability checks, provider abstraction, WinHTTP installer |
| `Media.cpp`, `Cadence.cpp` | ffprobe parsing, full-packet timing verification and output validation |
| `MetadataPolicy.cpp`, `MovAnalyzer.cpp` | Whitelist copy plan, bounded typed metadata/sample-entry reader and semantic diff |
| `AppleLogIdentification.cpp` | Independently verified sample-entry `logs` writer; metadata only, 1 MiB relocation buffer, strict atom validation |
| `ToolTrust.cpp`, `FFmpegNumeric.cpp` | Explicit path/hash execution approval, locked images and numeric qualification |
| `color/FloatTransformer.cpp` | Persistent CPU workers calling scalar equations on bounded chunks/tiles |
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
- The standard transcode worker reads bounded float chunks (at most 4 MiB), transforms them with persistent CPU workers, and writes them to the encoder. Creative mode needs corresponding RGB planes and retains one frame, split into 16,384-pixel worker tiles. Scalar math is unchanged and exact float equality is tested. Separate reader threads drain decoder stderr, encoder stderr and encoder progress to prevent pipe deadlocks.
- A cancellation watcher terminates both supervised processes when requested; readers unblock, all threads join, and only the job's uniquely named partial output is removed. Closing the UI uses the same cancellation path.
- The parent-process handle/job lifetime prevents orphan FFmpeg processes even if the main application exits unexpectedly.

## Timing and publication

The raw float pipe does not carry timestamps. V1 therefore checks the complete video packet cadence before conversion and supports only a fixed-rate, monotonic stream. The rational input rate generates rawvideo timestamps. Original audio timestamps are offset by the input video start; output offsets/durations are checked. This is intentionally narrower than silently rebuilding VFR timing.

Average and nominal rates are candidates, never a VFR verdict. If all packet durations and adjacent intervals are identical, their exact time-base ratio is authoritative, including 29.99/29.98/29.970x rates. For nonuniform quantized clocks, a nominal candidate inside the observed duration range is tested; otherwise the dominant packet duration is the conservative anchor. The checker does not regress the mean/endpoints to erase sustained corrections. Every duration, interval and cumulative phase must stay within the existing 1.05-tick budget; interval/previous-duration inconsistencies, duplicate/reverse PTS and sustained phase drift fail with a zero-based packet index. Missing timing is rejected. This conservative policy can refuse ambiguous nonuniform clocks rather than manufacture a new rate. Output is checked again over every packet and uses the verified input cadence.

Audio uses stream copy and `-guess_layout_max 0` on the source input and any rotation remux. An absent channel-layout declaration stays absent; two unlabelled channels are not automatically declared stereo. The validator continues to compare codec, channel count, sample rate and layout strictly, with a field-specific diagnostic. Regression tests compare the complete audio payload hash for both unrotated and rotated unlabelled PCM input.

The selected destination is never overwritten. A unique partial MOV in the destination directory is written, probed and atom-checked. A same-directory `MoveFileExW` without REPLACE_EXISTING publishes it only after validation, also protecting a destination created during conversion. Cancellation or a failed validator removes the partial. A local JSON report records the evidence.

After encoding and any rotation remux, the separate identification writer adds
the verified `logs` sample-entry atom. It requires one HQ entry and trailing
moov, preserves mdat/packet offsets, and holds a deny-write/delete handle while
moving the moov tail in 1 MiB chunks. Conflicting or unexpected layouts fail
before publication. Final validation requires the exact identifier and nclc
9/2/9; no color equation or camera identity is changed. See the
[actual Resolve A/B evidence](APPLE_LOG_IDENTIFICATION.md).

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

`DiscoveryHelper.cpp` implements a hidden entry point in both application binaries;
no additional helper EXE is distributed. The parent supervises it with the same
Job Object/process abstraction as FFmpeg. File/index queries run outside the UI
process, so a blocked provider cannot defeat cancellation or the Quick deadline.

Quick discovery shares a 3,000 ms budget across the preferred and fallback passes.
Managed and saved paths are discovered first and fully verified. A verified pair
stops further discovery. Hashes/capability/reference-signal validation are timed
separately and do not consume the filesystem budget. Otherwise the remaining
budget covers app-adjacent tools, PATH, registry App Paths, WinGet/Scoop/Chocolatey
actual installations, common folders (depth 3, up to 4,096 entries per root), then
an optional exact-filename query against the existing Windows Search index.
Known folders come from Windows, independently of the app data override. Index
absence/failure is not proof that FFmpeg is absent. No package-manager or shim
commands are executed, no service/index settings are modified.

Only explicit Deep mode enumerates accessible fixed/removable drives. Reparse
subdirectories, offline/protected locations and network shares are skipped.
Results stream before completion; a 256-candidate safety limit is reported rather
than silently claiming complete coverage. The report includes completion reason,
source, canonical pair, trust/capability issue, folder counts and timings.

Unknown candidates are never executed. Selection stops a live Deep search before
approval/validation. Both hashes are still checked while deny-write/delete handles
are held. GUI failure/cancellation always restores download, manual and retry
controls. A successful verification hides them. All background jobs remain serial.

Pipe readers propagate exceptions. The process watcher remains active until both
readers finish, even if the primary process already exited. Stdout callback lines
are bounded to 1 MiB; stderr/progress lines to 64 KiB. Jobs terminate before reader
joins on cancellation/failure, including partial reader-thread construction.

## Version and resources

CMake defines version 1.1.1 and build 26923D. Generated headers feed both the C++ display/logs/metadata and the Windows VERSIONINFO resource. The manifest uses the four-part assembly version. Both executables embed the same nine-size icon. Portable packaging uses an explicit document/image allowlist; an independent ZIP audit rejects unexpected files and compares the packaged executable to the tested Release binary.
