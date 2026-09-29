# Packet cadence contract in LogForge 1.3.0

LogForge verifies every selected video packet's PTS and duration. Reported nominal
and average frame rates are metadata, not proof of constant frame rate. Uniform
positive packet durations with matching PTS intervals remain authoritative even
when the declared frame rates disagree (including 29.99, 29.98 and 29.9701 fps).

## Integer timestamp quantization

For a fractional period, let `t[i]` be each packet boundary relative to the first
PTS, and `T` a candidate period in input time-base ticks. The final packet end is
also a boundary. A fixed quantized clock must satisfy:

```
max(t[i] - i*T) - min(t[i] - i*T) <= 1 tick
```

Every boundary shares the same quantization cell. The closed one-tick cell covers
floor/ceil timestamp rounding and a bounded one-tick clock correction; it is not
a larger empirical replacement for the former 1.05-tick phase threshold. Positive
durations must be adjacent integers, and every PTS interval must exactly equal the
previous duration. Gaps, overlap, duplicate/backward PTS and missing/non-positive
durations are rejected before inference.

Nominal FPS proposes a candidate first, but has no veto if it fails. The endpoint
period then proposes a rational candidate. All boundaries must pass
the cell test. If that candidate fails, upper and lower convex envelopes determine
the minimum vertical residual span over rational hull-edge slopes. This checks
the entire sequence rather than accepting an average or least-squares fit.
Accelerating, piecewise, and clustered clock changes can have plausible average
rates while failing the shared-cell requirement.

The integer floor period is subtracted with integer arithmetic before envelope
construction and residual evaluation. Remaining coordinates are bounded by packet
count, avoiding cancellation and preventing a large timestamp span from widening
acceptance. The only numerical allowance is floating evaluation roundoff scaled
by packet count. Construction is O(n); sorting hull-edge candidates is at worst
O(n log n), with logarithmic envelope-support queries and a final O(n) check.
Storage is O(n).

## Reported 59.94 regression

The regression reconstructs the supplied numbers with 309 packets, time base
`1/1200`, total duration 6183 ticks, and nominal rate `60000/1001`:

```
PTS[i] = floor(i * 2061 / 103)
average fps = 309 * 1200 / 6183 = 59.970887918...
nominal period = 20.02 ticks
packet 53 nominal phase error = 1.06 ticks
```

The former nominal-clock gate rejects packet 53. The verified inferred period is
`2061/103` ticks, whose full sequence occupies less than one tick of residual
span. The new report retains the nominal phase disagreement separately from the
inferred-clock error; it does not hide the discrepancy. This is a generated timing
fixture matching the reported numbers, not a claim to have examined the original
user recording.

## Identifiability and regression-contract correction

Timestamps alone cannot distinguish every physical clock defect from a different
fixed rational clock. For example, the existing negative fixture
`PTS[i] = 20*i - floor(i/64)` is also mathematically a quantized `1279/64`-tick
period. Classifying that sequence as VFR solely because its nominal tag says 24
fps would violate the v1.3 input contract. The fixture is retained as a **positive**
quantized-fixed regression. The negative drift test now uses an accelerating
clock (`20*i-floor(i*i/4096)`) and keeps the rejection assertion; additional
piecewise-rate and clustered-correction negatives prevent average fitting.
This corrects a mislabeled test, rather than weakening a physical timing limit.

Changes smaller than timestamp
quantization, or another physically different clock that produces the same packet
sequence, cannot be identified from packet timings alone. This is not a claim to
detect all possible VFR or physical clock drift. A constant clock differing from
nominal metadata is accepted when every packet supports its quantized fixed
period. A single bounded tick correction still keeps the nominal rate when that
candidate passes the complete quantization-cell check.

Reports include the time base, inferred rate, candidate source, classification,
reported rates, interval/duration/phase errors, phase error in seconds and frame
fractions, quantization span, nominal-clock phase error, and rejection packet and
reason. When inference cannot start because PTS is damaged, diagnostic frame
fractions explicitly identify nominal metadata as their estimation basis.
