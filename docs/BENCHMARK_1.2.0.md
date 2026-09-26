# LogForge 1.2.0 performance measurements

Local measurement: 2026-09-26; Windows build 26200, 20 logical CPUs, NVIDIA GeForce RTX 3080 Ti Laptop GPU (16 GB), NVIDIA driver 616.92 / CUDA Driver API 13040. Release MSVC x64, static runtime, precise color math.

The formal matrix processes **one second of generated HLG grayscale ramp** at each capture rate. Each job separately performs FFmpeg trust/capability/numeric verification and the complete production output validator. Decode and ProRes encoding remain CPU operations in both modes. Results are observations of this laptop, not real-time or all-GPU performance promises. Short jobs include a substantial fixed verification cost.

| Input | Mode | Backend | Pipeline s | Color s | Validation s | CLI total s | Pipeline fps | App / process-tree peak MiB |
| --- | --- | --- | ---: | ---: | ---: | ---: | ---: | ---: |
| 1920x1080 / 24 | Standard | CPU | 1.239 | 1.025 | 0.850 | 4.966 | 19.37 | 14.4 / 545.8 |
| 1920x1080 / 24 | Creative | CPU | 1.934 | 1.280 | 0.752 | 5.288 | 12.41 | 27.9 / 541.1 |
| 1920x1080 / 24 | Standard | CUDA | 0.753 | 0.503 | 0.782 | 4.740 | 31.85 | 298.9 / 855.7 |
| 1920x1080 / 24 | Creative | CUDA | 1.108 | 0.417 | 0.842 | 4.727 | 21.66 | 349.6 / 914.3 |
| 3840x2160 / 24 | Standard | CPU | 5.492 | 4.196 | 1.203 | 9.676 | 4.37 | 14.4 / 2066.5 |
| 3840x2160 / 24 | Creative | CPU | 7.593 | 4.984 | 1.057 | 11.784 | 3.16 | 99.1 / 2091.1 |
| 3840x2160 / 24 | Standard | CUDA | 3.409 | 1.779 | 1.057 | 8.550 | 7.04 | 298.5 / 2495.1 |
| 3840x2160 / 24 | Creative | CUDA | 4.597 | 1.691 | 0.984 | 8.752 | 5.22 | 565.3 / 2681.8 |
| 3840x2160 / 30 | Standard | CPU | 6.358 | 5.156 | 1.043 | 10.199 | 4.72 | 14.4 / 2074.9 |
| 3840x2160 / 30 | Creative | CPU | 9.845 | 6.532 | 1.026 | 13.789 | 3.05 | 99.2 / 2096.0 |
| 3840x2160 / 30 | Standard | CUDA | 3.688 | 1.836 | 1.077 | 7.826 | 8.14 | 297.4 / 2650.8 |
| 3840x2160 / 30 | Creative | CUDA | 5.854 | 2.317 | 0.945 | 9.857 | 5.12 | 566.0 / 2796.9 |
| 3840x2160 / 60 | Standard | CPU | 11.909 | 10.339 | 0.941 | 15.676 | 5.04 | 14.4 / 2110.3 |
| 3840x2160 / 60 | Creative | CPU | 19.191 | 13.118 | 1.078 | 22.999 | 3.13 | 99.2 / 2098.8 |
| 3840x2160 / 60 | Standard | CUDA | 6.820 | 4.049 | 1.059 | 10.947 | 8.80 | 297.9 / 2825.8 |
| 3840x2160 / 60 | Creative | CUDA | 10.580 | 4.197 | 1.134 | 14.783 | 5.67 | 565.0 / 2768.6 |
| 3840x2160 / 120 | Standard | CPU | 23.387 | 20.956 | 1.001 | 27.336 | 5.13 | 14.4 / 2110.7 |
| 3840x2160 / 120 | Creative | CPU | 37.734 | 26.465 | 0.995 | 41.733 | 3.18 | 99.2 / 2099.5 |
| 3840x2160 / 120 | Standard | CUDA | 12.465 | 7.659 | 1.142 | 16.405 | 9.63 | 297.9 / 2768.5 |
| 3840x2160 / 120 | Creative | CUDA | 16.985 | 6.876 | 0.972 | 21.052 | 7.07 | 565.6 / 2866.2 |

## Timing and memory definitions

The [complete machine-readable record](benchmarks/1.2.0.json) contains decoder/encoder process CPU seconds, read/write backpressure, transform, remux, creation metadata, identification, validation and total job times; worker counts; peak process-tree thread counts; GPU name/qualification/upload/kernel/download timings; and encoded pixel errors. These are concurrent stage measurements and **must not be added as if they were sequential wall times**. CLI total includes startup/tool verification outside the transcode job.

CPU allocation on this host is 2 decode / 9 color / 7 encode workers, plus one matrix filter per FFmpeg process. CUDA uses 5 decode / 11 encode workers, two reserved CPU fallback workers and one matrix filter per process. FFmpeg/driver helper threads are included in observed process-tree counts, so OS thread counts exceed configured compute workers. Small CPU counts retain at least one worker per necessary stage.

Standard uses three chunks of at most 4 MiB. Creative retains one float32 planar GBR frame (about 94.9 MiB at 4K), because sequential rawvideo planes cannot be treated as independent RGB stripes. CUDA also reserves pinned host/device buffers and incurs driver-context memory overhead; CPU and GPU modes are not equal-memory implementations. Reported private bytes include host allocations only; GPU device buffer size is recorded separately by current job reports.

No audio or rotation was added to this throughput matrix, so its remux time is near zero. The five portrait/header/timecode regressions independently exercise real remux and record stage times. This matrix does not claim to benchmark portrait remux throughput or extended laptop thermal equilibrium.

## Sustained CPU comparison

For the separately measured **240-frame 4K120 CPU Standard** workload, the original
serial bridge took 65.072 seconds including tool verification; the final overlapped
bridge took 45.202 seconds (3.688 to 5.309 fps). Pipeline wall time changed from
61.625 to 41.513 seconds, while color-transform time remained essentially equal
(38.395 versus 38.186 seconds). This supports overlap as the main observed gain;
the official color equations were not replaced. Final application peak private
memory was 14.43 MiB, with three buffers of at most 4 MiB each. The independent
post-encode luma check measured a maximum 0.8651 code-value error across 1,536
samples, below its unchanged 2-code-value limit.

These are single local runs of the same generated workload, not a statistical
cross-machine performance guarantee. Source generation and post-test sampling
are excluded from the stated conversion time.

## Reproduce

```powershell
python tests/benchmark.py --cli build/Release/LogForge-cli.exe --ffmpeg C:/approved/ffmpeg.exe --work build/benchmark --seconds 1
```

Use a longer `--seconds` value for a longer workload; source/output media are generated locally and removed after each case. Existing strict numeric tests remain separate and unchanged. `tests/sustained.py` additionally checks 240 consecutive 4K120 frames, memory growth, cadence and independent post-encode code values. See [the final verification record](VALIDATION.md) for that result.
