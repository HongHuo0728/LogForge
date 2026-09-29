# Input interpretation contract — LogForge 1.3.0

LogForge converts QuickTime MOV containing one primary ProRes 422 Standard/HQ
HLG video stream. Its color decoder interprets BT.2020 primaries and the BT.2020
non-constant-luminance matrix. Admission must agree with that interpretation.

| Source declaration | Policy |
| --- | --- |
| BT.2020 primaries / BT.2020 NCL matrix | Accept the declaration. |
| Missing, `unknown`, or `unspecified` primaries / matrix | Accept; assume BT.2020 / BT.2020 NCL and report warnings. |
| Explicit conflicting primaries / matrix, including BT.709 | Reject before decoding unless the user explicitly enables forced BT.2020 interpretation. |
| Explicit HLG (`arib-std-b67`) | Required. Missing transfer, PQ, and other transfers are rejected. |
| `tv` / `pc` range | Interpret as declared limited / full range. |
| Missing, `unknown`, or `unspecified` range | Assume limited range and report a warning. |
| `left` / `center` chroma siting | Interpret as declared. |
| Missing, `unknown`, or `unspecified` chroma siting | Assume left and report a warning, or use an explicit `--input-chroma-location left\|center` choice. |
| Explicit unsupported chroma siting or range | Reject. The chroma option cannot replace a source declaration. |

`--force-bt2020-interpretation` (or explicit GUI confirmation) authorizes the
existing decoder to interpret conflicting primaries/matrix as BT.2020 / BT.2020
NCL. This is a declared interpretation override, not a gamut conversion. It is
never automatic and emits a warning. It cannot override PQ/non-HLG transfer,
unsupported chroma siting, unsupported range, codec/profile, or container.

CLI probe, GUI Details, and validation reports expose the effective interpretation
and its origin. `input_interpretation` contains `declared`, `assumed`, `overridden`,
`effective`, `origin`, `conflicting_fields`, and `warnings`. Empty metadata is an
assumption, not evidence that the original camera used a particular setting.

The input container must be identified by FFprobe's MOV demuxer. An explicit
non-QuickTime major brand is rejected early. Legacy MOV without a major-brand
declaration remains allowed and is subsequently parsed as QuickTime atoms.
Multiple primary video streams are rejected instead of silently discarding a
second picture. An attached picture must not be the selected first video stream.

This policy does not restore strict camera-metadata gates: missing timecode,
camera metadata tracks (including `mebx` with nonzero media origin), and legal
camera edit lists remain compatible. Existing cardinal rotations are baked into
float32 pixels, and audio is copied with its relative start offset preserved.
Color math, output checks, numerical tolerances, trust checks, and file safety
are unchanged by this contract.
