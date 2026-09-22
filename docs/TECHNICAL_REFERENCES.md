# Technical references

Reviewed 2026-09-22. Conclusions are limited to the versions and interfaces below; a source URL does not imply a tested editor or device.

## Apple Log math and gamut

1. **Apple-supplied ACES IDT**, `IDT.Apple.AppleLog_BT2020.ctl`, Academy repository, immutable commit `528c78fe2c0f4e7eb322581e98aba05de79466cb`:
   https://github.com/ampas/aces-dev/blob/528c78fe2c0f4e7eb322581e98aba05de79466cb/transforms/ctl/idt/vendorSupplied/apple/IDT.Apple.AppleLog_BT2020.ctl
   The actual vendor-supplied decoder was retrieved and inspected. Its constants and piecewise branches establish the numerical reference. LogForge implements the algebraic inverse for encoding and does not perform the IDT's separate Rec.2020-to-ACES matrix step.
2. **Apple Log Profile White Paper**, September 2023, official developer download index:
   https://developer.apple.com/download/all/?q=Apple%20log%20profile
   This is the official publication reference. The index was not accessible through the research client; a guessed direct PDF URL returned 404. We do not claim to have retrieved a public standalone Apple-hosted PDF. The executable mathematical reference was independently obtained from Apple's vendor-supplied ACES transform above. The hard-coded test numbers are calculated from those constants.
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
   Supports the 75% HLG reference-white convention. It does not establish the exposure of arbitrary iPhone clips. The 90% reflectance anchor is an explicit LogForge workflow assumption.
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

## Downloads, licenses and dependencies

16. **FFmpeg download page**: https://ffmpeg.org/download.html
17. **Gyan Windows build provider**: https://www.gyan.dev/ffmpeg/builds/
18. **Pinned SHA-256**: https://www.gyan.dev/ffmpeg/builds/packages/ffmpeg-8.1.2-essentials_build.zip.sha256
19. **FFmpeg legal/licensing page**: https://ffmpeg.org/legal.html
20. **nlohmann/json v3.12.0 / MIT**: https://github.com/nlohmann/json/tree/v3.12.0

The installed archive, runtime version, encoder/filter capabilities, image data and audio payload were checked locally. A future provider update must repeat those checks. See `VALIDATION.md` for measured results and explicitly unverified claims.
