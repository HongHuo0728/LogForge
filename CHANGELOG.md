# Changelog

## 1.3.0 (26929A)

- Replace the nominal-rate phase threshold with an all-packet integer-quantization
  model. Accept verified fractional camera cadence even when average/nominal tags
  differ; retain negative tests for changing speed, VFR, gaps, duplicate/backward
  PTS and invalid durations. Report the candidate, time base, phase in ticks,
  microseconds and frame fractions, and the precise rejection category.
- Accept missing color declarations with explicit BT.2020 / BT.2020 NCL / limited
  / left assumptions and warnings. Reject explicit conflicts by default; a
  per-file GUI confirmation or `--force-bt2020-interpretation` permits a deliberate
  primaries/matrix reinterpretation. HLG and other format requirements still apply.
- Preserve v1.2.1 iPhone mebx/no-timecode admission, Blackmagic audio offset and
  stream copy, and cardinal rotations baked into float32 pixels.
- Preflight queue output paths with deterministic suffixes, skipping existing
  files, and expose the mapping before conversion and in queue reports.
- Separate `validation_passed` from `publication.published`; record publication
  failures and require both for completion. Retain race-safe no-overwrite rename.
- Require complete, bounded CLI numeric parsing before FFmpeg discovery.
- Reject multiple primary video streams and report all deliberately omitted
  attached-picture, subtitle, metadata and data streams; distinguish regenerated
  timecode from copied streams.
- Verify concatenated stream-copy audio payloads with per-stream SHA-256 in
  production, alongside the existing timing and format checks.
- Rank CUDA candidates deterministically and cache successful qualification only
  within the process, bound to GPU UUID, driver, embedded PTX hash, build,
  algorithm and Creative parameters. Failures invalidate the cache; explicit
  qualification always reruns the unchanged scalar comparison.
- Clarify per-buffer/total CPU bridge and CUDA memory report fields.
- Remove filename-only media cleanup and the remux rename/journal gap. Track
  original file identities through normal failure, cancellation and crash recovery.
- Update generated application/resources/manifest versions to 1.3.0 / 1.3.0.0.
  See [release details](docs/RELEASE_1.3.0.md) and [verification](docs/VALIDATION.md).
  Historical v1.2.1 academic results remain unchanged.

## 1.2.1 (26927B)

- Apply cardinal camera orientation to float32 pixels before encoding. Portrait
  output uses portrait raster dimensions and an identity display matrix, avoiding
  sideways playback when a player ignores rotation metadata.
- Limit recording-format admission to ProRes Standard/HQ + HLG. Remove edit-list
  pattern and optional metadata gates, including iPhone auxiliary mebx tracks
  with nonzero origins; timecode is optional.
- Retain source movie timescale, audio stream copy, relative audio timing,
  available timecode and creation metadata. Missing chroma defaults to left.
- Update regressions for the new admission and pixel-orientation contract.
  Color equations, precision, output numerical tolerances, CUDA, FFmpeg discovery,
  UI and queue implementation remain unchanged.
- Correct obsolete CPU-only/no-queue wording in README.
- Update UI/CLI to 1.2.1 (26927B) and Windows versions/manifest to 1.2.1.0.
  See [release details](docs/RELEASE_1.2.1.md) and [verification](docs/VALIDATION.md).

## 1.2.0 (26926A)

- Fix portrait MOV creation-time loss during rotation remux. Compare ISO 8601
  timestamps semantically and preserve documented movie/track/media creation fields.
- Preserve drop-frame timecode by regenerating its track from the source value
  instead of copying a track that overrides explicit timecode metadata.
- Distinguish Standard, Creative and warning completion in core/CLI/GUI.
- Replace the identification writer's fixed-depth assumption with checked ancestor
  traversal. Keep the verified logs identifier and nclc 9/2/9 unchanged.
- Enforce QuickTime input, an explicit safe edit-list policy, supported display
  matrices and chapter preservation; record removed metadata/data tracks.
- Add lightweight first/middle/last encoded pixel checks against the independent
  scalar input reference, alongside existing strict numerical qualification.
- Add cross-process settings/trust locks, ownership-based stale output recovery,
  local retention and writeability checks. Storage/report/remux failures remain fatal.
- Add runtime-qualified NVIDIA CUDA color processing with Auto CPU fallback and
  an explicit forced-CUDA mode. CPU ProRes decode/encode and official color
  equations remain unchanged; no fast math or approximate transfer functions.
- Allocate CPU workers dynamically and overlap Standard decode/transform/encode
  through bounded buffers. Retain complete RGB correspondence in Creative mode.
- Correct pipeline timing and report CPU, GPU, remux, metadata and validation costs.
- Add multi-file queue conversion, per-item failure reports and expanded Details.
- Replace competing main-window layouts with one measured scrollable layout,
  separate status/footer regions and single-owner text rendering.
- Generate and audit the real portable ZIP checksum; upload ZIP and checksum in CI.
- Update display version to 1.2.0 (26926A), FileVersion/ProductVersion and manifest
  to 1.2.0.0. See [release details](docs/RELEASE_1.2.0.md),
  [verification](docs/VALIDATION.md) and [benchmarks](docs/BENCHMARK_1.2.0.md).

## 1.1.1 (26923D)

A discovery and reliability update. The official Apple Log / HLG mathematics,
BT.2408 scaling, float intermediate, numerical tolerances and Apple Log
identification writer remain unchanged.

- Replace automatic all-drive traversal with a three-second Quick discovery budget.
- Stop after a managed/saved installation passes the existing trust and numerical checks; otherwise inspect PATH, App Paths, actual package locations, bounded common folders and the existing Windows Search index.
- Resolve package locations using Windows known folders and real environment variables, independently of `LOGFORGE_DATA_DIR`.
- Keep Deep search as an explicit cancellable GUI/CLI action with real directory counts.
- Isolate filesystem/index queries in a hidden mode of the same executable, supervised by a Job Object.
- Add a candidate list showing paths, origins, paired-file status and rejection reasons; selection still requires path/hash approval.
- Restore download/manual/retry controls after cancellation, timeout, failed verification or refused approval. Reset discovery counters before every run.
- Keep process deadlines active until output pipes close, including after the original process exits. Bound oversized lines and propagate reader exceptions without terminating the application.
- Reject malformed CLI commands before discovery, accept mixed-case `.mov` extensions, validate numeric metadata before integer conversion and use checked disk-space estimates.
- Clean failed trust-save temporary files and allocate Creative worker buffers inside the caller's exception boundary.
- Keep high-DPI keyboard focus visible, correct multiline diagnostic text, update candidate scroll extents on DPI changes, and refresh system colors.
- Add root-cause regressions and a severity-based bug audit. Update both executable resources, manifest, UI and CLI to 1.1.1 / 26923D.

Verification results and limitations are recorded in [VALIDATION](docs/VALIDATION.md)
and [BUG_AUDIT_1.1.1](docs/BUG_AUDIT_1.1.1.md).

## 1.1.0 (26923C)

This release includes all work beginning with the 29.99 fps CFR detection fix,
including Apple Log automatic input identification verified in DaVinci Resolve.
Build 26923C updates the build identifier, release documentation and packaged
changelog, and removes temporary local verification resources. The earlier
26923B test records describe the same 1.1.0 feature scope.

### Apple Log identification

- Added an independent identification writer using the `logs` ProRes sample-entry
  extension observed in a camera-origin iPhone 15 Pro Max Apple Log MOV, with
  the identifier `com.apple.rec2020.apple-log`.
- Verified automatic **Apple Log / Rec.2020** input identification by real A/B
  imports in **DaVinci Resolve Studio 20.3.2.9 on Windows**, using DaVinci YRGB
  Color Managed. No clip input color space, gamma or input LUT was manually set.
- Confirmed that Resolve's Auto data levels match Video levels for the tested
  output. The existing `nclc` 9/2/9 declaration and Video-range encoding remain.
- Kept the writer separate from pixel processing and preserved the complete
  encoded video/audio payload. No Apple encoder, camera model, `apl0` tag or
  unverified private atom is invented.
- Extended the reference analyzer and writer tests for native ProRes sample-entry
  children and the documented four-byte video-description terminator.
- Other Resolve versions/editions, Premiere and Final Cut remain unverified.
  See the [native-reference and editor evidence](docs/APPLE_LOG_IDENTIFICATION.md).

### Timing, FFmpeg safety and validation

- Accept fixed-cadence 29.99, 29.98 and 29.9701 fps inputs even when average and
  nominal rate tags differ. Check every video packet's PTS, duration, interval
  and cumulative phase; reject true VFR, timestamp gaps, duplicate/backwards
  PTS and sustained drift with a packet-specific reason.
- Discover FFmpeg paths without executing unknown programs. Require a verified
  managed download or explicit approval bound to the canonical paths and SHA-256
  hashes of both FFmpeg and ffprobe; changed binaries require approval again.
- Qualify approved tools with numerical matrix/range, chroma-phase and ProRes
  encode/decode reference signals. Only Verified FFmpeg may transcode.
- Copy metadata through explicit format/video/audio whitelists, preserve safe
  source fields and record removed conflicting HLG/HDR/Dolby/PQ/custom-gamma tags.
- Apply declared left/center input chroma siting and validate output siting.
  Missing input siting requires an explicit user declaration.
- Parse `meta/keys/ilst/mdta/data` into semantic key/value records and compare
  reference MOVs without treating byte offsets or file sizes as metadata changes.
- Include verified cadence, timing errors, FFmpeg identity/trust, chroma evidence,
  metadata decisions and Apple Log identification checks in validation reports.

### CPU processing and regression coverage

- Stream the standard transform through persistent CPU workers with a bounded
  4 MiB float buffer; process creative adjustments in parallel RGB tiles.
- Compare optimized processing against the independent scalar numerical reference.
- Add regressions for CFR/VFR timing, executable non-execution/hash changes,
  metadata conflicts, chroma siting, drop-frame timecode, multiple audio streams,
  rotation, post-encode numerical sampling, identification atoms and a sustained
  240-frame 4K120 conversion. Sustained testing does not imply real-time throughput.
- Keep the published Apple Log math, inverse HLG OETF, BT.2408 scaling, 32-bit
  float intermediate and BT.2020 ProRes 422 HQ 10-bit Video-range output unchanged.
- Keep Creative adjustments off by default and separate from standard conversion,
  with audio stream copy, cancellation/process supervision, no overwrite and
  partial-file validation before final publication.

Downloaded reference footage, generated videos, render images and temporary
Resolve projects were removed after verification. Source tests, documented
provenance/hashes and verification conclusions remain available. No test media,
FFmpeg binaries or personal settings are included in the portable ZIP.

## 1.0.0 (26922A)

Initial public release with the native Windows interface, standard HLG-to-Apple
Log conversion, ProRes 422 HQ output, FFmpeg management, English/Simplified
Chinese, dark/light themes and optional Creative adjustments.
