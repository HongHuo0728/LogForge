# Metadata contract and reference-sample research

Apple Log pixel encoding and Apple-specific identification are separate concerns.

## V1 output

- QuickTime MOV sample entry `apch`, ProRes HQ; encoded frame color primaries and matrix are BT.2020.
- `colr` payload is `nclc`, primaries **9**, transfer **2**, matrix **9**. The raw atom and ffprobe result are validated after every conversion.
- The ProRes sample entry contains `logs` with the exact UTF-8 payload `com.apple.rec2020.apple-log`. This separate identification writer is based on a real camera original and successful Resolve A/B imports; see [evidence and verified scope](APPLE_LOG_IDENTIFICATION.md).
- Transfer **2** means unspecified. Generic transfer **9** is logarithmic 100:1 and is not Apple Log; **18** is HLG and would be wrong after this transform.
- Custom QuickTime metadata `logforge.transfer=Apple Log`, `logforge.reference=BT2408_HLG75pct_to_100pct_reflectance`, `logforge.exposure_ev`, `logforge.version`, and `logforge.build` document the actual processing. No editor support for these project-owned keys is assumed. Versions 0.1.0/0.1.1 used the distinct `HLG75pct_to_90pct_reflectance` policy.
- Output range is video range. Legacy MOV `nclc` has no `nclx` full-range flag. V1 follows ProRes/FFmpeg's video-range interpretation and verifies the decoder reports `tv`.
- Copying starts with `-map_metadata -1` for format/video/audio. The whitelist is creation time/date, real camera make/model (including their QuickTime keys), timecode and language. Source tags outside it, including custom gamma, HLG/HDR/Dolby/PQ, GPS, source encoders and unknown private fields, are removed. Both format and video stream dictionaries are processed. A real camera value found only at video scope may be promoted to format mdta when no conflicting format value exists.
- The JSON copy plan records source scope, promoted write scope, values and removal reasons. Output validation verifies preserved values and rejects conflicting color declarations. Codec/muxer structural fields such as brands and vendor are newly generated rather than copied source claims.
- Audio streams use packet copy. Existing timecode is passed to MOV's timecode writer. Rotation uses a documented FFmpeg display rotation during a lossless remux; 90, -90, 180 and 270 degrees, multiple audio streams and drop-frame timecodes are covered by tests.

No Apple encoder vendor ID (`apl0`) is spoofed. No iPhone model substitution is made. The output correctly identifies a software transcode.

LogForge additionally writes `logforge.rendering` (`standard` or `creative-luma-v1`), `logforge.shadow_lift_ev`, `logforge.highlight_compression_ev`, and `logforge.saturation`. These describe the separately enabled creative grade; they are not Apple identifiers and do not alter the container's color interpretation. Disabled rendering records effective values 0 / 0 / 1. Both modes produce the same Apple Log transfer encoding and BT.2020/video-range format.

## Public API versus container bytes

Apple exposes `AVCaptureColorSpace.appleLog`, `kCVImageBufferLogTransferFunctionKey`, `kCVImageBufferLogTransferFunction_AppleLog`, and `kCMFormatDescriptionExtension_LogTransferFunction`. An API attachment name is not by itself a published MOV atom serialization. The reviewed FFmpeg 8.1.2 source has no Apple Log H.273 transfer enum or equivalent muxer mapping.

Apple's VideoToolbox documentation explicitly names `com.apple.rec2020.apple-log` as a Log curve identifier. CoreMedia also exposes the LogTransferFunction format-description extension. Neither API statement alone specifies MOV bytes. On 2026-09-23, an unmodified iPhone 15 Pro Max / Blackmagic Camera reference established a 35-byte `logs` sample-entry atom with that identifier and movie-level mdta `com.apple.proapps.customgamma` with the same value.

Actual Resolve Studio 20.3.2.9 managed imports verified `logs` alone is sufficient. Movie-level `/moov/meta` customgamma was also sufficient, but the same tag in FFmpeg's `/moov/udta/meta` was not. The production writer therefore adds only `logs` after encoding/rotation, then validates it before publishing. Source customgamma remains excluded by the whitelist. No Apple encoder or camera identity is copied to trigger detection. Premiere, Final Cut and other Resolve editions/versions remain unverified; no binary camera-file equivalence is claimed.

## Chroma siting evidence

Input filters explicitly set `chromalin=left` or `center`. Missing siting requires an explicit user declaration (GUI prompt or CLI `--input-chroma-location`), otherwise conversion is refused. The original probe field and override remain separate in reports; an override never replaces a supported native declaration.

Output uses `chromal=left`. **FFmpeg 8.1.2 ProRes MOV does not expose chroma_location on re-probe even when the encoder option requests it.** This tested limitation is recorded honestly. LogForge writes its own `logforge.chroma_location=left`. When ffprobe omits siting, validation requires that declaration plus successful numeric verification of the actual output sampling phase; a conflicting center field, absent declaration or absent qualification fails. Integer linear ramps distinguish a half-pixel shift for both input sitings and post-encode output. No undocumented chroma atom is written, and native editor recognition of the custom key is not claimed.

## ReferenceMovAnalyzer

`LogForge-cli --analyze OUTPUT REFERENCE --ffmpeg APPROVED_EXE` reports raw atom evidence and a semantic diff. It resolves local keys indices to namespace/name and ilst entries to typed data values. Supported values include multiple locales, UTF-8/UTF-16, signed/unsigned integers, floating point and binary hex. ISO full-box and QuickTime headerless meta are supported; keys may follow ilst. Legacy four-character/freeform metadata is retained. ProRes fields and children include colr, pasp, clap, fiel, gama, mdcv, clli, nested meta and opaque unknown extensions.

Semantic differences compare resolved metadata, sample-entry/color fields and probed streams while ignoring mdat placement, file length and key numbering. Raw offsets/sizes remain available separately. The parser does not load mdat. Bounds, nesting (20), atom count (100,000), key count (65,536) and metadata payload (4 MiB per atom) guards reject malformed/excessive files. Unknown fields remain bytes, never guessed or written.

The native-reference extension also exposes `logs.log_transfer_function`, its
original UTF-8 bytes and optional four-zero-byte video-description terminators.
Terminator locations are diagnostic data, excluded from semantic differences.
Invalid three/five-byte or nonzero endings remain errors.

Additional evidence still needed:

1. Additional camera originals, including Apple's Camera app and other iOS versions.
2. Ideally a matching HLG capture of chart/ramps under controlled exposure and illumination.
3. Repeat the verified Resolve managed-import/range test in Premiere, Final Cut and other Resolve versions; compare actual LUT/CST workflows separately.
4. Tie any further atom changes to observed editor behavior. The existing native-reference/A/B evidence supports only the implemented minimal identification field.

V1 does not copy arbitrary camera data streams, Dolby Vision metadata, chapters, GPS tracks, anamorphic display transforms or complex edit lists. Files requiring those should be treated as outside the certified workflow. We do not claim binary equality with an iPhone file.

Version 1.1.1 records `logforge.version=1.1.1` and `logforge.build=26923D`. These are application version identifiers, not camera-model or Apple compatibility identifiers.
