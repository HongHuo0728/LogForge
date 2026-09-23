# Changelog

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
