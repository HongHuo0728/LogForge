# LogForge

A small native Windows tool that re-encodes **BT.2020 HLG ProRes** into **Apple Log / BT.2020 ProRes 422 HQ** for a consistent grading workflow.

LogForge changes the pixels using published color mathematics. It does not restore clipped highlights, crushed shadows, tone-mapped-away detail, or information lost in a camera's ISP. It cannot turn processed phone footage into the original sensor capture.

**Version 0.1.0 is a working preview.** The mathematical transform and generated-media workflow are tested. Automatic Apple Log recognition in commercial editors is **not certified**. In your editor, manually assign **Apple Log transfer + Rec.2020 gamut**, with **Video data levels**. Do not select Apple Log 2 / Apple Gamut.

![LogForge Windows interface](docs/images/LogForge.png)

<!-- Screenshot slot for the next release: replace docs/images/LogForge.png with a new verified GUI capture. -->

## Features

- Native Win32 GUI, file dialogs, Unicode paths and file drag-and-drop.
- FFmpeg discovery and an optional verified, per-user HTTPS installer.
- Double-precision Apple Log and inverse HLG math; 32-bit float RGB transport.
- ProRes HQ 10-bit 4:2:2 MOV output; audio packet copy, frame rate and raster preservation.
- Timecode, creation metadata, original Make/Model and ordinary rotation preservation.
- Actual FFmpeg frame/time progress, cancellation, supervised child processes.
- Automatic output checks, local diagnostic logs and JSON validation reports.
- A developer CLI and a MOV atom / metadata comparison tool.

## Install and run

1. Download `LogForge-0.1.0-Windows-x64.zip` from this project's GitHub Releases when published.
2. Extract the ZIP and run `LogForge.exe`. No installer or administrator rights are required.
3. If FFmpeg is missing, select **一键安装 FFmpeg** or **手动选择 FFmpeg**.
4. Open or drop one supported video, choose a new output `.mov`, then select **转换为 Apple Log**.
5. Wait for output validation. An existing output file is never overwritten.

The preview is unsigned. Windows may show an unrecognized-publisher prompt. The Release build uses the static MSVC runtime (`/MT`); no separately installed VC++ runtime is required by LogForge. Windows 10 22H2 / Windows 11 x64 is the supported target. ARM64, macOS and Linux are not supported in v1.

The source build also produces `LogForge-cli.exe`; this developer tool is not required for the portable GUI release.

## Supported input

| Property | V1 requirement |
| --- | --- |
| Codec | ProRes 422 (Standard) or ProRes 422 HQ |
| Pixel format | `yuv422p10le`, 10-bit 4:2:2 |
| Primaries / matrix | BT.2020 / BT.2020 non-constant luminance |
| Transfer | Explicit HLG (`arib-std-b67`) |
| Range | Explicit video or full range |
| Timing | One progressive video stream, fixed cadence verified from all packets |
| Resolution | Even width, up to 8192 × 8192; no resize |
| Frame rate | Valid rational frame rate, at most 120 fps |

iPhone 13 Pro / 14 Pro HDR ProRes is the intended use case **when its actual stream parameters meet these requirements**. A device model name alone is not evidence of compatibility. VFR, HEVC/Dolby Vision, PQ, SDR, interlaced footage, ProRes LT/Proxy/4444, and missing/ambiguous color tags are refused. Timed metadata tracks, chapters, arbitrary track transforms and camera-specific edit lists are not reproduced. Additional audio streams are copied when MOV supports their codecs; otherwise conversion fails visibly.

## Output format and Apple Log workflow

- QuickTime MOV, ProRes `apch` / 422 HQ, 10-bit 4:2:2, BT.2020.
- Original resolution, rational fps and video frame count; audio is copied without re-encoding.
- Apple Log encoded RGB is mapped into **video-range YCbCr** (Y 64–940, C 64–960 nominal in 10-bit). Normalize these levels before applying an Apple Log LUT.
- `colr` is `nclc 9 / 2 / 9`: BT.2020 / unspecified transfer / BT.2020 matrix. `2` is deliberately honest; it is not an Apple Log ID. LogForge's own `logforge.transfer=Apple Log` metadata records intent but is not an Apple private identifier.
- In DaVinci Resolve, manually choose Rec.2020 input color space and Apple Log input gamma in CST, or assign the corresponding input color space in a managed workflow. Verify Clip Attributes data levels are Video. Use one technical input conversion; avoid applying both CST and a technical Apple Log-to-display LUT unintentionally.
- In other editors, explicitly declare Apple Log / BT.2020, then use an appropriate Apple Log LUT or transform. Commercial-editor behavior still requires testing with a real reference sample.

**Exposure convention:** 75% normalized HLG is interpreted as 90% scene reflectance. This is a documented exposure anchor, not an assertion about the phone's original metering or ISP. HLG metadata cannot determine the absolute capture exposure. The transform applies inverse OETF only, without a display OOTF, tone mapping, saturation changes or gamut conversion. See [color mathematics](docs/COLOR_PIPELINE.md).

## How FFmpeg works

The release ZIP contains **no FFmpeg binaries**. Detection order is managed LogForge tools, saved manual selection, PATH entries, then common installation locations. Both `ffmpeg.exe` and `ffprobe.exe` are executed. The program checks ProRes decoding, `prores_ks`, float/10-bit formats, required filters, and performs an actual encode/decode smoke test.

The installer currently pins **Gyan.dev FFmpeg 8.1.2 essentials**, a third-party Windows build, **GPLv3**. Download size is 109,728,040 bytes (about 105 MiB). HTTPS, embedded SHA-256, bounded retries, byte progress, extraction and post-install capability checks are implemented. It does not update PATH or install system components. Files are stored under:

```text
%LOCALAPPDATA%\LogForge\tools\ffmpeg\ffmpeg-8.1.2-essentials_build\
```

The provider interface allows controlled future version/source changes; this is intentionally not a floating "latest" URL. See [provider and licensing details](docs/FFMPEG_PROVIDER.md). FFmpeg.org supplies source and links to third-party Windows builds; it does not publish this binary.

## Build from source

Prerequisites: Visual Studio 2022 C++ desktop workload, a Windows 10/11 SDK, CMake 3.24+, Git. Python 3 is needed only for optional generated-media integration tests. The small nlohmann/json dependency is vendored; configure/build performs no dependency download.

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
.\build\Release\LogForge.exe
```

For the full integration suite, supply an already validated FFmpeg path:

```powershell
cmake -S . -B build -DLOGFORGE_TEST_FFMPEG="C:/ffmpeg/bin/ffmpeg.exe"
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```

Generate the small portable package:

```powershell
cpack --config build/CPackConfig.cmake -C Release -B dist
```

Pushes and pull requests configure/build/test on Windows. Version tags additionally create a ZIP artifact; the workflow does not silently publish a GitHub Release. See [validation details](docs/VALIDATION.md) and [architecture](docs/ARCHITECTURE.md).

## Local diagnostics and reference analysis

Logs, settings, transient files and validation JSON are stored under `%LOCALAPPDATA%\LogForge`. Nothing is uploaded. Logs contain media filenames, commands and media properties; review them before sharing. A custom `LOGFORGE_DATA_DIR` environment variable is supported for isolated test runs.

```powershell
.\build\Release\LogForge-cli.exe --detect
.\build\Release\LogForge-cli.exe --probe "input.mov"
.\build\Release\LogForge-cli.exe --convert "input.mov" "output_AppleLog.mov"
.\build\Release\LogForge-cli.exe --analyze "output_AppleLog.mov" "real_AppleLog_reference.mov" > comparison.json
```

Pass `--ffmpeg "C:\path\ffmpeg.exe"` to test a specific build. `--install-ffmpeg` exercises the same installer used by the GUI. Reference analysis reports known container atoms, raw ffprobe metadata, stream parameters and a structural JSON diff; it does not interpret unknown private atoms or assert binary equivalence.

## Limits and next validation

- Real iPhone HDR clips and native iPhone 15 Pro Apple Log reference clips have not yet been validated in this repository.
- No claim of native sensor dynamic range, recovery of clipped data, Apple encoder identity, camera certification, or exact iPhone MOV atom equivalence.
- Private Apple Log signaling and automatic editor detection are unfinished research, kept separate from the tested pixel transform. Manual input assignment is required.
- CPU float processing favors a verifiable reference implementation. No GPU acceleration, batch queue, preview player or HDR display pipeline is included.
- Large/long camera files, uncommon edit lists and unusual audio layouts need additional coverage. The validator fails closed when required preservation checks differ.

Highest priority: compare actual iPhone HLG and Apple Log samples in Resolve/Final Cut/Premiere, verify exposure/range behavior, and investigate published/private signaling only with evidence.

## License and trademarks

LogForge source is [MIT](LICENSE). See [third-party notices](THIRD_PARTY_NOTICES.md) for nlohmann/json and the independently obtained FFmpeg tools. GPL FFmpeg is a separate executable, not linked into LogForge; downloading it does not relicense its components under MIT. Redistributing FFmpeg yourself entails its own license/source obligations.

LogForge is an independent open-source project and is not affiliated with or endorsed by Apple Inc.

Apple, ProRes and Apple Log are trademarks of their respective rights holders. This project does not use the Apple logo. Software is provided without warranty; assess results against your own delivery requirements. Patent/trademark rights are not granted by the MIT license.
