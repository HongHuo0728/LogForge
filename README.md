# LogForge

[English](README.md) · [简体中文](README.zh-CN.md)

Native **Windows and iOS** applications that convert **BT.2020 HLG ProRes** into **Apple Log / BT.2020 ProRes** for a consistent grading workflow.

LogForge changes the pixels using published color mathematics. It does not restore clipped highlights, crushed shadows, tone-mapped-away detail, or information lost in a camera's ISP. It cannot turn processed phone footage into the original sensor capture.

**Windows 1.3.0 (26929A) · iOS 1.3.1.** Windows exports ProRes 422 HQ; iOS offers ProRes 422 and 422 HQ. Output uses **Apple Log / Rec.2020 with Video levels**. The identification fields were **verified in the Windows 1.1.0 release with DaVinci Resolve Studio 20.3.2.9**, using DaVinci YRGB Color Managed. The Windows verification scope is recorded in [VALIDATION](docs/VALIDATION.md); this historical evidence does not verify a new iOS editor import. This is Apple Log, not Apple Log 2 / Apple Wide Gamut. See the [native-reference and real-import evidence](docs/APPLE_LOG_IDENTIFICATION.md).

## Platforms

| | Windows | iOS |
| --- | --- | --- |
| Application | Native Win32 / C++ | Native Swift / SwiftUI / Metal |
| System | Windows 10 22H2 / Windows 11 x64 | iOS 26 or newer |
| Color processing | CPU or runtime-qualified NVIDIA CUDA | CPU or runtime-qualified Apple Metal |
| ProRes codecs | CPU through approved external FFmpeg tools | System codecs, with embedded software decoder/encoder fallback |
| Export | ProRes 422 HQ MOV | ProRes 422 / 422 HQ MOV |
| Interface languages | English, Simplified Chinese | English, Simplified Chinese, Traditional Chinese, French, Spanish |
| Distribution | Portable Windows ZIP | iPhone arm64 IPA; signing required |

Both platforms belong to the main [LogForge repository](https://github.com/HongHuo0728/LogForge). Windows remains 1.3.0; the current iOS patch version is 1.3.1. Availability depends on the assets uploaded to [Releases](https://github.com/HongHuo0728/LogForge/releases/tag/v1.3.0); a prepared release configuration does not mean its IPA has already been published. macOS and Linux applications are not provided.

## iOS 1.3.1

- Native transparent Liquid Glass interface, layered glass icon, accessibility contrast handling and haptic feedback.
- Import from Photos, files or folders; choose an export directory, process a queue, cancel and retry failed videos.
- Automatically fall back from unavailable system ProRes decoding/encoding to the embedded software codecs. CPU color processing and Metal color processing remain selectable.
- Convert supported **ProRes 422 / 422 HQ, BT.2020 HLG MOV** footage to Apple Log; HEVC, PQ, SDR and other ProRes profiles are not accepted by the current input contract. A phone's camera-recording capability is separate from software conversion capability.
- Preserve and independently validate frame timing, audio, auxiliary tracks and display rotation. iOS accepts valid variable frame timing; the Windows fixed-cadence requirement below applies to Windows.
- Open the app's Apple Settings page with the top-left language button. Select English, Simplified Chinese, Traditional Chinese, French or Spanish when the system exposes the preferred-language setting.
- Show only `1.3.1` in the app; reports retain each conversion attempt’s version and internal diagnostics.

Download the iOS IPA and matching RelinkKit from the main release when uploaded. The provided build pipeline produces an **unsigned** IPA: sign it with your own valid Apple identity and provisioning profile before installation. No App Store or TestFlight release is configured. Keep the corresponding source, license and relink material when distributing the app.

For cloud builds, use **iOS build and tests** in the main repository's [Actions](https://github.com/HongHuo0728/LogForge/actions/workflows/build-ios.yml). Simulator tests must pass before the iPhone archive is packaged. See the [iOS build and installation guide](https://github.com/HongHuo0728/LogForge/blob/main/LogForgeMac/docs/GITHUB_BUILD.md).

iOS 1.3.1 fixes AAC/timecode track preservation and improves license navigation. [Release details](docs/RELEASE_1.3.1_IOS.md) distinguish regressions, cloud builds and device verification. The user's original video has not been supplied; simulator results do not establish physical-device performance or editor recognition.

![LogForge Windows interface](docs/images/LogForge.png)

The screenshot uses an empty workspace; no personal video or camera image is included.

## Windows: what's new in 1.3.0

- **Fractional CFR compatibility:** verify the shared integer-timestamp quantization model over every packet boundary. A generated 309-frame regression reproduces the reported 59.94/59.970888 case and its old 1.06-tick rejection. Nominal FPS does not veto an otherwise verified fixed clock; real changing cadence and damaged timestamps still fail. See the [timing contract](docs/CADENCE_1.3.0.md).
- **Explicit input interpretation:** missing primaries, matrix, range and chroma tags use documented defaults with warnings. Explicit BT.709 conflicts are rejected unless deliberately overridden. CLI probe, GUI Details and reports distinguish declarations, assumptions and overrides. See the [input contract](docs/INPUT_CONTRACT_1.3.0.md).
- **Camera compatibility retained:** iPhone auxiliary mebx tracks and missing timecode remain accepted. Blackmagic audio offset/PCM copy and float32 portrait rotation remain intact.
- **Predictable queue output:** preflight every filename, reserve stable `_2`, `_3` suffixes for collisions and preview the input/output mapping in Details. Existing files are never overwritten.
- **Honest completion reports:** distinguish media validation from final publication. Reports retain a failed rename's reason even when the encoded media passed validation.
- **Audio and recovery safeguards:** hash copied audio payloads per stream; use file identities for both failure cleanup and crash recovery. Extra attached pictures/data/subtitle tracks are explicitly reported as omitted; multiple primary videos are rejected.
- **CUDA engineering:** deterministic device ranking and a process-local qualification cache bound to device, driver, kernel, build and Creative parameters. Color kernels and numerical tolerances are unchanged. Buffer reports distinguish individual slots, total bridge memory and GPU allocations.
- **Strict CLI integers:** reject trailing garbage, overflow, empty values and nonpositive `--cancel-after-frames` values before tool discovery.

See [1.3.0 release details](docs/RELEASE_1.3.0.md) and the [validation record](docs/VALIDATION.md). The bilingual academic papers in `AcademicPapers` are updated for 1.3.0, with current release evidence and explicitly labeled historical measurements.

## Previous release: 1.2.1

- **Upright portrait output:** apply cardinal rotation directly to float32 pixels before encoding. A 3840x2160 source with a 90-degree display rotation becomes a 2160x3840 output with identity orientation, so playback no longer depends on rotation-tag support.
- **Simpler camera admission:** the recording-format gate requires ProRes Standard/HQ and HLG. Missing timecode and auxiliary metadata are not requirements. Edit lists are recorded and interpreted by FFmpeg, rather than rejected by a camera-specific pattern whitelist; iPhone `mebx` metadata tracks are omitted.
- **Audio synchronization:** preserve the source movie clock and stream-copy audio, including Blackmagic Cam start offsets. Keep available source timecode and creation metadata.
- **Documentation correction:** CUDA color processing and the multi-file queue introduced in 1.2.0 remain available; ProRes decoding and encoding still run on the CPU.

See [1.2.1 release details](docs/RELEASE_1.2.1.md) and [minimal regression results](docs/VALIDATION.md). Color mathematics, numerical tolerances and other features are unchanged.

## Previous release: 1.2.0

- **Portrait MOV fixes:** preserve movie/video/audio creation timestamps and source creationdate through all supported rotations. Compare timestamp meaning, and preserve drop-frame timecode during remux.
- **Media safeguards:** explicit QuickTime container and edit-list policy, chapter validation, unsupported display-matrix rejection, structural identification writing and first/middle/last pixel sanity sampling.
- **Processing backends:** Auto, CPU or NVIDIA RTX CUDA. The GPU accelerates precise color mathematics; ProRes decoding and encoding remain on the CPU. Auto falls back safely after GPU failures, while forced CUDA fails explicitly.
- **Measured CPU pipeline:** adaptive thread allocation and three bounded Standard buffers overlap decoding, color processing and encoding. Reports distinguish CPU time, color time, pipe waits, remux, metadata, validation and GPU copy/kernel timings. See the [benchmark](docs/BENCHMARK_1.2.0.md).
- **Batch conversion:** select/drop several videos; each gets its own validation report and a failed item does not stop later items.
- **Correct main-window layout:** one measured, scrollable layout separates source/output/status/footer, with a single renderer per status text and full relocation invalidation.
- **Storage reliability:** cross-process settings/trust locking, ownership-checked abandoned temporary-file recovery, bounded retention and report-directory preflight. No output is published after report failure.
- **Release integrity:** an actual `.zip.sha256` accompanies the ZIP, and packaging audits its contents and executable identity.

See [1.2.0 release details and limitations](docs/RELEASE_1.2.0.md). The Apple Log/HLG reference equations, BT.2408 scale, float32 intermediate, BT.2020, nclc 9/2/9, Video levels, packet cadence checks and color tolerances remain unchanged.

## Previous release: 1.1.1

- **Fast FFmpeg startup:** Quick discovery has a shared three-second filesystem budget. A verified managed/saved pair avoids later searches. Other sources include PATH, Windows App Paths, package installations, bounded common folders and the existing Windows Search index.
- **Deep search is explicit:** use **Deep search all drives** only when needed, or `LogForge-cli --detect --deep-search`. Startup never falls through to whole-drive traversal.
- **Review discovered candidates directly:** select a path from the candidate list, review the real executable pair and hashes, and approve before any tool runs. Missing probes and changed/incompatible tools retain their reasons. Search cancellation and failures restore download/manual/retry actions.
- **Supervised discovery:** potentially blocking filesystem/index queries run in a hidden mode of LogForge itself, with Job Object cancellation and deadlines. Discovery and verification times are reported separately; hashes and numerical qualification are still checked.
- **Reliability fixes:** descendant-held pipes remain cancellable after a parent exits; excessive output and reader exceptions fail safely. Mixed-case MOV extensions, numeric metadata bounds, temporary trust-file cleanup and worker-buffer allocation are corrected.
- **Regression audit:** see [confirmed defects and remaining limits](docs/BUG_AUDIT_1.1.1.md) and [actual verification](docs/VALIDATION.md). Apple Log/HLG equations, reference scaling, numerical tolerances and the Resolve identification writer are unchanged.

## Previous release: 1.1.0

Version **1.1.0 (26923C)** includes all changes beginning with the 29.99 fps CFR fix, including the verified Resolve Apple Log identification work. The Apple Log equations, inverse HLG OETF and BT.2408 reference scaling remain unchanged. See the [1.1.0 changelog](CHANGELOG.md) for the complete release scope.

- **Fixed false VFR detection:** fixed-cadence 29.99, 29.98 and 29.9701 fps footage is accepted even when average and nominal rate tags differ. Every video packet is checked for PTS, duration, interval and cumulative phase; true VFR, gaps, duplicate/backwards timestamps and sustained drift are rejected with a packet-specific reason.
- **Safer FFmpeg discovery:** scanning discovers paths without executing unknown programs. A verified managed download or explicit user approval is required. Approval binds both executable paths and SHA-256 hashes; changed files require approval again. Execution uses the locked canonical pair, including protection against an unapproved ffprobe beside an executable alias.
- **Numerical FFmpeg qualification:** small reference signals check matrix/range conversion, left/center chroma phase and ProRes encode/decode results. Only **Verified FFmpeg** may transcode; **Compatible but unverified FFmpeg** is available for inspection.
- **Metadata whitelist:** format, video and audio tags follow an explicit safe-copy policy. Source creation time, real camera make/model and timecode can be preserved; conflicting HLG/HDR/Dolby/PQ/custom-gamma fields are removed and recorded.
- **Explicit chroma handling:** input left/center siting is passed to the conversion filter. Missing siting requires a user declaration, unsupported siting is rejected, and output validation records its declaration and numerical phase evidence.
- **Better reference MOV analysis:** resolve `meta/keys/ilst/mdta/data` entries into key names and typed values, inspect ProRes sample-entry extensions, and compare semantic metadata with `--analyze A B` without treating file sizes or byte offsets as meaningful differences.
- **Verified Resolve Apple Log identification:** a separate writer adds the native-reference `logs` sample-entry identifier after encoding. Actual Resolve A/B imports establish the minimal field; the color math, nclc 9/2/9, Video levels, encoder identity and original camera metadata remain unchanged. Native video-description terminators are now parsed correctly.
- **Lower-memory, parallel CPU processing:** the standard path streams at most 4 MiB of float samples through persistent workers; Creative adjustments use parallel RGB tiles. Results are tested for exact float equality with the scalar implementation.
- **Stronger output validation:** JSON reports include average/nominal rates, verified cadence, maximum timing errors, FFmpeg identity/trust, chroma evidence and preserved/removed metadata. Publication also refuses a destination created while conversion is running.
- **Expanded regression coverage:** timing edge cases, untrusted executable non-execution, hash changes, metadata parsing/conflicts, chroma siting, drop-frame timecode, multiple audio streams, 180/270-degree rotation, post-encode numeric sampling and a 240-frame 4K120 conversion.

Automatic identification was checked with an unmodified iPhone 15 Pro Max / Blackmagic Camera original, a negative LogForge baseline, isolated metadata candidates and a real converted output. No camera model or Apple encoder is spoofed. Other Resolve versions/editions, Premiere and Final Cut remain unverified. See the [metadata contract](docs/METADATA.md) and [validation record](docs/VALIDATION.md).

## Windows features

- Native Win32 GUI, file dialogs, Unicode paths and file drag-and-drop.
- Bounded Quick FFmpeg discovery and optional manual Deep search; explicit path/hash approval before external tools may run, plus a pinned HTTPS installer.
- Numerical FFmpeg qualification (matrix, range, left/center chroma phase and post-ProRes code values).
- English (default) and Simplified Chinese; dark (default) and light themes, saved in Settings.
- Original spectrum-and-curve icon; Windows version 1.3.0.0 and in-app build display 1.3.0 (26929A).
- Runtime-qualified CPU/CUDA color processing, a serial multi-file queue and detailed per-job reports.
- Double-precision Apple Log and inverse HLG math; 32-bit float RGB transport.
- ProRes HQ 10-bit 4:2:2 MOV output; audio packet copy, frame rate and raster preservation.
- Timecode, creation metadata, original Make/Model and ordinary rotation preservation.
- Actual FFmpeg frame/time progress, cancellation, supervised child processes.
- Automatic output checks, local diagnostic logs and JSON validation reports.
- Optional shadow lift, highlight compression and saturation adjustment, with a separate enable switch; unchanged Apple Log encoding follows the creative adjustment.
- Per-component signal-range accounting over every frame, with visible warnings for Apple Log floor clipping or above-nominal-white signals.
- A developer CLI and a MOV atom / metadata comparison tool.

## Windows installation

1. Download `LogForge-1.3.0-Windows-x64.zip` and its `.sha256` file from this project's GitHub Releases when published.
2. Extract the ZIP and run `LogForge.exe`. No installer or administrator rights are required.
3. Let LogForge discover FFmpeg paths. Unapproved programs are **not executed**. Use **Review FFmpeg...** to select a discovered candidate and review both SHA-256 hashes, then explicitly approve it. If none is usable, retry Quick search, explicitly start Deep search, choose a file manually, or use **Download FFmpeg** for the pinned build. Buttons disappear after approved tools pass numerical verification. An old saved path alone is not execution approval.
4. Open or drop supported videos. Missing chroma siting defaults to left for this ProRes camera workflow; an explicit CLI override remains available. Choose a new output `.mov` for one file, or an output directory for a queue, then select **Convert to Apple Log**.
5. Wait for output validation and final publication. An existing output file is never overwritten. If the final report update fails after a successful rename, the valid movie is retained and the job reports an error; see the release notes for this two-file transaction limitation.

The Windows executable is unsigned. Windows may show an unrecognized-publisher prompt. The Release build uses the static MSVC runtime (`/MT`); no separately installed VC++ runtime is required by LogForge. Windows 10 22H2 / Windows 11 x64 is the Windows target; Windows ARM64 is not supported. The iOS arm64 application is distributed separately as an IPA.

The source build also produces `LogForge-cli.exe`; this developer tool is not required for the portable GUI release.

For exposure matching, keep **0 EV** unless you have a deliberate reason to adjust it. A positive offset lifts scene exposure before Apple Log encoding. It does not correct unknown camera rendering or certify a native-camera match. In the developer CLI, use `--exposure-ev 1` for +1 stop, for example.

## Windows settings

Open **Settings** in the top right:

- **Language:** English or Simplified Chinese. English is the first-run default, independent of the Windows display language.
- **Appearance:** Dark or Light. Dark is the first-run default.
- **Processing backend:** Auto (default), CPU or NVIDIA RTX CUDA. Auto tries a usable CUDA device and falls back to CPU with a recorded reason. Forced CUDA reports failure if unavailable or unqualified. The app uses the installed NVIDIA driver; no bundled CUDA runtime or Toolkit is required.
- **Creative adjustments:** Off by default. Enable to lift shadows, reduce highlights and adjust saturation; the initial values are **3 EV / 1 EV / 85%**. The detailed controls appear when enabled and retain their values when disabled.

**Save** applies changes immediately and persists them for the next launch. **Cancel** discards changes. Settings are disabled during discovery, downloads, probing or conversion. Windows-owned file pickers and operating-system error text may use the Windows language.

Creative adjustment is an intentional grade applied before the unchanged Apple Log encoding, not a native-camera appearance match. Turning it off restores the standard conversion. See the [equations and limits](docs/CREATIVE_ADJUSTMENTS.md). Exposure remains a separate control on the main window. Status, errors, progress details, license and local-processing notes appear at the bottom left; recording requirements and editor assignment appear at the bottom right.

## Windows supported input

| Property | Windows requirement |
| --- | --- |
| Container | Nonfragmented QuickTime MOV; `qt  ` major brand, or legacy MOV without a brand |
| Codec | ProRes 422 (Standard) or ProRes 422 HQ |
| Pixel format | `yuv422p10le`, 10-bit 4:2:2 |
| Primaries / matrix interpretation | BT.2020 / BT.2020 NCL; missing tags are assumed with warnings; explicit conflicts require deliberate override |
| Transfer | Explicit HLG (`arib-std-b67`) |
| Range | Declared full/limited; unspecified assumes limited with a warning; other explicit tags are rejected |
| Timing | One primary video stream, fixed cadence verified from every packet's PTS, duration, interval and cumulative phase |
| Chroma location | Use the source declaration or explicit override; missing declaration defaults to left |
| Resolution | Even width, up to 8192 × 8192; no resize |
| Frame rate | Valid rational frame rate, at most 120 fps |

The listed target devices are **iPhone 13 Pro, iPhone 13 Pro Max, iPhone 14 Pro and iPhone 14 Pro Max**. Record **ProRes 422 HDR or ProRes 422 HQ HDR (10-bit BT.2020 HLG)**. Choose your recording resolution and frame rate freely within the format guards above; LogForge preserves the source settings and does not force 4K or 24 fps. Admission depends on actual media parameters, not a model-name allowlist. VFR, HEVC/Dolby Vision, PQ, SDR, ProRes LT/Proxy/4444 and missing/non-HLG transfer tags are refused. Testing targets progressive camera recordings; there is no automatic deinterlacing. Missing auxiliary color tags use the interpretation documented above. Chapters are copied and validated. Nonessential metadata/data tracks are removed with a report entry. Cardinal rotations are applied to float32 pixels; 90/270 degrees exchange the output width and height without resizing. Other display matrices remain metadata. Edit-list shape, missing timecode and auxiliary metadata do not block format admission. FFmpeg interprets the video/audio playback timeline, while nonessential metadata/data tracks are omitted and recorded. Additional audio streams are copied when MOV supports their codecs; otherwise conversion fails visibly.

Identical packet durations/intervals establish the exact rational cadence. Nonuniform floor/ceil timestamps must share one quantization cell across the entire sequence, including the final packet endpoint. Nominal and average tags cannot override packet evidence. Changing-speed drift, gaps and duplicate/reverse PTS are rejected with time-base, microsecond and frame-fraction diagnostics. The float pipeline normalizes timestamp quantization; it does not preserve arbitrary VFR timestamps. See the [timing policy and its limits](docs/CADENCE_1.3.0.md).

The optional CLI flag `--force-bt2020-interpretation` is only for a known incorrect primaries/matrix declaration. It does **not** perform gamut conversion and can produce incorrect colors if the source really is BT.709. The GUI offers an explicit, default-No confirmation for a single conflicting clip. PQ/non-HLG, unsupported chroma/range, codec/profile and container cannot be forced through this option. A GUI batch keeps normal admission for every item; the CLI flag applies deliberately to that CLI job or batch.

## Windows output format and Apple Log workflow

- QuickTime MOV, ProRes `apch` / 422 HQ, 10-bit 4:2:2, BT.2020.
- Original pixel count, rational fps and video frame count; 90/270-degree orientation swaps raster width/height, and audio is copied without re-encoding.
- Apple Log encoded RGB is mapped into **video-range YCbCr** (Y 64–940, C 64–960 nominal in 10-bit). Normalize these levels before applying an Apple Log LUT.
- `colr` is `nclc 9 / 2 / 9`: BT.2020 / unspecified transfer / BT.2020 matrix. `2` is deliberately honest; it is not an Apple Log ID. LogForge's own `logforge.transfer=Apple Log` metadata records intent but is not an Apple private identifier.
- The ProRes sample entry additionally contains `logs = com.apple.rec2020.apple-log`, observed in a camera original and independently verified by Resolve import.
- In **DaVinci YRGB Color Managed**, the tested Resolve version automatically detects **Apple Log** (Rec.2020 gamut). Its Auto data-level interpretation matched an explicit Video control in a real render. Use one technical input conversion; avoid adding a redundant CST or Apple Log-to-display LUT after managed conversion.
- Unmanaged DaVinci YRGB still needs an intentional viewing transform, such as Rec.2020 / Apple Log input in a CST. Identification and display conversion are different operations. Other editors/versions must be checked separately before assuming automatic behavior.

**Exposure reference:** 75% normalized HLG is interpreted as 100% scene reflectance, following the nominal reference in BT.2408. At zero exposure offset, an 18% gray reference maps from HLG 0.378259 to Apple Log 0.488272. This does not establish the phone's original metering or ISP behavior. Use the **Exposure** control only for an intentional exposure adjustment; zero is the default. The standard path applies inverse OETF and a scene-linear gain, without a display OOTF, tone mapping, saturation changes or gamut conversion. Optional creative rendering is applied only when explicitly enabled and is recorded in output metadata. See [color mathematics and monitoring](docs/COLOR_PIPELINE.md).

## Windows FFmpeg tools

The release ZIP contains **no FFmpeg binaries**. Quick discovery searches managed/saved paths, app-adjacent tools, PATH, App Paths, actual WinGet/Scoop/Chocolatey installations, bounded common folders and the existing Windows Search index. It never starts a full-drive search automatically. Deep search of accessible local drives is an explicit user action. It records unknown paths without running them. Only a pinned, SHA-256-verified download or an explicitly approved pair may execute. Approval is stored separately as canonical paths plus SHA-256 for **both ffmpeg and ffprobe**; changes require approval again. While in use, deny-write/delete handles protect the approved executable images. Network shares, inaccessible/offline directories and reparse subdirectories are excluded from disk traversal; skipped locations are reported.

A compatible feature list is not sufficient for conversion. **Verified FFmpeg** additionally passes integer-signal matrix/range tests, half-pixel chroma tests and ProRes HQ encode/decode sampling. **Compatible but unverified FFmpeg** may be inspected but cannot transcode. Failed numerical tests are errors, never warnings. See [trust and provider details](docs/FFMPEG_PROVIDER.md).

The Standard path uses three chunks of at most 4 MiB each to overlap CPU decode,
color transform and CPU encode. Creative adjustment retains one planar float32
frame so RGB channels stay correctly paired. CPU workers use the unchanged scalar
equations. CUDA uses precise double expressions with float32 transport and must
pass independent reference qualification. CPU operation remains available without
a GPU. No fast math, FP16 or LUT approximation is used.

The installer currently pins **Gyan.dev FFmpeg 8.1.2 essentials**, a third-party Windows build, **GPLv3**. Download size is 109,728,040 bytes (about 105 MiB). HTTPS, embedded SHA-256, bounded retries, byte progress, extraction and post-install capability checks are implemented. It does not update PATH or install system components. Files are stored under:

```text
%LOCALAPPDATA%\LogForge\tools\ffmpeg\ffmpeg-8.1.2-essentials_build\
```

The provider interface allows controlled future version/source changes; this is intentionally not a floating "latest" URL. See [provider and licensing details](docs/FFMPEG_PROVIDER.md). FFmpeg.org supplies source and links to third-party Windows builds; it does not publish this binary.

## Build Windows from source

Prerequisites: Visual Studio 2022 C++ desktop workload, Windows SDK 10.0.22621 or newer, CMake 3.24+, Git. Python 3 is needed only for optional generated-media integration tests. The small nlohmann/json dependency is vendored; configure/build performs no dependency download.

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
.\build\Release\LogForge.exe
```

For the full integration suite, supply an FFmpeg build you trust. Test runners explicitly approve that selected pair in isolated test profiles; they do not approve arbitrary scan results. Tests generate their own signals, including 240 frames of 4K120, and need several minutes and free disk space:

```powershell
cmake -S . -B build -DLOGFORGE_TEST_FFMPEG="C:/ffmpeg/bin/ffmpeg.exe"
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```

Generate the small portable package:

```powershell
cpack --config build/CPackConfig.cmake -C Release -B dist
python tests/package_audit.py --zip dist/LogForge-1.3.0-Windows-x64.zip --exe build/Release/LogForge.exe
```

Pushes and pull requests configure/build/test on Windows. Every build creates a ZIP artifact; version tags must match the CMake version, and the workflow does not silently publish a GitHub Release. See [validation details](docs/VALIDATION.md) and [architecture](docs/ARCHITECTURE.md).

On an interactive Windows desktop, run the native settings/theme/language suite:

```powershell
python tests/gui_smoke.py --exe build/Release/LogForge.exe --ffmpeg C:/ffmpeg/bin/ffmpeg.exe --work build/gui-check
```

After the integration suite has generated its fixtures, add `--input build/integration/standard.mov` to test the actual drop-to-conversion flow in both standard and creative modes. Test hooks capture only LogForge windows and use an isolated `LOGFORGE_DATA_DIR`. The explicitly injected missing-FFmpeg UI case does not replace the real disk-discovery/capability tests. No personal recordings are needed.

## Windows diagnostics and reference analysis

Logs, settings, transient files and validation JSON are stored under `%LOCALAPPDATA%\LogForge`. Nothing is uploaded. Logs contain media filenames, commands and media properties; review them before sharing. A custom `LOGFORGE_DATA_DIR` environment variable is supported for isolated test runs.

```powershell
.\build\Release\LogForge-cli.exe --detect
.\build\Release\LogForge-cli.exe --probe "input.mov"
.\build\Release\LogForge-cli.exe --convert "input.mov" "output_AppleLog.mov"
.\build\Release\LogForge-cli.exe --analyze "output_AppleLog.mov" "real_AppleLog_reference.mov" > comparison.json
```

The CLI also provides `--version`, `--help` and `--language en|zh-CN`; English is the CLI default. Pass `--ffmpeg "C:\path\ffmpeg.exe"` to select an explicitly approved build. `--install-ffmpeg` exercises the GUI installer. Reference analysis includes raw atoms/probe data plus a semantic diff of resolved metadata and stream parameters; it does not guess unknown private meanings or assert binary equivalence.

## Windows limits and next validation

- A private iPhone HDR clip was used for earlier regression fixes; its test derivatives have been removed and are excluded from source and packages. Routine regression tests use generated media. The public camera reference used for Resolve metadata testing is also excluded from source/packages.
- No claim of native sensor dynamic range, recovery of clipped data, Apple encoder identity, camera certification, or exact iPhone MOV atom equivalence.
- Resolve Studio 20.3.2.9 managed import is verified. Premiere, Final Cut, other Resolve versions and native-camera appearance matching remain separate validation tasks.
- CPU and optional runtime-qualified NVIDIA CUDA color processing are available, along with a serial multi-file queue. ProRes decoding and encoding remain CPU operations. No preview player or HDR display pipeline is included.
- Large/long camera files, uncommon edit lists and unusual audio layouts need additional coverage. The validator fails closed when required preservation checks differ.

Highest priority: repeat the controlled metadata import test in other Resolve versions, Premiere and Final Cut; obtain matched HLG/native-Log captures for exposure and camera-rendering comparisons.

## Windows developer CLI approval and analysis

```powershell
.\build\Release\LogForge-cli.exe --approve-ffmpeg --ffmpeg C:\ffmpeg\bin\ffmpeg.exe
.\build\Release\LogForge-cli.exe --detect --ffmpeg C:\ffmpeg\bin\ffmpeg.exe
.\build\Release\LogForge-cli.exe --convert input.mov output.mov --ffmpeg C:\ffmpeg\bin\ffmpeg.exe --input-chroma-location left
.\build\Release\LogForge-cli.exe --analyze output.mov reference.mov --ffmpeg C:\ffmpeg\bin\ffmpeg.exe
```

`--approve-ffmpeg` is an explicit execution authorization. Review the selected files first. Omit `--input-chroma-location` when ffprobe reports a supported siting; an override is for a known but unlabelled source, not a guess. `--check-compatible --ffmpeg PATH` reports capability-only status without certifying numeric behavior. The analyzer resolves metadata key indices, values and ProRes extensions; its semantic diff ignores `mdat` offsets and file sizes. No native Apple sample is bundled.

## License and trademarks

LogForge source is [MIT](LICENSE). See [third-party notices](THIRD_PARTY_NOTICES.md). Windows obtains GPL FFmpeg as a separate executable. iOS statically links a separately licensed LGPL-only subset of FFmpeg and distributes corresponding source and relink material. These components retain their own licenses; LogForge's MIT license does not relicense them.

LogForge is an independent open-source project and is not affiliated with or endorsed by Apple Inc.

Apple, ProRes and Apple Log are trademarks of their respective rights holders. This project does not use the Apple logo. Software is provided without warranty; assess results against your own delivery requirements. Patent/trademark rights are not granted by the MIT license.
