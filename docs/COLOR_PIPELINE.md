# Color pipeline and numerical contract

## Domain and scope

The input signal is interpreted as BT.2020 non-constant-luminance HLG, scene-referred according to the inverse OETF in ITU-R BT.2100-3. This interpretation does not undo a camera's local tone mapping, noise reduction, sharpening, exposure choice, white balance or other ISP processing. An already rendered HLG grade remains a rendered signal under this interpretation.

No HLG display OOTF, display peak-nit conversion, or inverse display EOTF is applied. Using a display transform here would introduce a different luminance-dependent rendering and would not be the stated scene pipeline.

## Equations

For normalized HLG component `H`, use `a = 0.17883277`, `b = 1 - 4a`, `c = 0.5 - a ln(4a)`:

```text
E = H² / 3                         0 <= H <= 0.5
E = (exp((H - c) / a) + b) / 12     H > 0.5
```

For small negative RGB excursions caused by matrix conversion/resampling, LogForge explicitly extends the lower branch with `E = -H²/3`. This signed extension is an engineering choice outside the nominal [0,1] HLG domain, not a claim about camera sensor encoding. Above-white values use the analytic upper branch; the intermediate float transport does not clamp them.

HLG's relative scene normalization does not identify absolute reflectance/exposure. V1 adopts this visible, fixed convention:

```text
S = 0.9 / inverseHLG(0.75) = 3.3967063066191767
R = S E
```

The 75% HLG reference-white convention is documented in ITU production guidance. Associating that reference with a 90% reflecting chart is **LogForge's policy**, not an Apple or iPhone calibration guarantee. There is no arbitrary tone mapping. Exposure matching against a real scene may still be needed during grading.

Apple Log is the inverse of Apple's vendor-supplied ACES decoder, using these constants:

```text
R0 = -0.05641088
Rt =  0.01
C  = 47.28711236
B  =  0.00964052
G  =  0.08550479
D  =  0.69336945
Pt = C (Rt - R0)²

P = G log2(R + B) + D          R >= Rt
P = C (R - R0)²               R0 <= R < Rt
P = 0                        R < R0

R = 2^((P - D)/G) - B         P >= Pt
R = sqrt(P/C) + R0            0 <= P < Pt
R = R0                       P < 0
```

`R = 0.18` is the Apple Log reference-gray reflectance domain, and `P` is a normalized encoded component. Independently calculated values from the constants above:

| R | P |
| --- | --- |
| 0 | 0.1504764523009125 |
| 0.01 | 0.2085553187030790 |
| 0.18 | 0.4882724585268676 |
| 0.90 | 0.6816867959342260 |
| 12.0 | 0.9999999784008755 |

Published constants are rounded. The two branches differ very slightly at their mathematical join. Tests allow `2e-8` at the knee, `2e-7` for linear-domain round trips, and `3e-8` at the HLG unity reference. These are numerical tolerances, not perceptual similarity criteria.

## Actual implementation

1. FFmpeg decodes ProRes to its native 10-bit 4:2:2 representation.
2. `zscale` explicitly applies the BT.2020 NCL matrix and input range, reconstructs chroma with spline36, and produces full-range `gbrpf32le` HLG-coded RGB. Its input and output transfer are the same, so it performs no transfer conversion or OOTF.
3. LogForge transports one planar float frame through supervised pipes. Each component is promoted to double for inverse HLG, reference scaling and Apple Log encoding; only the result is stored as float. There is no LUT, no 3D gamut transform and no 8-bit intermediate.
4. A second FFmpeg process receives float Apple Log RGB. Its `zscale` stage has **equal linear input/output transfer labels solely to bypass transfer processing** while converting RGB to BT.2020 NCL YCbCr. This label does not assert that the values are scene-linear. `setparams` clears it before encoding.
5. Chroma is downsampled to left-sited 4:2:2 and quantized with error-diffusion dithering to `yuv422p10le`, then encoded by `prores_ks`, profile 3 (HQ).

Both primaries stay BT.2020 / D65. In normalized coded RGB:

```text
Y  = 0.2627 R' + 0.6780 G' + 0.0593 B'
Cb = (B' - Y) / 1.8814
Cr = (R' - Y) / 1.4746
```

The nominal output code mapping is video range: `Ycode = 64 + 876Y`, `Cbcode = 512 + 896Cb`, `Crcode = 512 + 896Cr`, followed by rounding/dithering and codec quantization. Normalized `P=0.488272` therefore does **not** imply raw stored Y code 500 in this video-range container. A full-range table and limited-range storage are different mappings of the same normalized function. Editors must normalize video levels before CST/LUT processing.

Final 10-bit code storage and lossy ProRes compression are finite; arbitrary out-of-range excursions cannot be preserved indefinitely. Apple Log itself floors below `R0`. These boundaries are documented instead of presented as dynamic-range recovery.

## What the tests demonstrate

Tests compare mathematical reference points, 100,001 samples across each forward/inverse pair, finite float handling, and real decoded codec pixels. Generated frames contain gray, shadow, highlight, super-white, BT.2020 primary and color ramps. Codec error measurements exclude discontinuity borders affected by chroma resampling. Results do not certify a commercial editor or a particular phone's exposure mapping.
