# Metadata contract and reference-sample research

Apple Log pixel encoding and Apple-specific identification are separate concerns.

## V1 output

- QuickTime MOV sample entry `apch`, ProRes HQ; encoded frame color primaries and matrix are BT.2020.
- `colr` payload is `nclc`, primaries **9**, transfer **2**, matrix **9**. The raw atom and ffprobe result are validated after every conversion.
- Transfer **2** means unspecified. Generic transfer **9** is logarithmic 100:1 and is not Apple Log; **18** is HLG and would be wrong after this transform.
- Custom QuickTime metadata `logforge.transfer=Apple Log`, `logforge.reference=BT2408_HLG75pct_to_100pct_reflectance`, `logforge.exposure_ev`, `logforge.version`, and `logforge.build` document the actual processing. No editor support for these project-owned keys is assumed. Versions 0.1.0/0.1.1 used the distinct `HLG75pct_to_90pct_reflectance` policy.
- Output range is video range. Legacy MOV `nclc` has no `nclx` full-range flag. V1 follows ProRes/FFmpeg's video-range interpretation and verifies the decoder reports `tv`.
- Source global and video tags are copied where FFmpeg can write them. Conflicting HDR/transfer declarations are removed. Original Make/Model, existing capture date, location and other nonconflicting tags are not fabricated.
- Audio streams use packet copy. Existing timecode is passed to MOV's timecode writer. Rotation uses a documented FFmpeg display rotation during a lossless remux; ordinary +90/-90 degree cases are covered by tests.

No Apple encoder vendor ID (`apl0`) is spoofed. No iPhone model substitution is made. The output correctly identifies a software transcode.

LogForge additionally writes `logforge.rendering` (`standard` or `creative-luma-v1`), `logforge.shadow_lift_ev`, `logforge.highlight_compression_ev`, and `logforge.saturation`. These describe the separately enabled creative grade; they are not Apple identifiers and do not alter the container's color interpretation. Disabled rendering records effective values 0 / 0 / 1. Both modes produce the same Apple Log transfer encoding and BT.2020/video-range format.

## Public API versus container bytes

Apple exposes `AVCaptureColorSpace.appleLog`, `kCVImageBufferLogTransferFunctionKey`, `kCVImageBufferLogTransferFunction_AppleLog`, and `kCMFormatDescriptionExtension_LogTransferFunction`. An API attachment name is not by itself a published MOV atom serialization. The reviewed FFmpeg 8.1.2 source has no Apple Log H.273 transfer enum or equivalent muxer mapping.

The project has not established a reliable public serialization for Apple's native Log identification. Therefore V1 does not fabricate atom types, magic numbers, a private `LogTransferFunction` payload or camera-specific tags. Automatic Apple Log detection in Resolve, Premiere and Final Cut remains unverified; manual input assignment is required.

## ReferenceMovAnalyzer

`LogForge-cli --analyze OUTPUT REFERENCE` produces a JSON report with container structure, atom offsets/sizes, known ProRes sample-entry children, decoded `colr` values, ffprobe stream/format metadata, and structural differences. It reads container atoms without loading the entire movie; `mdat` is skipped. Bounds, nesting and atom count limits reject malformed input. Unknown atoms remain opaque.

Required next evidence:

1. A genuine, unmodified iPhone 15 Pro Apple Log ProRes MOV and its capture settings.
2. Ideally a matching HLG capture of chart/ramps under controlled exposure and illumination.
3. Native file versus LogForge output inspected in Resolve CST/LUT, Premiere, and Final Cut, explicitly recording range and input assignment.
4. Atom/metadata differences tied to observed editor behavior. No private field should be added merely because its bytes happen to differ.

V1 does not copy arbitrary camera data streams, Dolby Vision metadata, chapters, GPS tracks, anamorphic display transforms or complex edit lists. Files requiring those should be treated as outside the certified workflow. We do not claim binary equality with an iPhone file.

Version 1.0.0 records `logforge.version=1.0.0` and `logforge.build=26922A`. These are application version identifiers, not camera-model or Apple compatibility identifiers.
