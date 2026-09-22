# Architecture

The C++20 core contains no GUI framework or linked multimedia library. Win32 is currently the only process, network and file-system backend. Pure color math and JSON parsing are independent of the window layer; future ports should replace the platform layer, not rewrite the color definition.

| Module | Responsibility |
| --- | --- |
| `color/AppleLog.cpp`, `color/HLG.cpp` | Double reference equations, float frame transformation |
| `Platform.cpp` | UTF-8/UTF-16, RAII handles, quoting, supervised child processes, logs, BCrypt SHA-256 |
| `FFmpeg.cpp` | Ordered discovery, runtime capability checks, provider abstraction, WinHTTP installer |
| `Media.cpp` | ffprobe parser, input acceptance, packet cadence checks, metadata policy, output validation, bounded MOV atom reader |
| `Transcode.cpp` | Job lifecycle, float decode/transform/encode bridge, progress, orientation, validation and publication |
| `MainWindow.cpp` | Native controls, dialogs, drop handling, DPI scaling and background-task events |
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

The selected destination is never overwritten. A unique partial MOV in the destination directory is written, probed and atom-checked. A same-directory rename publishes it only after validation. Cancellation or a failed validator removes the partial. A local JSON report records the evidence.

`-progress` measurements come from the encoder, using real frames and timestamps. Indeterminate phases have a named stage and no invented percent. Estimated remaining time is shown only if FFmpeg has reported a usable speed after measurable work; fast/short jobs may have no ETA.

## Source and binary boundaries

FFmpeg remains a separate GPL executable. LogForge invokes its CLI and uses documented rawvideo/progress interfaces; no FFmpeg source is compiled or linked into the MIT executable. The only bundled source dependency is nlohmann/json 3.12.0 (MIT).

Release uses `/MT`, the OS's common controls, WinHTTP, BCrypt and WIC. CPack includes the GUI executable and required license/documentation files, excluding CLI/test executables, media fixtures, compiler artifacts, caches and FFmpeg.
