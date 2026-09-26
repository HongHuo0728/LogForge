# LogForge 1.2.0 (26926A)

This release addresses portrait MOV preservation, media and storage robustness,
processing performance, queued conversion and the main-window layout. It keeps
the independently tested Apple Log/HLG equations and BT.2408 reference scaling.

## Portrait MOV and metadata

- Compare creation timestamps as instants, including equivalent fractional-second
  and timezone representations. Different instants still fail validation.
- Preserve movie and individual video/audio/timecode creation fields through
  rotation remux. FFmpeg assigns global creation time to track headers; a bounded
  writer restores the source's documented `mvhd`, `tkhd` and `mdhd` creation fields
  without resizing atoms or modifying media data.
- Regenerate the timecode track from its source timecode when rotating. This avoids
  FFmpeg's precedence rule that ignores explicit timecode when an old track is copied.
- Keep source creationdate, genuine camera identity and other approved metadata.
  No camera or encoder identity is invented.
- Traverse the actual ProRes sample-entry ancestor chain for identification patches.
  Preserve `logs=com.apple.rec2020.apple-log`, `nclc 9/2/9` and Video levels.

## Media and publication safeguards

- Accept QuickTime MOV explicitly. Reject fragmented MOV, unsafe edit lists,
  mirrored/scaled/translated/perspective display matrices and unsupported containers.
- Allow unit-rate zero-origin duration declarations and packet-confirmed AAC
  priming. Reject trims, empty edits, repeats and speed changes before encoding.
- Copy and validate chapters; record removed nonessential metadata/data tracks.
- Sample first, middle and last encoded frames against independent CPU reference
  patches, in addition to existing strict color-signal and metadata tests.
- Preflight report/log directory access. Report write, encoding and remux-space
  failures prevent final publication. Existing destinations remain protected.
- Synchronize settings and FFmpeg trust read/modify/write across processes.
- Recover only temporary outputs recorded in an ownership journal whose file IDs
  still match a dead owner. Similar user filenames and older unjournalled files
  are not deleted.
- Retain local logs/reports for 30 days and marked obsolete FFmpeg staging/replaced
  folders for seven days. Active files, unmarked folders and reparse points are
  excluded from cleanup.

## CPU and NVIDIA CUDA processing

- Add Auto / CPU / NVIDIA RTX CUDA in Settings and `--backend auto|cpu|cuda` in CLI.
- Qualify a runtime-detected CUDA device against the independent CPU scalar reference
  before use. No GPU-model whitelist or CUDA Toolkit installation is required to run.
- Keep ProRes decoding and ProRes HQ encoding on the CPU. CUDA accelerates only the
  middle color transform. Auto reports and recovers from GPU initialization/runtime
  errors; forced CUDA returns an explicit error.
- Use float32 transport and precise double expressions in CUDA, with pinned host
  buffers, asynchronous copies, a nonblocking stream and kernel/copy timing.
  No fast math, FP16, Tensor Cores, LUT substitution or approximate log/exp is used.
- Allocate CPU workers according to available logical processors and backend.
  Standard conversion overlaps decode, transform and encode through three bounded
  chunks. Creative conversion retains one full planar RGB frame so channel
  correspondence and numerical behavior remain exact.
- Separate pipeline wall time, color time, pipe waits, decoder/encoder CPU time,
  remux, creation metadata, identification and validation. Concurrent stage times
  are not additive. See [benchmark definitions and results](BENCHMARK_1.2.0.md).

## Interface and queue

- Use one measured layout for input, output, status and footer regions. Status
  text belongs to one control only. Short windows scroll instead of clipping text.
- Recalculate after resize, DPI, language, theme and state changes, and erase old
  child rectangles during relocation.
- Open/drop multiple files and convert them serially into the selected directory.
  A failed item gets its own report and does not stop later items; Cancel stops
  the remaining queue.
- Expand Details with source/verified cadence, mode, exposure, backend/GPU,
  metadata, audio, rotation, identification and timings.
- Distinguish Standard, Creative and completed-with-warning status.

## Release contract

The portable package contains the GUI executable, documentation and licenses.
It does not contain FFmpeg, CUDA DLLs, test footage, settings or logs. Packaging
generates `LogForge-1.2.0-Windows-x64.zip.sha256` from the actual ZIP. The audit
checks archive integrity, an exact file allowlist, EXE identity and ZIP checksum.
Windows FileVersion/ProductVersion are `1.2.0.0`; the application displays
`1.2.0 (26926A)`.

## Verification scope

The final local verification record is maintained in [VALIDATION.md](VALIDATION.md):
22/22 CTests, 26 native GUI scenarios, 960 layout combinations, 20 benchmark cases
and actual CUDA qualification passed. A fresh Resolve Studio 20.3.2.9 import
automatically identified both new Standard and Creative outputs as Apple Log,
without manual clip color-space or LUT assignment. Historical 1.1.0 native A/B
evidence remains separately labeled. Other GPU families/drivers and Windows
machines require their own runtime qualification; one laptop benchmark is not
a universal speed claim. Creative retains a complete float32 frame. Hardware
exhaustion paths use fault injection, and other editors remain untested.
