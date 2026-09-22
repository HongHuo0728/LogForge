# Optional creative adjustments

In **Settings**, the **Creative adjustments** switch defaults to off. With it off, the standard transform follows the same numerical path as 0.1.2, regardless of the retained control values. With it on, the following project-defined grade is applied **after scene-linear exposure, before the unchanged Apple Log function**. It is not an Apple camera transform, inverse ISP model, reference-camera match, or a new Log curve.

## Definition

Let `R,G,B` be the exposed scene-linear BT.2020 components:

```text
Y = 0.2627 R + 0.6780 G + 0.0593 B
x = log2(Y / 0.18)                         if Y > 0; otherwise -6
t = clamp(abs(x) / 6, 0, 1)
w = t^2 (3 - 2t)
deltaEV = shadowLiftEV * w                 if x < 0
deltaEV = -highlightCompressionEV * w      otherwise
gain = 2^deltaEV
Rnew = gain [Y + saturation (R - Y)]        likewise for G and B
```

Both stop controls are limited to [0,3], saturation to [0,1.5]. Initial enabled values are **+3 shadow stops / 1 highlight-compression stop / 0.85 saturation**. These are creative choices, not measurements of native Apple Log appearance. At least six stops below/above 18% gray the maximum selected change is reached; near gray it tapers smoothly to zero. Gray remains 0.18, and zero signal remains zero. Detail in very dark nonzero samples can occupy more output codes when lifted, but missing source information is never reconstructed.

The luminance curve is continuous with a continuous first derivative, and has strictly positive log-domain slope: `d(log2 Ynew)/d(log2 Y) >= 1 - 1.5*3/6 = 0.25`. It therefore compresses contrast without reversing the order of neutral levels. The shared RGB gain preserves channel ratios at saturation 1; saturation changes scale linear chroma about BT.2020 luminance. Saturation is not modified in already encoded Apple Log RGB.

This is a global, deterministic mapping with no frame-by-frame auto exposure, local adaptation, analysis of neighboring pixels, or content-dependent parameter changes. It avoids temporal pumping introduced by adaptive settings. It is not a display rendering and does not make the output a ready-to-view SDR deliverable. Extreme exposure or saturation can still exceed the encoding domain; signal warnings must be inspected.

## Usage and metadata

Open **Settings**, enable **Creative adjustments**, choose **Shadow lift (EV) / Highlight reduction (EV) / Saturation (%)**, and select **Save**. Turn the switch off and save to use the standard transform again. Values are retained while disabled. **Cancel** discards pending changes. Language, theme and creative values persist per user. The conversion snapshots settings on the UI thread; Settings is disabled while a background job runs.

The CLI equivalent of the initial settings is:

```powershell
LogForge-cli.exe --convert input.mov adjusted.mov --tone --shadow-lift-ev 3 --highlight-compression-ev 1 --saturation-percent 85
```

Without `--tone`, the standard transform remains active. Tuning parameters supplied without `--tone` are refused, preventing a silent no-op. Non-finite, trailing-junk and out-of-range values are refused before conversion.

Each conversion records the active rendering identifier and effective parameters in project-owned metadata and the local validation report. `creative-luma-v1` is LogForge's algorithm identifier, not an Apple identifier. Effective values for disabled rendering are 0 / 0 / 1.

## Signal integrity and monitoring limits

LogForge counts every input and transformed RGB component, records extrema, and counts components below Apple's linear floor or above normalized Apple Log 1. This accounting occurs **before** YCbCr quantization and ProRes compression; it is not a full post-encode pixel audit. The GUI and CLI expose warnings instead of silently reporting an unqualified success for range-risk settings. Above nominal white is a warning, not proof of clipping, because nominal range and available code storage differ.

A separate integration test constructs integer 10-bit YCbCr patches directly, without using `zscale` for the reference or fixture matrix. It decodes actual ProRes output to integer YCbCr and checks standard and creative outputs against independent scalar equations, including very dark ramps, above-white ramps, reference gray and color patches. Chroma discontinuities are excluded from patch-center numeric comparisons, but included in the all-component range counters. Hard boundaries can create reconstruction excursions.

10-bit HLG to 10-bit Apple Log is not lossless: multiple extremely dark HLG codes may map to the same Apple Log toe code, and 4:2:2 resampling/ProRes compression add further error. An RGB component merely being above zero or below one does not prove that all local texture survived. Detail investigations must compare the same region after decoding both images into a common signal domain.
