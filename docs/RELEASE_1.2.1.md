# LogForge 1.2.1 (26927B)

This maintenance release fixes two MOV input compatibility problems. The Apple
Log/HLG equations, BT.2408 scaling, numerical tolerances and other features are
unchanged.

## Portrait playback

- Apply standard 90/180/270-degree orientation directly to float32 RGB pixels
  before Apple Log encoding, with no 8-bit intermediate or additional resize.
- A rotated 3840x2160 camera raster produces upright 2160x3840 pixels and an
  identity display matrix. Playback does not depend on recognizing a rotation tag.
- Retain creation fields and optional timecode. Non-cardinal matrices remain
  metadata rather than being guessed from an angle.

## Camera input compatibility

- Recording-format admission now checks ProRes Standard/HQ and HLG. Auxiliary
  metadata, timecode availability and edit-list patterns are not admission gates.
- In particular, iPhone mebx tracks with a nonzero media origin are omitted without
  blocking conversion. FFmpeg interprets the actual video/audio playback timeline.
- Retain the movie timescale and audio stream-copy payload, including Blackmagic
  Cam's leading audio offset. Missing chroma defaults to left, while explicit
  source siting/overrides remain available. The color interpretation remains
  BT.2020 NCL HLG, with video range unless full range is explicitly declared.
- Memory/raster bounds, packet timing needed by the fixed-rate pipeline, decoder
  errors, output correctness, cancellation and no-overwrite protections remain.
  This is not a new arbitrary-VFR or non-MOV conversion pipeline.

## Documentation and version

- Correct README's obsolete CPU-only/no-queue limitation. Auto/CPU/NVIDIA CUDA
  color processing and the serial multi-file queue remain available. ProRes
  decoding and encoding still use the CPU.
- GUI and CLI display `1.2.1 (26927B)`. FileVersion, ProductVersion and manifest
  use `1.2.1.0`.
- Portable files: `LogForge-1.2.1-Windows-x64.zip` and
  `LogForge-1.2.1-Windows-x64.zip.sha256`. FFmpeg is not bundled.

## Verification scope

See [VALIDATION.md](VALIDATION.md) for this build's minimum regression results.
Generated regressions cover rotated pixels, matrix identity, audio offsets and metadata.
Local camera originals were also exercised as recorded in VALIDATION; no media is included in the repository or package. Historical 1.2.0 GUI, CUDA performance and editor
import evidence remains historical; those workflows are not newly certified by
this patch release.
