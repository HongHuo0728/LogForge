# Apple Log identification: native reference and Resolve A/B evidence

Verified locally on **2026-09-23**, using **DaVinci Resolve Studio 20.3.2.9
on Windows x64** and its installed, official scripting SDK. This document
records container identification, not an assertion that converted HLG recovers
a native camera's scene rendering or sensor information.

This identification work is part of **LogForge 1.1.0**. The editor experiments
used build 26923B; the current build is 26923C, with the same identification
implementation. See the [1.1.0 changelog](../CHANGELOG.md).

## Reference provenance

The camera-origin reference is **Studio Apple LOG ProRes 422 BMCam App 4K.mov**,
published with Oleg Nikitin's [Apple Log workflow tutorial](https://www.youtube.com/watch?v=K5F2uyMxL3Y).
The tutorial's original description links to this [sample folder](https://drive.google.com/drive/folders/1FOziyLgHfKimVunhcF5Ws5ZCcvkZcvEp).
The [specific original download](https://drive.google.com/file/d/1iS5rYcgOyOKQfgFq6uHmx7oL51vUpeC_/view)
was retrieved directly, not from Google's or YouTube's playback transcodes.

- Download length: **961,760,713 bytes**.
- SHA-256: `204a47fba09ff1c69e47936caf6bf1537f7d770474837a057deabedc241d2d42`.
- The publisher identifies iPhone 15 Pro Max and Blackmagic Camera. Internal
  metadata records `Apple iPhone16,2 24mm`, `Blackmagic Cam 1.0.10003` and
  2023-10-02 capture time. This is an iPhone native Apple Log recording through
  **Blackmagic Camera**, not a claimed Apple Camera app recording.
- Original `apcn` ProRes 422, 3840 × 2160, 650 frames, AAC audio and a timecode
  track. QuickTime/Core Media handlers, camera metadata, frame/container color
  fields and file layout are consistent with camera acquisition.
- Its bytes were never remuxed, re-encoded or patched during this test. Uploader
  provenance is public evidence, not a cryptographically authenticated capture
  chain. No camera original, image, GPS data or publisher footage is distributed
  with LogForge. The downloaded reference, A/B media, render images, raw metadata
  and temporary Resolve project exports were deleted after verification on
  2026-09-23. The source links, hashes, relevant atom bytes and sanitized results
  in this document remain as the verification record. Repeating the editor
  experiment requires obtaining the original again and regenerating test media.

## Observed bytes

The analyzer resolves two distinct Apple Log signals in the original:

1. `/moov/trak/mdia/minf/stbl/stsd/apcn/logs`, a **35-byte atom**. Its payload is
   exactly the **27 UTF-8 bytes** `com.apple.rec2020.apple-log`, with no FullBox
   version/flags and no NUL suffix.
2. `/moov/meta` (QuickTime headerless), `hdlr=mdta`, `keys` entry 29 in namespace
   `mdta`, key `com.apple.proapps.customgamma`. Its `ilst` item 29 contains a
   `data` value of type **1 (UTF-8)**, locale 0, with the same identifier.

The original's `colr` is also **nclc 9/2/9**. The unspecified H.273 transfer is
therefore not, on its own, an indication that an Apple Log file is mislabeled.
Apple's CoreMedia/VideoToolbox APIs establish the identifier; the **actual MOV
bytes and import tests** establish the sample-entry representation used here.
No unobserved API-to-file serialization has been invented.

The original video description has a four-zero-byte terminator after its
extensions. This is [explicitly permitted by QTFF](https://developer.apple.com/documentation/quicktime-file-format/video_sample_description).
The analyzer previously rejected it as a truncated atom. It now accepts exactly
that terminator only inside video sample descriptions; malformed lengths and
nonzero tails still fail. The `logs` identifier, raw payload, resolved metadata
scope and terminator offsets are exposed separately. Semantic comparisons
ignore file offsets, length, key numbering and the optional terminator.

## Controlled Resolve imports

A generated HLG ProRes clip was converted through the existing LogForge pixel
pipeline to form the baseline. Only MOV metadata was altered for each candidate.
All candidate `mdat` payloads and the independently regenerated production output
have SHA-256:

`a5263767aef1970556d54b963ee73723662b2a671d07d08208d917ea8c55b4e5`

Thus the video and audio compressed bytes are identical throughout this A/B
test. Original reference pixels were never substituted into the baseline.

Resolve ran in **DaVinci YRGB Color Managed**, automatic color management enabled.
The fallback input was **Rec.709 Gamma 2.4**, not Apple Log. The SDK imported
each distinct file and read `GetClipProperty()`; **no input color space, gamma,
IDT or input LUT was manually assigned**.

| File / change from baseline | Detected Input Color Space | IDT | Result |
| --- | --- | --- | --- |
| Original iPhone reference | Apple Log | Apple Log | Positive control |
| Baseline LogForge, nclc 9/2/9 only | Rec.2020 (Scene) | Empty | Negative control |
| A: customgamma in existing FFmpeg `/moov/udta/meta` | Rec.2020 (Scene) | Empty | Not sufficient at this scope |
| A2: customgamma at native `/moov/meta` scope | Apple Log | Apple Log | Sufficient at this scope |
| B: only `logs` in the ProRes sample entry | Apple Log | Apple Log | Sufficient independently |
| AB: sample-entry `logs` plus A | Apple Log | Apple Log | No extra field needed |
| Actual HLG → LogForge output using the C++ writer | Apple Log | Apple Log | Production path verified |

Resolve's combined **Apple Log** input preset denotes Apple Log with Rec.2020
primaries. It is not Apple Log 2 / Apple Wide Gamut. The baseline/production
files have no invented camera manufacturer/model, and retain FFmpeg/LogForge
encoder identification. The result does not depend on impersonating an iPhone.

## Video-level control

The production output was placed on a Resolve timeline, with its automatically
detected Apple Log input. The same frame was rendered to **16-bit RGB TIFF**
with three Clip Attributes data-level settings:

- **Auto** and **Video** render hashes are identical:
  `18d64505aca6c540faf79305e2158bd349e8e426961b6c73fc1a447939821688`.
- **Full** differs:
  `c5910467ab97091c42e7ae69eefc86ec9eb61d01548e4735badb208fb83e7a62`.

The import used Auto throughout; Video/Full were explicitly changed only for
this separate range control and restored to Auto afterward. This confirms
automatic video-range interpretation for the tested ProRes HQ file, rather than
assuming the literal `Data Level=Auto` property means video range.

## Production contract

`AppleLogIdentificationWriter` writes **only the independently sufficient
sample-entry `logs` atom** after encoding and any rotation remux, before output
validation and final rename. No customgamma, camera identity, Apple encoder ID
(`apl0`) or generic H.273 Log ID is added.

The writer requires one ProRes HQ sample entry, one nclc 9/2/9 color atom and a
trailing moov. It refuses conflicting/duplicate identification, unexpected
hierarchies, fragmented files and color/HDR conflicts. It does not offer a
retag-HLG command. Only the application's encoded partial output is eligible.

The new atom adds **35 bytes**. Ancestor sizes are updated, with support for
extended lengths; a bounded **1 MiB** buffer shifts only the moov tail. `mdat`,
packet offsets, audio, timecode, rotation and safe camera metadata are unchanged.
A deny-write/delete handle prevents concurrent mutation. Cancellation/errors
leave the partial under the existing cleanup guard; final files are published
only after strict re-probe, atom and cadence validation, without overwriting.

Validation JSON separates `apple_log_identification.passed` (this file's structural
validation) from the recorded version-specific compatibility experiment. It
sets `this_file_imported_in_editor=false`: normal conversions do not secretly
launch Resolve or claim a fresh editor import for every output.

## Reproduce and limits

`tests/resolve_identification.py` is an opt-in test using the installed Resolve
SDK and real imports. Supply separate `--reference`, `--baseline`, `--production`,
`--resolve-library` (fusionscript.dll) and `--report` paths. Optional `--candidate`
arguments use `expected color space=path`. Run in an idle empty/test Resolve
session. It creates a fresh managed project, compares all results and removes
only its own disposable project; it never sets a clip color space or input LUT.
Ordinary CI runs synthetic atom, writer and codec tests, **not a mock certification
of Resolve**. Public sample downloads are not CI dependencies.

Verified scope is **Resolve Studio 20.3.2.9 on Windows, managed Apple Log import
and the stated range control**. Unmanaged DaVinci YRGB does not automatically
apply a display transform merely because a clip has identification. Older or
other Resolve editions, Premiere and Final Cut still require separate testing.
No claim is made of Apple Camera app atom-for-atom equality, all commercial
editors, native exposure/ISP equivalence, or restored sensor dynamic range.
