# Validation record

## 1.2.0 (26926A): portrait MOV, CUDA, stability and interface

Local verification: **2026-09-26**, Windows 11 x64 build 26200, 20 logical CPUs,
MSVC Release with static runtime. UI/CLI display `1.2.0 (26926A)`; Windows
FileVersion, ProductVersion and manifest are **1.2.0.0**. The tested GUI EXE is
1,511,424 bytes. Its imports contain Windows libraries, without a VC++ runtime,
FFmpeg or CUDA runtime DLL dependency.

- Configure and Release build passed. The complete **22/22 CTest suite passed
  in 225.77 seconds**, including independent scalar math, CFR/VFR, FFmpeg trust
  and numerical verification, metadata/chroma, audio/timecode, no-overwrite,
  cancellation and actual encoding. Existing color tolerances were not relaxed.
  `AppleLog.cpp`, `HLG.cpp` and `Cadence.cpp` are unchanged from the starting release.
- Portrait regressions construct actual MOV headers with distinct movie, video,
  audio and timecode creation timestamps. **0, 90, -90, 180 and 270 degrees** all
  pass semantic tag comparison, structural header preservation, timecode and
  output validation. These are generated structural fixtures, not private footage.
- New regression paths cover chapter titles/times and removed metadata tracks,
  unsafe trim/edit lists, mirrored matrices, Creative completion, ownership-checked
  stale partial cleanup (including locked-file retry), four concurrent state-store
  writers, report-save failure, insufficient conversion/remux space, Auto GPU
  fallback and forced-GPU failure. Failed jobs publish no final movie.
- CUDA qualification passed on **NVIDIA GeForce RTX 3080 Ti Laptop GPU, 16 GB,
  driver 616.92 / DLL 32.0.16.1692 / CUDA Driver API 13040**. Six cases each compare
  24,576 samples against independent scalar CPU double calculations: Standard
  and Creative at -8, 0 and +8 EV, including ramps, signed shadows, super-white,
  saturated colors and random values. Observed maximum and mean float32 output
  error were **0** for every case. Qualification tolerance remains 2e-6; this new
  gate does not replace any older stricter scalar or encoded-signal test.
- All **20 benchmark cases** passed production validation: 1080p24 and
  4K24/30/60/120, CPU/CUDA, Standard/Creative. Timing definitions, peak application
  and process-tree memory, worker counts and GPU copy/kernel times are in the
  [benchmark record](BENCHMARK_1.2.0.md).
- The independent **240-frame 4K120 CPU Standard** test passed in **45.202 seconds
  of conversion time / 5.309 fps**, versus a 65.072-second serial-bridge baseline
  on this machine. Application peak private memory was **14.43 MiB**, with three
  chunks of at most 4 MiB. The 1,536 post-encode luma samples across frames
  0/119/239 had maximum error **0.8651 code values**, below the unchanged limit of 2.
- **26 native GUI scenarios passed**: English/Simplified Chinese × dark/light ×
  five DPI values, two missing-FFmpeg scenarios, closing an active held-pipe
  search, real Standard/Creative conversions and a queue whose first file fails
  while the second succeeds. Approval refusal, Settings Save/Cancel, focus,
  failure/cancel recovery and per-item reports were checked. Repeated dialogs
  require exactly unchanged GDI/USER resource counts after the existing warmup.
- **960 production-layout cases and 20 distinct snapshots passed** at
  100/125/150/175/200% DPI and 1280×720, 1920×1080, 2560×1440 client sizes, both
  languages/themes, Creative on/off, FFmpeg ready/missing and idle/running/failed/
  completed states. Checks cover non-overlapping logical rectangles, reachable
  scroll content, footer separation, single status rendering, DPI relayout and
  stable GDI use. They exercise native windows and the real `WM_DPICHANGED`,
  layout and paint paths; they do not claim three physical monitor setups.

### Fresh Resolve import

**DaVinci Resolve Studio 20.3.2.9** imported this build's new generated Standard
and Creative outputs in a fresh, temporary DaVinci YRGB Color Managed project.
Both reported `Input Color Space = Apple Log`, `IDT = Apple Log`,
`Data Level = Auto`, an empty Input LUT, ProRes 422 HQ and 10-bit. The test did
not assign any clip color space, gamma or LUT, and camera make/type remained
empty rather than invented. The temporary project was deleted and the headless
Resolve instance launched for this check was closed.

This **verifies automatic identification for these v1.2.0 outputs in that Resolve
version**. It is not a new native-camera A/B experiment or a new render-level
comparison; the separate 1.1.0 evidence is retained below. Premiere and Final Cut
remain untested.

### Limits and release evidence

Only one physical CUDA GPU and this Windows machine were tested. Other GPU
families/drivers must pass the same runtime qualification. Disk-full, CUDA error
and OOM paths use deterministic injected failures; the physical disk and GPU
were not deliberately exhausted. Creative still holds one complete planar RGB
frame. One earlier GUI resource sample changed without an established root
cause; its isolated recheck and the final strict 26-case suite passed, with no
relaxation of resource assertions. This is recorded as a remaining observation,
not a proven leak repair or a claim that all possible bugs are eliminated.

The [compact verification record](verification/1.2.0.json) includes the tested
EXE hash, test groups, CUDA qualification, GUI cases, sustained result and fresh
Resolve import properties. The portable ZIP audit passed its exact **22-file
allowlist**, CRC integrity, byte-identical tested EXE and actual matching
`.zip.sha256` sidecar checks. The executable extracted from the ZIP also passed
native startup, Settings and missing-tool/failure-recovery smoke checks. Generated
media and temporary app/editor profiles were removed after verification and are
excluded from Git and the release. Compact reports and synthetic UI snapshots
remain; the empty main-window screenshot comes from the tested executable.

## 1.1.1 (26923D): discovery and reliability

Local verification: 2026-09-23, Windows build 26200, MSVC x64 Release, static runtime.

- Configure and Release build passed. The complete **19/19 CTest** suite passed in
  **240.65 seconds**, including actual codec conversions, timing/metadata/audio/
  rotation, numeric samples, publication protection and 240 frames of 4K120.
- After final discovery/UI refinements, **14/14 core/resource tests** passed in
  **12.46 seconds**. This includes the added long-path regression on a system where
  ordinary non-extended filesystem paths cannot exceed MAX_PATH. No tolerance or
  validator was weakened. AppleLog.cpp, HLG.cpp and AppleLogIdentification.cpp are
  unchanged from 1.1.0.
- Saved approved pair: first isolated-profile launch **47 ms discovery + 1797 ms
  full verification** (1853 ms total); repeat **47 + 1734 ms** (1788 ms total).
  These are application launches, not a rebooted/cold Windows file-cache benchmark.
  With no approved/saved pair, the optional-source pass returned zero candidates
  in 141 ms on this host. That result does not mean FFmpeg is absent everywhere.
- Owned stalled-helper tests enforce the Quick deadline while retaining streamed
  candidates. Other checks cover Deep opt-in, fake executable non-execution,
  environment package paths, quoted/expanded registry values, Unicode/long paths,
  missing pairs, duplicate results, cancellation, hash changes and trust-save failure.
- A held-pipe descendant test verifies that root-process exit cannot defeat the
  timeout/cancel watcher. Long stdout/stderr and throwing callback fixtures fail
  safely. The GUI close test also confirms no surviving descendant.
- The 4K120 test processed **240 frames, 3840 x 2160**, with a **4 MiB application
  float buffer**. Conversion took **64.02 seconds (3.75 fps)**; this is support for
  120-fps source media, not a real-time performance claim. Maximum sampled
  post-encode luma error was **0.656 code values**, within the unchanged limit.
  Application private memory was about 6.27 MiB; this excludes FFmpeg child memory.
- The native GUI matrix covers English/Simplified Chinese, dark/light and
  96/144/192 DPI layout values, plus two missing-tool cases. It exercises real
  candidate dialogs, No on execution approval, Settings Save/Cancel, injected
  download/hash failures, actual search cancellation and repeated dialogs.
  Native caches are warmed with 20 identical cycles before measuring another 20;
  GDI and USER resource counts must remain exactly equal. Earlier unequal samples
  were decreases caused by cache/teardown state, not a relaxed leak threshold.
- **Resolve import was not reverified in 1.1.1.** Resolve was launched, but the
  installed scripting SDK returned no connection. No temporary project was created
  and no clip settings were changed. The unchanged writer's real 1.1.0 native/A-B
  evidence below remains historical evidence; it is not reported as a new pass.
- Version resource checks passed: text FileVersion/ProductVersion 1.1.1,
  numeric resource/manifest 1.1.1.0, UI/CLI 1.1.1 (26923D).

Final GUI checks passed: **14 language/theme/DPI/missing-tool cases**, **one
close-during-active-search case**, and **two real standard/Creative conversions**.
All final focus, CRLF, approval-refusal, cancellation and resource assertions passed.
The empty English/dark main-window screenshot was refreshed from this final EXE.
The portable ZIP is audited against the **17-file allowlist**, CRC integrity and
byte-identical Release EXE; FFmpeg, test tools, media, logs and settings are excluded.
The adjacent .zip.sha256 file records the final archive hash. The original 1.1.0
validation history is retained below.
See [the defect audit and remaining limits](BUG_AUDIT_1.1.1.md).


## 1.1.0 (26923C) cleanup and release verification

Local verification date: **2026-09-23**. The 1.1.0 scope includes both the
29.99 fps CFR/hardening work and the native-reference Apple Log identification
work. This is recorded in [CHANGELOG.md](../CHANGELOG.md) and the README.

- CMake configure and MSVC x64 Release build passed. All **11 core/resource
  CTest groups passed in 5.46 seconds**, including independent color mathematics,
  identification writer/parser, timing, security, parallel processing and actual
  executable version/icon resource inspection.
- Both executables retain Windows FileVersion/ProductVersion **1.1.0**
  (numeric **1.1.0.0**). Generated headers, the CLI and the actual native GUI
  report **1.1.0 (26923C)**. Settings Save/Cancel and FFmpeg-control visibility
  checks passed using an isolated profile. The empty-workspace README screenshot
  was refreshed from this build.
- Downloaded reference footage, A/B videos, render TIFFs, temporary Resolve
  project exports, raw reference metadata, obsolete test profiles/screenshots,
  duplicate installer resources and packaging staging were removed. The initial
  cleanup removed **1,183 files / 1,544,531,754 bytes**; temporary resources from
  this final verification were also cleaned afterward. Source tests, documented
  results and the required development FFmpeg/ffprobe pair remain available.
- The portable ZIP allowlist now contains **16 files**, including `CHANGELOG.md`.
  `tests/package_audit.py` checks the exact allowlist, archive CRC and equality
  between the packaged executable and the tested Release executable.
- This build changes version stamps, documentation and the package contents;
  Apple Log/HLG mathematics and the identification implementation are unchanged.
  The full media/4K120 tests and real Resolve A/B imports below were verified
  on 26923B and were not rerun for this packaging change. Their historical build
  labels are preserved rather than relabeled as new editor tests.

## 1.1.0 (26923B) Apple Log identification verification

Local verification date: **2026-09-23**. This extends the earlier 1.1.0 work
without changing Apple Log/HLG equations or BT.2408 scaling.

- Acquired a public, camera-origin iPhone 15 Pro Max / Blackmagic Camera Apple
  Log ProRes original; preserved its downloaded bytes and recorded provenance,
  size and SHA-256. No reference footage is included in Git or the release ZIP.
- Fixed the analyzer's rejection of the documented four-byte video-description
  terminator; it now exposes the reference's `logs` identifier and complete
  `meta/keys/ilst/data` key/value scope.
- Actual **Resolve Studio 20.3.2.9 on Windows** imported the native reference,
  negative baseline, four isolated metadata candidates and real production output.
  A fresh-project repeat using `tests/resolve_identification.py` also passed.
  Input color space/gamma/LUT was never manually assigned. `logs` alone caused
  **Apple Log** identification; the baseline remained **Rec.2020 (Scene)**.
- Resolve rendered the same automatically identified production frame at
  **Auto**, **Video** and **Full** levels to 16-bit TIFF. Auto and Video hashes
  were identical; Full differed. Evidence, exact placement results for
  customgamma, hashes and limitations are in [the A/B record](APPLE_LOG_IDENTIFICATION.md).
- The standalone C++ writer adds 35 metadata bytes after encoding/rotation.
  Production output and baseline have identical complete `mdat` payload hashes.
  This confirms no pixel/audio changes in the identification step.
- CMake configure and MSVC x64 Release build passed. **16/16 CTest groups passed
  in 242.45 seconds**, including independent scalar math, real codec/numeric
  sampling, metadata conflicts, audio preservation, rotation, no overwrite,
  cancellation/trust checks, writer bounds and sustained 240-frame **4K120**.
- Writer tests cover extended atom sizes, exact preservation across backward
  chunk moves, optional terminators, idempotence, conflicting/missing/duplicate
  declarations, unexpected layout rejection and cancellation without mutation.
- After formatting the new module, the final Release build passed **11/11
  core/resource groups** in **6.85 seconds**. The actual GUI completed both
  standard and creative synthetic conversions. Both resulting MOV files were
  imported into Resolve and automatically identified as Apple Log with no input
  LUT; the standard result remained byte-identical to the successful B candidate.
- The updated portable package contains **15 allowed files**, including the
  new identification evidence document; no FFmpeg, reference footage, generated
  test video, Resolve project, logs or local settings are part of that allowlist.
- Normal conversion validation records structural identification separately from
  editor test evidence. It does not claim that each user's output was opened
  in Resolve. Automatic import remains unverified in Premiere, Final Cut and
  other Resolve versions/editions. Native-camera appearance equivalence is not
  established by an identification test.

The sections below retain earlier verification history; their statements that
no native reference or Resolve test existed describe those earlier checkpoints.

## 1.1.0 (26923B) version and package verification

Local verification date: 2026-09-23. The 1.1.0 feature scope begins with the
29.99 fps CFR/VFR fix and includes the FFmpeg trust/numeric checks, metadata and
chroma handling, MOV analysis, CPU processing and validation work recorded below.
This subsection covers the final version-stamping checks: CMake, generated
resources, the manifest, displayed/logged identifiers and documentation. The
version-stamping step itself does not change conversion behavior.

- CMake configure and MSVC x64 Release build passed.
- All **10 core/resource CTest groups** passed in **4.11 seconds**, including
  actual GUI/CLI executable resource inspection and independent color tests.
- Both executables have numeric FileVersion/ProductVersion **1.1.0.0** and
  property text **1.1.0**. The generated manifest declares **1.1.0.0**.
- CLI output and the actual native English/dark window report
  **1.1.0 (26923B)**. Settings Save/Cancel and the captured window check passed.
  The README screenshot was refreshed from this empty window.
- The portable package is `LogForge-1.1.0-Windows-x64.zip`. Its 14-file allowlist,
  archive CRC and exact EXE match are checked with `tests/package_audit.py`.
- Full generated-media, sustained 4K120 and multi-scenario GUI results from the
  unchanged hardening implementation remain recorded below. They were not rerun
  for this version-only change. No native Apple Log/editor certification is added.

## 1.1.0 feature verification before version stamping

Local verification date: 2026-09-22. These are the 1.1.0 feature tests, performed
before the executable version was updated. The test binary still displayed
1.0.0 (26922A); this identifies the tested artifact, not the release to which the
features belong. These changes are not in the previously published 1.0.0 ZIP.
All media used is programmatically generated; no private camera footage or
native Apple Log sample is needed by these tests.

MSVC x64 Release configure/build and all **15 CTest groups** passed in the final
run (**220.76 seconds**), including the canonical-path execution fix and the
4K120 conversion. The malicious alias-sibling ffprobe regression ran and passed
on this host; it was not skipped. The published
Apple Log and inverse HLG source files are unchanged, as are BT.2408 scaling,
independent scalar references and numerical acceptance thresholds.

| Area | Verified behavior |
| --- | --- |
| Packet cadence | All-packet PTS, duration, adjacency and cumulative phase; constant 29.99, 29.98 and 29.9701 fps accepted despite differing average/nominal hints; VFR, cumulative drift, gaps, duplicate/backwards timestamps rejected with a packet index |
| Executable trust | Production discovery callback encounters a real executable fake FFmpeg without running it; absent approval, changed ffmpeg or ffprobe hashes, approval-snapshot changes and image write attempts are rejected; an unapproved ffprobe beside an approved ffmpeg alias is never executed; a positive canary control proves the fake would write its marker if run |
| External FFmpeg numerics | Independent integer YCbCr matrix/range reference, left/center chroma phase, float transport and post-ProRes code-value checks; capability-only status cannot start conversion |
| Metadata | Format/video/audio whitelist, source-only camera values, conflicting color-tag removal and preservation validation; output side-data conflicts are errors |
| MOV analyzer | Indexed mdta keys, typed ilst/data values, ISO and headerless QuickTime meta, ProRes sample-entry child atoms, malformed boundaries and semantic comparison ignoring byte offsets/key indices |
| Chroma | Explicit left/center conversion; unknown requires a user declaration; unsupported native siting cannot be replaced by an override; output phase qualification and declarations checked |
| Preservation | Drop-frame timecode, two audio streams with exact payload hashes, 180/270-degree rotation, existing audio/rotation/cancellation regressions |
| Publication | Destination created during conversion remains intact; partial output is removed and publication fails instead of overwriting it |
| Parallel color | 4K standard and creative transforms exactly match scalar float results; persistent CPU workers preserve range counters |

The full group list is `hardening_cadence`, `hardening_security`, `hardening_mov`,
`hardening_parallel`, `color_math`, `media_metadata_validation`, `ffmpeg_platform`,
`application_preferences`, `ffmpeg_disk_discovery`, `application_resources`,
`media_pipeline`, `color_signal`, `ffmpeg_discovery_capabilities`,
`publication_no_overwrite` and `sustained_4k120`.

### Numerical and performance observations

- The unchanged generated-media RGB comparison measured mean absolute error
  **0.00043347373910234966** and maximum **0.01275258321150774** on 122,304
  samples (existing limits 0.003 and 0.025, including ProRes/chroma error).
- FFmpeg qualification's float matrix/chroma comparison measured maximum error
  about **5.823e-8** (limit 2e-6). Post-encode patch error was at most
  **0.655728 of one 10-bit code value** (limit 2); the output chroma phase ramp
  had zero measured code error. These are fixture-specific measurements.
- Sustained fixture: **3840 x 2160, 120 fps, 240 frames**. Conversion and tool
  qualification took **61.515 seconds**, about **3.902 encoded frames/second**.
  This is high-frame-rate input support, not real-time 120 fps processing.
- The standard app-owned float buffer was **4,194,304 bytes**. Sampled peak
  LogForge process private memory was **6,283,264 bytes**. This excludes FFmpeg
  child-process allocations; it is not total conversion memory. The creative
  path still retains one planar frame plus small worker tiles.
- Independent post-encode luma sampling at frames 0, 119 and 239 covered 1,536
  patch-center samples; maximum error was **0.655728 code**. The test also
  checked all 240 output packets and the expected output format.

Reports are generated under `build/sustained/sustained-report.json`,
`build/integration/integration-report.json`, `build/signal-test/signal-report.json`
and the isolated test profiles' `logs/*.validation.json`. Validation JSON includes
average/nominal fps, verified cadence, maximum timing errors, both executable
paths/hashes, FFmpeg version/trust/qualification, chroma evidence and the
preserved/removed metadata plan with preservation results.

### Installer and native UI

- The production installer was rerun in the isolated
  `build/installer-hardening` profile. It downloaded **109,728,040 bytes** over
  HTTPS, matched the embedded archive SHA-256, extracted the pair, recorded
  managed trust and passed capability and numerical qualification. A subsequent
  managed rediscovery passed. An unrelated unapproved executable path was
  rejected in that same profile. User AppData preferences were not changed.
- `tests/gui_smoke.py` passed **14 language/theme/DPI/missing-state scenarios**
  and both standard and enabled-creative real GUI conversions. These tests
  explicitly approve the selected test tools in isolated profiles. Interactive
  manual approval/file-picker click-through is not claimed as an automated test.
- The current README screenshot was captured from the English/dark empty GUI;
  it contains no private media. Test artifacts are excluded from source/packages.

### Deliberate limits

- No genuine iPhone Apple Log MOV or Resolve/Premiere/Final Cut validation was
  available. Output remains **nclc 9/2/9** (transfer unspecified). Neither
  `com.apple.rec2020.apple-log` nor `com.apple.proapps.customgamma` is written;
  no Apple encoder identity, `apl0`, invented camera identity or unknown private
  atom is synthesized. The analyzer enables later evidence-based comparison.
- FFmpeg 8.1.2 does not expose a chroma-location field for these ProRes MOVs.
  Output validation therefore requires numerical left-siting qualification and
  LogForge's own `logforge.chroma_location=left` declaration when that native
  field is absent. This is not a standardized Apple/QuickTime siting atom and
  does not establish automatic editor interpretation. Unknown input siting is
  not guessed.
- Nonuniform packet clocks retain the conservative 1.05-tick error budget and
  may be refused when no supported candidate can be established. A fitted
  average is never used to disguise accumulated timestamp corrections.
- Path/hash approval covers both executable images, not every dependency DLL
  in arbitrary shared FFmpeg builds. The pinned build is static. Same-user
  modification of LogForge's approval store is outside this trust boundary.
- The sustained run is 240 full 4K frames, not a multi-hour endurance test.
  Windows 10 boot testing, commercial editors, arbitrary edit lists and unusual
  audio layouts remain unverified. Remote GitHub Actions was not run as part
  of this local task.

## Historical 1.0.0 release record

Local verification date: 2026-09-22. Results below are observations from this development machine, not claims of device or editor certification.

## 1.0.0 application changes

The current release adds a compiled multiresolution icon, unified version resources,
English/Chinese localization, dark/light themes, persisted settings and automatic
local-drive FFmpeg discovery. The standard and optional creative color equations
are unchanged from the prior color audit. Historical sections below retain their
original version labels; they are not new native-camera tests.

- The Release GUI and CLI resources report numeric version **1.0.0.0**, property
  text **1.0.0**, and display **1.0.0 (26922A)**. Resource group 101 contains all
  nine icon sizes.
- New automated tests cover catalog completeness/placeholder consistency,
  English/dark/off defaults, migration of legacy FFmpeg preferences, settings
  round trips, disabled creative-value retention, unrelated-field preservation,
  corrupt/invalid JSON recovery and refusal of invalid adjustment values.
- Disk-discovery fixtures cover multiple roots, empty/missing roots, Unicode and
  mixed-case executable names, trailing root separators, duplicate roots,
  excluded directories, rejected candidates, cancellation and real local-volume
  enumeration. A real FFmpeg/ffprobe pair passes the actual ProRes capability
  test after invalid candidates; cached rediscovery avoids a drive scan. The
  reparse-loop fixture passed in the full test run; it is conditionally skipped
  on restricted Windows tokens that cannot create links.
- Native windows passed **14 UI scenarios**: both languages × both themes ×
  96/144/192 DPI, plus two explicitly injected missing-FFmpeg states. Save changes
  language/theme; Cancel discards a changed draft. Preferences survive reload.
  Valid FFmpeg hides both fallback buttons; the injected missing state shows both.
  Injection tests visibility only, not an assertion that the host lacks FFmpeg.
- Standard and enabled-creative generated-video GUI conversions exercised the
  actual drop handler, asynchronous ffprobe, color transform, encode and automatic
  validation. Controls return to an enabled Convert / disabled Cancel state.
- At high DPI on a small work area, the central content and Settings can scroll.
  DPI cases are produced with the app's explicit layout test override; a physical
  multi-monitor DPI transition and Windows 10 boot test remain unverified.
- The README screenshot is an English/dark empty workspace with no personal
  media or filenames. Private reference media is not needed by any current test.

The final Release build passed all **9 CTest groups**. The ZIP was audited to
contain exactly **14 allowed files**, with an EXE identical to the tested build.
The executable extracted into `dist/portable` then passed all 14 UI scenarios
and both generated-video GUI conversions using the per-user FFmpeg installation.
The full installer download was previously verified; this release reuses and
revalidates the installed tool instead of repeating the 105 MiB download.

Reproduction: build and run CTest as described in the README. Use
`tests/gui_smoke.py` on an interactive Windows desktop, and
`tests/package_audit.py` on the CPack ZIP. Test settings/logs/reports remain in the
ignored build directory; generated media can be removed after verification.

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
| `application_preferences` | Bilingual catalog, default/legacy/corrupt settings, preservation and reload |
| `ffmpeg_disk_discovery` | Local-volume discovery, root handling, exclusions, candidates and cancellation |
| `application_resources` | Actual GUI/CLI VERSIONINFO and nine embedded icon sizes |
| `ffmpeg_discovery_capabilities` | Real ProRes/float check through discovery and verified cache reuse |
| `media_pipeline` | Generated high-precision media converted through actual FFmpeg and production C++ code; real output metadata and decoded pixels checked |
| `color_signal` | Independent integer-YCbCr fixtures and scalar matrix reference; dark/super-white/color patches, standard and creative modes, range warnings and invalid controls |

Integration media: 320 × 180, 60 frames, 30000/1001 fps, 10-bit HLG BT.2020 ProRes HQ, 48 kHz stereo PCM 24-bit audio, timecode `10:20:30:00`, synthetic original Make/Model and creation date.

Content includes gray, shadow and highlight ramps, above-white levels, color ramps, BT.2020 primary/secondary patches and neutrals. The source was generated from planar 32-bit float samples, not an 8-bit picture.

Measured on 122,304 RGB samples away from discontinuity borders:

| Metric | Result | Acceptance |
| --- | --- | --- |
| Mean absolute normalized RGB error against independent equations | 0.00043347373910234966 | < 0.003 |
| Maximum absolute error in measured regions | 0.01275258321150774 | < 0.025 |
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
- Unlabelled 48 kHz, two-channel PCM 16-bit audio, both without rotation and with +90° rotation: no guessed channel-layout tag, original codec/count/rate/bit depth, exact audio SHA-256 match. The unlabelled-audio case reproduced the 0.1.0 failure before the fix.
- A 24 fps sequence with one 1/480-second clock correction: output stays 24 fps with the same frame count. A sequence whose nominal/average rates are close but whose cumulative phase drifts beyond one tick is refused by packet validation.
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

## 0.1.1 real-camera regression

A user-supplied recording identifying its camera as iPhone 13 Pro and its recorder as Blackmagic Cam exposed a 0.1.0 failure: the original had no audio channel-layout declaration, FFmpeg guessed `stereo` during stream copy, and the strict post-conversion check rejected the resulting label difference. Version 0.1.1 disables that guess, including during rotation remuxes, and reports individual audio fields when validation fails.

The full recording was converted using the **0.1.1 portable GUI executable**, exercising the native window, drop handler, asynchronous probe, conversion and validation. Observed results:

| Check | Result |
| --- | --- |
| Input / output raster | 3840 × 2160 |
| Frame count | 521 input, 521 output, all 521 output frames independently decoded |
| Output | ProRes 422 HQ, `yuv422p10le`, BT.2020, Apple Log pixel encoding, `nclc 9/2/9` |
| Output frame rate | 24/1; source packet cadence verified at 24 fps with one 1/480-second correction |
| Video duration | 21.706250 s input, 21.708333 s output; the 2.083 ms difference is the documented one-tick normalization |
| Audio | PCM 16-bit, 48 kHz, two channels; absent layout stays absent; exact duration/start preserved |
| Complete audio payload SHA-256 | Exact input/output match; private-media fingerprint omitted |
| Timecode / GUI / output validator | Passed |

The private recording is not distributed. Its local test derivatives, extracted frames/audio and raw metadata/logs have been deleted from the project after verification. The automated regression suite recreates the relevant properties with synthetic media. This verifies this particular recording, not every device/application combination or editor's Apple Log recognition.

## 0.1.2 color and exposure audit

- Corrected the nominal HLG reference to BT.2408 §2.1/Table 1: 75% HLG = 100% reflecting diffuse white. The older 90% project policy was 0.152003 EV darker. The Apple Log encoding formula is unchanged.
- Reference tests now check HLG 0.3782588830779046 → 18% reflectance → Apple Log 0.4882724585268676, along with reference white, exposure gains, invariant black, and rejection of invalid offsets.
- All four CTest groups passed. The generated-media integration test checks +1 EV against independently calculated output pixels, the saved exposure metadata, invalid CLI values, and all retained audio/timing/cancellation cases.
- The native GUI's +1 EV selection completed a real conversion with rotation and unlabelled PCM audio. ffprobe confirmed `logforge.exposure_ev=1.000000`; the window and controls were visually inspected.
- A 12-frame 3840 × 2160 excerpt of the same private HLG recording was converted at 0 EV and +1 EV. Its first decoded frame was compared against an independent calculation using constants extracted from the retrieved Apple-supplied ACES IDT. The 96,078-sample grid includes actual scene edges and chroma detail.
- The complete recording was also converted through the 0.1.2 portable GUI at the default 0 EV: 3840 × 2160, 24 fps, all 521 output frames independently decoded, original timecode retained, and the entire audio payload hash matched the 0.1.1 real-camera record above. The video duration remained 21.708333 s under the documented timing normalization. This is an end-to-end conversion result, not a native-Apple-Log appearance certification.

| Actual-camera encoded-RGB audit | Mean absolute error | 99th-percentile error |
| --- | --- | --- |
| 0.1.1, previous reference | 0.00107810 | 0.00502431 |
| 0.1.2, standard reference, 0 EV | 0.00110587 | 0.00516923 |
| 0.1.2, standard reference, +1 EV | 0.00125960 | 0.00646609 |

These errors include ProRes compression and chroma resampling. They check that the intended mathematics reached the file, **not** that the image matches a native Apple Log recording. The comparison PNGs were unmanaged, 16-bit previews of encoded RGB with explicit YCbCr matrix/range decoding. They used no viewing LUT, did not change the conversion pipeline, and were deleted after verification. No native Apple Log reference was available, so native-camera appearance and PotPlayer's color handling are not certified.

## 0.1.3 creative rendering and signal audit

The optional grade was explicitly requested after inspection of particular shadow/highlight regions. It is not presented as a correction to Apple's transfer function. The standard path remains unchanged; turning off the adjustment is tested for exact float-output identity.

- Release build passed. All **five CTest groups passed** in one final run (28.22 seconds on this machine).
- Unit tests check reference-gray/black invariance, 30,003 ordered neutral samples across three tone strengths, linear-chroma saturation scaling, disabled identity, invalid controls, finite output and signal counters.
- The new independent test generates **28 flat 10-bit YCbCr patches**, including below-black excursions, deep shadows, gray, white, above-white signals and BT.2020 colors. Neither fixture construction nor reference matrix calculations use `zscale`. Maximum error of the **mean patch-center Y/Cb/Cr codes** against the scalar reference was **0.211127 code** for standard conversion and **0.253078 code** for the enabled default grade; acceptance is below 3 codes. This is a patch-center measurement, not a bound on every pixel at chroma discontinuities.
- GUI tests with the actual portable executable verified enabled adjustment, disabled adjustment with disabled dependent controls, and a range-warning path at +4 EV. The enabled case also preserved a 90-degree rotation and unlabelled PCM audio. The current screenshot shows the synthetic enabled case.
- The user's full 3840 × 2160 recording was converted through the **0.1.3 portable GUI**, at 24 fps and 0 EV, with +3-stop shadow lift / 1-stop highlight compression / 85% saturation. All **521 output frames** independently decoded. Original timecode and audio parameters were preserved; the entire audio payload SHA-256 matched the source (private-media fingerprint omitted). Output video duration was 21.708333 s versus 21.706250 s input under the existing clock policy.
- The complete float-stage signal accounting covered **12,964,147,200 RGB components**. None exceeded normalized Apple Log 1. **Six negative components** fell below Apple's linear-domain floor and were encoded as zero by the published curve; the warning is retained. This is about 0.0000000463% of components, but it is not silently rounded to zero or called lossless. The counters precede final codec quantization.
- The full conversion took approximately **8 minutes 30 seconds** on this development machine, including the GUI workflow. CPU color processing is currently serial; this measurement is not a performance promise for another host.

Two selected frames were independently decoded from the finished MOV. A common explicit BT.2020 matrix and video-level interpretation were used, with no display LUT. Region medians below are normalized encoded RGB component medians, not luminance, nits or recovered dynamic range:

| Selected region | Standard encoded median | Adjusted encoded median | Adjusted frame RGB range |
| --- | --- | --- | --- |
| Dark region at frame 60 (2.5 s) | 0.158287 | 0.225630 | 0.103998–0.669426 |
| Bright region at frame 360 (15 s) | 0.818997 | 0.757170 | 0.144347–0.779497 |

No component in either selected decoded frame reached 0 or 1. The adjusted output's mean absolute error against an independent implementation of the declared grade followed by Apple's formula was 0.001787 and 0.000731, respectively, on an 8-pixel grid including scene edges. The corresponding 99th-percentile errors were 0.011440 and 0.003513. Codec and chroma reconstruction error are included.

Before enabling the creative grade, the two selected standard-output regions were also decoded back into HLG for a fair spatial-detail comparison. Luma correlations with the source were **0.995483** (dark region) and **0.999978** (bright region). This does not prove lossless conversion, but did not support a claim that either region had been uniformly clipped. Very dark detail is visually compressed near the Apple Log toe; lifting it also exposes the source noise. The private test outputs, crops, comparisons and raw metadata were deleted from the project; only anonymized numerical results are retained here. PotPlayer's actual display processing remains unverified.

## Not yet verified

- Other iPhone 13/14 Pro HLG recordings, Apple Camera recordings, and a genuine iPhone 15 Pro Apple Log reference sample. The specific Blackmagic Cam / iPhone 13 Pro recording above has passed.
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
.\build\Release\LogForge-cli.exe --approve-ffmpeg --ffmpeg C:\ffmpeg\bin\ffmpeg.exe
# Approve only a reviewed pair, or install the pinned build through --install-ffmpeg.
# Use only a generated fixture with known left chroma siting for this declaration.
Start-Process .\build\Release\LogForge.exe -ArgumentList '--smoke-test "input.mov" "new-output.mov" --input-chroma-location left' -Wait
```

This path creates a native window, injects a drop into that window, uses the production worker, and emits `new-output.mov.gui-test.json` plus a PNG. It never simulates encoder progress or bypasses output validation.
