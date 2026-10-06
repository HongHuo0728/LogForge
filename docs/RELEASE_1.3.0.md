# LogForge 1.3.0 — Windows 26929A / iOS 26106A

## iOS addition to the existing 1.3.0 release

The main LogForge repository now contains the native Swift / SwiftUI / Metal iOS
application under `LogForgeMac/`. Its user-visible version is **1.3.0 (26106A)**;
the numeric Apple bundle build remains separate. Windows stays **1.3.0 (26929A)**.

iOS 26+ offers CPU/Metal color processing, automatic system/software ProRes
decode and encode fallback, ProRes 422/HQ export, Photos/files/folder import,
queue retry, Liquid Glass UI/icon, haptics and five languages. Its input contract
requires ProRes 422/HQ BT.2020 HLG MOV; valid variable frame timing is retained.
Audio, rotation, stored frame timing and Apple Log metadata are independently
checked. The Windows timing and input policy in the following sections remains
Windows-specific.

The iOS release was built from commit
[`4b9b7771be97663cd3a3cb2dc3074a4cd1675fa0`](https://github.com/HongHuo0728/LogForge/commit/4b9b7771be97663cd3a3cb2dc3074a4cd1675fa0).
[Main-repository iOS build #2](https://github.com/HongHuo0728/LogForge/actions/runs/37495072326)
passed all 5 packaging checks and 28 simulator tests without failures or skips,
and successfully archived and packaged the iPhone application. Device performance
and editor import remain separate checks. No new Windows media/editor validation
is claimed.

The existing **v1.3.0** tag remains at the September 29 Windows commit
`913e4b9417fa0f32b88629c39062b54589d9dd21`. GitHub's automatically generated
**Source code (zip)** and **Source code (tar.gz)** assets follow that tag and
**do not contain the iOS project**. For the source corresponding to the iOS
26106A assets, use the pinned commit above or download its
[source ZIP](https://github.com/HongHuo0728/LogForge/archive/4b9b7771be97663cd3a3cb2dc3074a4cd1675fa0.zip)
or [source tar.gz](https://github.com/HongHuo0728/LogForge/archive/4b9b7771be97663cd3a3cb2dc3074a4cd1675fa0.tar.gz).
The native project is in `LogForgeMac/`. The original release tag is not moved.

Run **iOS build and tests** in the main repository. Tests must pass before archive
and packaging. Release assets are `LogForge-1.3.0-iOS-26106A-unsigned.ipa`, its
SHA-256 file, the matching versioned RelinkKit and its checksum. Supply your own
valid Apple signature/profile to install. Preserve corresponding source and
relink material when distributing a signed build. App Store/TestFlight and
automatic Release publication are not configured.

These are additions to the existing **v1.3.0** release, not a new Windows version
or a replacement for its original verification history. See the
[main-repository iOS release steps](https://github.com/HongHuo0728/LogForge/blob/main/LogForgeMac/docs/RELEASE_1.3.0_IOS.md).

## Windows release

LogForge 1.3.0 fixes fractional camera timing, makes input color interpretation
explicit, and strengthens queue naming, publication reports and media recovery.
It retains the verified color equations and the v1.2.1 camera compatibility policy.
Windows FileVersion/ProductVersion and manifest are **1.3.0.0**.

## Compatibility and timing

Every video packet still participates in verification. A nominal FPS tag is now
a candidate clock, not an authority: nonuniform integer timestamps must share
one fixed rational quantization cell, including the last packet duration.
Local duration/interval checks and complete-sequence phase checks reject damaged
PTS, gaps and changing-speed examples without raising the former 1.05-tick limit.
See [the mathematical contract and identifiability limits](CADENCE_1.3.0.md).

The reported 309-frame 59.94 / 59.970888 case is reproduced with generated packet
timings, including the old 1.06-tick error at packet 53. This fixture passes; it
is not a claim that the original failing recording has been examined.

Missing primaries, matrix, range and chroma declarations use explicit defaults
with warnings. Explicit conflicting declarations fail before decoding. A single
file can be deliberately reinterpreted through the GUI confirmation or
`--force-bt2020-interpretation` in the CLI. This authorizes BT.2020 interpretation,
not a gamut conversion, and cannot bypass the HLG transfer requirement or other
format checks. GUI queue jobs do not inherit a first-file confirmation; a CLI
batch override is explicit for that command. [Full input contract](INPUT_CONTRACT_1.3.0.md).

iPhone auxiliary mebx tracks, nonzero auxiliary origins and absent timecode
remain accepted. Blackmagic audio start offset, PCM stream copy and source
creation/timecode metadata remain supported. Cardinal rotations continue to be
baked into float32 pixels, swapping the raster for 90/270 degrees and leaving
identity output orientation. No strict auxiliary edit-list admission gate returns.

## Confirmed causes, fixes and regressions

| Issue | Cause | Fix and regression evidence |
| --- | --- | --- |
| Fractional CFR falsely rejected | Absolute phase measured against a nominal metadata clock with a fixed 1.05-tick cutoff | Full-sequence rational quantization model; reported 59.94 reconstruction, floor/ceil variants, 29.99/29.98/29.9701 positives, accelerated/piecewise/clustered drift negatives |
| Explicit BT.709 accepted as BT.2020 | Admission ignored labels while the decoder always used BT.2020 NCL | Declared/assumed/overridden contract; actual MOV conversions cover missing fields, consistent labels, rejected conflicts and explicit override |
| `media_pipeline` policy mismatch | Its BT.709 rejection expected behavior admission no longer enforced | Restore meaningful conflict rejection and expand tests; retain the old case |
| Same-name queue items collide | Each item independently used the unsuffixed input stem | Freeze a deterministic complete plan before encoding; same-stem inputs, case variants and existing targets tested |
| Validated output falsely implies completion | Validation ended before the no-overwrite publication rename | Separate validation/publication/completed; another process creates the destination and the job records publication failure without overwrite |
| Partially parsed CLI integers | `stoll` did not verify the entire token | Full-token bounded parsing; empty, garbage, overflow, negative and zero cases fail before discovery |
| Omitted streams absent from reports | All video streams were treated as preserved although only v:0 was mapped | Reject multiple main videos; list omitted attached pictures/data/subtitles and regenerated timecode separately |
| Audio format equality mistaken for byte identity | Production compared format/timing but only tests hashed payload | Per-stream SHA-256 over copied packet content; deliberate PCM byte corruption cannot publish an output |
| GPU order depends on enumeration | First qualifying device won | Deterministic VRAM/compute/UUID ranking, candidate reports and order-independent unit test |
| Repeated qualification cost | Every transformer repeated the same scalar comparison | Bounded process-local identity-bound cache; explicit qualification and failed-transform retry rerun it |
| Misleading memory field | A single slot's size was named as general float-buffer usage | Explicit per-slot, count, total, Creative frame and GPU allocation fields; legacy field marked deprecated |
| Cleanup could delete a replaced file | A filename-only guard bypassed the existing ownership journal | All media cleanup requires matching identity; substituted files survive both failure cleanup and startup recovery |
| Remux crash gap | Temporary rename preceded journal identity update | Keep both original paths tracked and switch the active path; ten injected crash stages cover reservation, transform, remux, patch, reports and publication |

One historical drift fixture, `20*i-floor(i/64)`, is exactly a fixed rational
cadence (`1279/64`). It remains as a positive test. The negative test now uses
genuine acceleration, with additional changing-speed negatives. The contract
explains this correction; no numeric color tolerance or validator was weakened.

## Report and backend changes

- `input_interpretation`: declarations, assumptions, overrides, effective values,
  origins, conflicts and warnings. Cadence adds clock source, classification,
  time base, interval/duration/phase error and human-readable units.
- `validation_passed` retains the media verdict. `publication` records attempted,
  published, final path, status and error. `completed` requires both validation
  and publication. Legacy `passed` continues to mean media validation only.
- `metadata.audio_copy` distinguishes `audio_stream_copy_declared` from
  `audio_payload_verified`. Hashes cover concatenated copied packet bytes,
  not packet boundaries or timestamps; existing timing checks remain separate.
- Buffer fields identify one bridge slot, slot count, total bridge bytes,
  Creative frame bytes and CUDA pinned/device bytes. These are not whole-process
  memory measurements; the retained `float_buffer_bytes` is deprecated.
- CUDA cache identity includes GPU UUID, driver API/file version, embedded PTX
  SHA-256, application version/build, algorithm and Creative parameters. Successful
  entries live only in the current process, at most 16. Failures evict an entry.
  CPU/CUDA equations, qualification cases and the `2e-6` tolerance are unchanged.

## Verification and remaining limits

See [the current verification record](VALIDATION.md) and
[machine-readable results](verification/1.3.0.json) for actual executed checks.
Tests use generated media. The bilingual academic papers in `AcademicPapers` have been revised for 1.3.0;
they retain explicitly labeled historical measurements and cite this release verification record.

- Timestamp quantization limits what can be distinguished physically. A different
  fixed clock producing the same packet sequence cannot be identified as VFR;
  deviations below the timestamp resolution are not independently observable.
- The media rename and report replacement are two different filesystem
  transactions. A crash between them may leave a valid final movie and a pending
  report. A final report-write failure preserves that valid movie and returns an
  error. Recovery never deletes a published destination to roll back the report.
- A crash before durable journalling, an unknown/replaced identity or inaccessible
  ownership evidence can leave a temporary file for manual review. A filename
  alone never authorizes deletion.
- Audio hashing adds two sequential audio reads and FFmpeg launches. Memory is
  bounded by streaming; it is not a whole-file in-memory comparison.
- Physical multi-GPU selection is not locally exercised. Ranking is unit-tested
  and the installed GPU is qualified. There is no new GPU selector or CUDA
  upload/kernel/download overlap redesign; Creative retains a full planar frame.
- GUI per-file color confirmation is not a persistent preference. For a batch
  with explicit conflicts, use an explicit CLI override or handle the file
  individually after reviewing its declaration.
- This version does not introduce a new Resolve/Premiere/FCP import verification.
  The identification writer and native-reference fields are unchanged; earlier
  Resolve evidence remains historical, not a claim about all editors.

## Portable release

`LogForge-1.3.0-Windows-x64.zip` contains the GUI EXE, README/changelog, MIT and
third-party notices, technical/release/verification documents and the interface
image. It excludes FFmpeg, CUDA runtime DLLs, the developer CLI, tests, settings,
logs and source/output footage. CPack creates the actual `.zip.sha256` sidecar.
The package audit checks an explicit allowlist, CRCs, checksum content and equality
between the packaged EXE and the tested Release executable.

Run `LogForge.exe` after extracting the archive. Windows 10 22H2 / Windows 11 x64;
static MSVC runtime. ProRes decode and ProRes HQ encode remain CPU operations;
CUDA accelerates only intermediate color processing. The executable is unsigned.

LogForge reinterprets the available BT.2020 HLG signal in scene-linear terms and
re-encodes it as Apple Log. It does not restore sensor data, replicate native
Apple Log capture, promise lossless ProRes encoding or claim universal editor
compatibility.

## Changed implementation and test files

- Build/package: `CMakeLists.txt`, `.github/workflows/build-windows.yml`,
  `tests/package_audit.py`. Windows resources and manifest take the new values
  from existing CMake templates; no duplicate hardcoded version was introduced.
- Cadence/input/stream reporting: `src/Cadence.cpp`, `src/Media.cpp`,
  `src/MediaSafety.cpp`, `include/logforge/Media.h`, `include/logforge/Messages.inc`.
- CLI/GUI/queue: `src/Cli.cpp`, `src/MainWindow.cpp`, `src/Queue.cpp`,
  `include/logforge/Queue.h`.
- Publication/audio/recovery: `src/Transcode.cpp`, `include/logforge/Transcode.h`,
  new `src/AudioPayload.cpp` and `include/logforge/AudioPayload.h`. Existing
  `StorageSafety.cpp` identity checks are reused unchanged.
- CUDA engineering: `src/color/CudaTransformer.cpp` and
  `include/logforge/CudaTransformer.h`. Scalar formulas, CUDA kernel source,
  embedded PTX, FloatTransformer, PixelSanity, FFmpeg discovery and trust code
  remain unchanged.
- New regressions: `tests/cadence_v130.cpp`, `tests/v130.cpp`.
- Extended existing regressions: `tests/tests.cpp`, `tests/integration.py`,
  `tests/compatibility_v121.py`, `tests/hardening.cpp`, `tests/process_fixture.cpp`,
  `tests/reliability.cpp`, `tests/application.cpp`.
- Current documentation: `README.md`, `CHANGELOG.md`, `docs/ARCHITECTURE.md`,
  `docs/TECHNICAL_REFERENCES.md`, `docs/VALIDATION.md`, `docs/images/LogForge.png`;
  new `docs/CADENCE_1.3.0.md`, `docs/INPUT_CONTRACT_1.3.0.md`, this release note and
  `docs/verification/1.3.0.json`. Academic papers are revised separately; historical release records are retained.
