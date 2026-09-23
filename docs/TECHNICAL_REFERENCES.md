# Technical references

Reviewed through 2026-09-23. Conclusions are limited to the versions and interfaces below; a source URL does not imply a tested editor or device.

## Native Apple Log and actual Resolve import (2026-09-23)

- [Publisher's original tutorial](https://www.youtube.com/watch?v=K5F2uyMxL3Y), its [sample folder](https://drive.google.com/drive/folders/1FOziyLgHfKimVunhcF5Ws5ZCcvkZcvEp) and [specific ProRes original](https://drive.google.com/file/d/1iS5rYcgOyOKQfgFq6uHmx7oL51vUpeC_/view). The original description and file bytes were retrieved directly. SHA-256, provenance limits, exact observed `logs` and movie-level mdta fields, and controlled import results are recorded in [Apple Log identification evidence](APPLE_LOG_IDENTIFICATION.md).
- Apple's [video sample description](https://developer.apple.com/documentation/quicktime-file-format/video_sample_description) explicitly permits a four-zero-byte optional terminator. This explains the real reference's layout and supports the bounded analyzer correction; unrelated truncation errors are not suppressed.
- The locally installed Blackmagic **DaVinci Resolve Studio 20.3.2.9 Scripting README**, last updated 7 October 2025, documents `-nogui`, `GetClipProperty`, project color settings, media import and rendering. Actual SDK calls imported the original, negative baseline, isolated candidates and production output. This is direct runtime evidence, not inference from API names or a simulated Resolve test.

The following 2026-09-22 notes record the earlier investigation. Native-reference/Resolve conclusions from that date are superseded by the evidence above; Premiere and Final Cut remain untested.

## Hardening research (2026-09-22)

- [VideoToolbox LogTransferFunction](https://developer.apple.com/documentation/videotoolbox/kvtcompressionpropertykey_logtransferfunction) explicitly identifies `com.apple.rec2020.apple-log` for Apple Log. This is an API identifier, not sufficient evidence of MOV serialization or editor recognition.
- [CoreMedia LogTransferFunction extension](https://developer.apple.com/documentation/coremedia/kcmformatdescriptionextension_logtransferfunction) and [typed extension](https://developer.apple.com/documentation/coremedia/cmformatdescription/extensions-swift.struct/key/logtransferfunction) describe format-description signaling. No private sample-entry bytes are inferred from those symbol names. No authoritative mapping for `com.apple.proapps.customgamma` to verified iPhone/editor behavior was established during this review; it remains a candidate for reference-file research only.
- Apple's [metadata structure](https://developer.apple.com/documentation/quicktime-file-format/metadata_atoms_and_types), [item keys](https://developer.apple.com/documentation/quicktime-file-format/metadata_item_keys_atom), [item list](https://developer.apple.com/documentation/quicktime-file-format/metadata_item_list_atom), [value atom](https://developer.apple.com/documentation/quicktime-file-format/value_atom) and [well-known value types](https://developer.apple.com/documentation/quicktime-file-format/well-known_types) define key-index resolution and type/locale/value decoding. These underpin the reader, not a speculative Apple Log writer.
- [FFmpeg zscale implementation](https://ffmpeg.org/doxygen/8.1/vf__zscale_8c_source.html) exposes explicit input `chromalin` and output `chromal`. Runtime tests distinguish left and center using a linear chroma ramp and independently computed BT.2020 equations, then sample the encoded output phase. [Apple's left-siting definition](https://developer.apple.com/documentation/corevideo/kcvimagebufferchromalocation_left) describes horizontal co-siting. Neither that definition nor a command-line option guarantees that ProRes MOV carries a readable siting tag. Our selected FFmpeg's output was re-probed: it omits chroma_location. Reports preserve that limitation instead of inventing a native atom.

At that earlier review no genuine native Apple Log movie or editor project was available. The synthetic fixtures alone were not evidence of native-camera serialization. The current writer still uses H.273 transfer unspecified, as does the actual reference, with the separately verified sample-entry `logs` identifier added after encoding.

## Apple Log math and gamut

1. **Apple-supplied ACES IDT**, `IDT.Apple.AppleLog_BT2020.ctl`, Academy repository, immutable commit `528c78fe2c0f4e7eb322581e98aba05de79466cb`:
   https://github.com/ampas/aces-dev/blob/528c78fe2c0f4e7eb322581e98aba05de79466cb/transforms/ctl/idt/vendorSupplied/apple/IDT.Apple.AppleLog_BT2020.ctl
   The actual vendor-supplied decoder was retrieved and inspected. Its constants and piecewise branches establish the numerical reference. LogForge implements the algebraic inverse for encoding and does not perform the IDT's separate Rec.2020-to-ACES matrix step.
2. **Apple Log Profile White Paper**, September 2023, official developer download index:
   https://developer.apple.com/download/all/?q=Apple%20log%20profile
   This is the official publication reference. The index was not accessible through the research client; a guessed direct PDF URL returned 404. We do not claim to have retrieved a public standalone Apple-hosted PDF. The executable mathematical reference was independently obtained from Apple's vendor-supplied ACES transform above. The hard-coded test numbers are calculated from those constants.
   During the 0.1.2 audit, a third-party text mirror of the September 2023 paper was also inspected: https://www.scribd.com/document/695704838/Apple-Log-Profile-White-Paper . Its constants and BT.2020 NCL matrix agree with the independently retrieved references. This is corroboration from a secondary host, not authenticated evidence of a camera file's MOV serialization or range flags.
3. **AVCaptureColorSpace.appleLog**:
   https://developer.apple.com/documentation/avfoundation/avcapturecolorspace/applelog
   Its official Markdown endpoint was retrieved: https://developer.apple.com/documentation/avfoundation/avcapturecolorspace/applelog.md
   Establishes BT.2020 primaries and an Apple-defined Log transfer. Apple Log 2 is a different color space and is outside v1.

## HLG, scene light and normalization

4. **ITU-R BT.2100-3 (02/2025)**, Table 5 and transfer-function annexes:
   https://www.itu.int/dms_pubrec/itu-r/rec/bt/R-REC-BT.2100-3-202502-I!!PDF-E.pdf
   Establishes scene-light HLG OETF, constants and distinction from the OOTF / display EOTF. LogForge uses the inverse OETF only.
5. **ITU-R BT.2408-6**, operational HDR production guidance, scene-referred mapping sections:
   https://www.itu.int/dms_pub/itu-r/opb/rep/R-REP-BT.2408-6-2023-PDF-E.pdf
   Section 2.1 and Table 1 define 75% HLG as **100%** reflecting reference white; 90% reflecting white is approximately 73% HLG. Version 0.1.2 corrects the earlier project-specific 90% anchor to this nominal reference. It does not establish the exposure of arbitrary iPhone clips. Sections 2.1/2.2 also distinguish reference alignment from camera painting and creative exposure.
6. **ITU-R BT.2020-2**, color primaries and non-constant-luminance matrix:
   https://www.itu.int/rec/R-REC-BT.2020
   BT.2020/D65 is maintained throughout the pipeline.

## MOV signaling and Apple API limits

7. **Apple Technical Note TN2162**, `colr`/`nclc` extension and YCbCr range conventions:
   https://developer.apple.com/library/archive/technotes/tn2162/_index.html
   An archival QuickTime specification; useful for atom structure, not evidence of a current private Apple Log identifier.
8. **CoreVideo log attachment APIs**:
   https://developer.apple.com/documentation/corevideo/kcvimagebufferlogtransferfunctionkey
   https://developer.apple.com/documentation/corevideo/kcvimagebufferlogtransferfunction_applelog
9. **CoreMedia format-description log extension**:
   https://developer.apple.com/documentation/coremedia/kcmformatdescriptionextension_logtransferfunction
   These establish API symbols, not a validated MOV byte serialization in this project. No fabricated private atom is written.

## FFmpeg and its actual selected version

10. **FFmpeg 8.1.2 color transfer enumeration**:
    https://github.com/FFmpeg/FFmpeg/blob/n8.1.2/libavutil/pixfmt.h
    Retrieved source includes unspecified=2, generic log=9 and HLG=18; no Apple Log enum is present. Generic log is not substituted for Apple Log.
11. **FFmpeg MOV muxer**:
    https://github.com/FFmpeg/FFmpeg/blob/n8.1.2/libavformat/movenc.c
    `mov_write_colr_tag` writes `nclc` for MOV and `nclx` for MP4. Actual generated MOV bytes are additionally inspected by tests.
12. **FFmpeg ProRes encoder and options**:
    https://ffmpeg.org/ffmpeg-codecs.html#ProRes
    https://github.com/FFmpeg/FFmpeg/blob/n8.1.2/libavcodec/proresenc_kostya.c
    `prores_ks` profile `hq` / 3, `yuv422p10le`. No Apple vendor identity is spoofed.
13. **FFmpeg filters**:
    https://ffmpeg.org/ffmpeg-filters.html#zscale
    https://github.com/FFmpeg/FFmpeg/blob/n8.1.2/libavfilter/vf_zscale.c
    `zscale` handles explicit matrices, range, chroma resampling and float formats. Equality of input/output transfer avoids applying an unwanted HLG display conversion. This behavior is verified with decoded numerical ramps.
14. **Investigated, not selected: FFmpeg LUT implementation**:
    https://github.com/FFmpeg/FFmpeg/blob/n8.1.2/libavfilter/vf_lut3d.c
    A 65536-entry 1D LUT and float execution exist, but v1 uses direct equations so no LUT interpolation/domain clipping is required.
15. **FFmpeg progress and timestamp controls**:
    https://ffmpeg.org/ffmpeg.html
    `-progress pipe:1`, `-fps_mode passthrough`, `-copyts`, `-display_rotation`, audio stream copy. Actual output frame count is also used where progress timestamps are temporarily zero.
    Audio layout policy: https://ffmpeg.org/ffmpeg.html#Advanced-Audio-options documents `-guess_layout_max 0` to disable inferred layouts. LogForge applies it when copying audio, including a rotation remux.
    Timing fields: https://ffmpeg.org/doxygen/8.1/structAVStream.html defines `avg_frame_rate` as a mean and cautions that `r_frame_rate` is a guess. LogForge checks the candidate against all packet timestamps, including cumulative phase, before using it.

## Downloads, licenses and dependencies

16. **FFmpeg download page**: https://ffmpeg.org/download.html
17. **Gyan Windows build provider**: https://www.gyan.dev/ffmpeg/builds/
18. **Pinned SHA-256**: https://www.gyan.dev/ffmpeg/builds/packages/ffmpeg-8.1.2-essentials_build.zip.sha256
19. **FFmpeg legal/licensing page**: https://ffmpeg.org/legal.html
20. **nlohmann/json v3.12.0 / MIT**: https://github.com/nlohmann/json/tree/v3.12.0

The installed archive, runtime version, encoder/filter capabilities, image data and audio payload were checked locally. A future provider update must repeat those checks. See `VALIDATION.md` for measured results and explicitly unverified claims.

## Project-defined creative rendering

The optional 0.1.3 `creative-luma-v1` curve is a LogForge creative adjustment requested separately from standard conversion. Its full equation, parameter limits and monotonicity bound are documented in `CREATIVE_ADJUSTMENTS.md`. It is not attributed to Apple, ITU or a camera vendor. Apple's published transfer function is applied unchanged after this adjustment. Unit and integer-YCbCr codec tests establish that the declared equations are implemented; they do not establish a native-camera appearance match.

## Windows application resources and discovery

21. **Microsoft VERSIONINFO resource**: https://learn.microsoft.com/en-us/windows/win32/menurc/versioninfo-resource
    Numeric four-part Windows versions are distinct from the application build display. CMake generates both resource and C++ definitions from one source.
22. **Microsoft DWM window attributes**: https://learn.microsoft.com/en-us/windows/win32/api/dwmapi/ne-dwmapi-dwmwindowattribute
    Documented caption theme/color attributes are requested when available. The content palette and controls are drawn independently; unsupported caption attributes do not prevent the app from running.
23. **Microsoft volume path enumeration**: https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-getvolumepathnamesforvolumenamew
    Local mount paths supplement ordinary drive letters. Remote, unavailable and unreadable locations are not asserted to have been searched.
24. **Microsoft directory enumeration**: https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-findfirstfileexw
    Discovery uses explicit enumeration and reparse/offline exclusions, with cancellation and actual counts.
