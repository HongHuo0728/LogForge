# Validation record — 0.1.0 preview

Local verification date: 2026-09-22. Results below are observations from this development machine, not claims of device or editor certification.

## Build and host

- Windows x64, OS build 26200.
- Visual Studio 2022 / MSVC 19.44.35228, Windows SDK 10.0.26100.0, CMake 4.3.3.
- CMake configure and Release build passed. Release uses `/MT`.
- `dumpbin /dependents` found Windows system DLLs only; no `VCRUNTIME*.dll`, `MSVCP*.dll`, Qt or FFmpeg DLL dependency.
- Windows 10 22H2 is the declared minimum target but has not been boot-tested on this machine.

## Automated tests that passed

| CTest group | Evidence |
| --- | --- |
| `color_math` | Apple Log reference points, knee/floor, 100,001 Apple Log round trips and 100,001 HLG round trips, normalization, finite float handling |
| `media_metadata_validation` | ffprobe parser, malformed/missing values, wrong codec/profile/depth/gamut/transfer/range, output frame/rate/audio/color validation |
| `ffmpeg_platform` | Missing binary rejection, capability parser, Unicode argument quoting, SHA-256 standard vector, MOV `colr` parsing, pinned provider, supervised process cancellation |
| `media_pipeline` | Generated high-precision media converted through actual FFmpeg and production C++ code; real output metadata and decoded pixels checked |

Integration media: 320 × 180, 60 frames, 30000/1001 fps, 10-bit HLG BT.2020 ProRes HQ, 48 kHz stereo PCM 24-bit audio, timecode `10:20:30:00`, synthetic original Make/Model and creation date.

Content includes gray, shadow and highlight ramps, above-white levels, color ramps, BT.2020 primary/secondary patches and neutrals. The source was generated from planar 32-bit float samples, not an 8-bit picture.

Measured on 122,304 RGB samples away from discontinuity borders:

| Metric | Result | Acceptance |
| --- | --- | --- |
| Mean absolute normalized RGB error against independent equations | 0.0003907838039660831 | < 0.003 |
| Maximum absolute error in measured regions | 0.01238918955373558 | < 0.025 |
| Output frame count | 60 | exactly 60 |
| Audio compressed/PCM packet payload hash | unchanged | exact SHA-256 match |
| Output sample entry | `apch` / ProRes HQ | exact |
| Output pixel format | `yuv422p10le` | exact |
| MOV `nclc` | 9 / 2 / 9 | exact |

Error includes codec compression, 4:2:2 resampling and final quantization; it is not the double-precision function error. The test also asserts a substantial input/output pixel change to rule out a metadata-only result.

Additional generated-media cases passed:

- ProRes 422 Standard input.
- +90° and -90° rotation preservation.
- Silent footage.
- AAC audio copy and timing validation.
- **3840 × 2160, three ProRes frames** through the actual float pipeline.
- Rec.709 and deliberately VFR inputs refused.
- Existing output not overwritten, verified by unchanged file hash.
- Cancellation during an encoder progress event returned code 130, published no output and left no partial MOV.

## Installer and GUI observations

- The production installer downloaded **109,728,040 bytes** using WinHTTP, verified the pinned SHA-256, extracted the archive and re-ran the ProRes/float capability test successfully.
- Installed under `%LOCALAPPDATA%\LogForge\tools\ffmpeg\ffmpeg-8.1.2-essentials_build`; no PATH or Program Files changes.
- A real native GUI window was created. An automated `WM_DROPFILES` payload with a Unicode filename exercised the actual drop handler; asynchronous probe, conversion, validation and normal close passed. The window captured its own rendered controls to PNG for visual inspection.
- GUI screenshot is stored in `docs/images/LogForge.png`.
- Manual IFileOpenDialog/IFileSaveDialog code is compiled and present; a human clicking through those native dialogs has not been recorded as a separate test.
- No FFmpeg/ffprobe child processes remained after the completed local test runs.
- The ZIP was extracted to a separate portable directory. That exact packaged executable passed the same native drag/drop-to-output test against the per-user installed FFmpeg. Final controls were verified enabled for a new conversion and disabled for cancellation.
- `ReferenceMovAnalyzer` compared the generated HLG and Apple Log files and emitted a nonempty structural diff, including source `colr` 9/18/9 and output 9/2/9. This is a tool test, not a real iPhone reference comparison.

## Not yet verified

- Genuine iPhone 13/14 Pro HLG recordings and a genuine iPhone 15 Pro Apple Log reference sample.
- Apple private Log transfer serialization, camera-specific MOV atoms and automatic Apple Log detection.
- Apple official or third-party LUT behavior inside Resolve / Premiere / Final Cut. Pixel math and video-level normalization have been checked independently; commercial-editor interoperability is still a separate validation task.
- Long-form 4K/8K workloads, unusual multichannel audio/edit lists, arbitrary non-rotation display transforms, and every user's proxy/TLS configuration.
- Network-failure retries, disk-full errors and corrupted-download rejection have implementation paths; only success-path downloading, SHA-256 reference calculation and ordinary task cancellation have been exercised end to end here.
- GitHub-hosted Actions execution. The workflow is included and the local equivalent commands pass; it has not yet run on GitHub because this repository has not been pushed.

## Reproduce

Use the README configure/build/CTest commands with `LOGFORGE_TEST_FFMPEG`. Reports are generated in `build/integration/integration-report.json`, `build/integration/appdata/logs/*.validation.json`, and `build/Testing/Temporary/LastTest.log`.

The GUI supports a developer smoke path:

```powershell
$env:LOGFORGE_DATA_DIR = "$PWD\build\gui-test-data"
# Put a validated ffmpeg path in this directory's settings.json, or install it through the CLI.
Start-Process .\build\Release\LogForge.exe -ArgumentList '--smoke-test "input.mov" "new-output.mov"' -Wait
```

This path creates a native window, injects a drop into that window, uses the production worker, and emits `new-output.mov.gui-test.json` plus a PNG. It never simulates encoder progress or bypasses output validation.
