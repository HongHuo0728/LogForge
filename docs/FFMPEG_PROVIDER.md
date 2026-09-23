# FFmpeg build provider

## Pinned build

| Field | Value |
| --- | --- |
| Provider | Gyan Doshi / Gyan.dev |
| Build | FFmpeg 8.1.2 essentials, Windows x64 static |
| URL | https://www.gyan.dev/ffmpeg/builds/packages/ffmpeg-8.1.2-essentials_build.zip |
| Bytes | 109728040 |
| SHA-256 | `db580001caa24ac104c8cb856cd113a87b0a443f7bdf47d8c12b1d740584a2ec` |
| Published checksum | https://www.gyan.dev/ffmpeg/builds/packages/ffmpeg-8.1.2-essentials_build.zip.sha256 |
| License stated by provider | GPLv3 |
| Source tag | https://github.com/FFmpeg/FFmpeg/tree/n8.1.2 |
| Provider repository / build info | https://github.com/GyanD/codexffmpeg |
| Provider package root | `ffmpeg-8.1.2-essentials_build` |

The checksum was retrieved from the provider over HTTPS and the downloaded archive independently hashed on 2026-09-22. It is embedded in the program; runtime installation does not trust a just-downloaded checksum. This is a pinned tested build, not a claim that it is the newest FFmpeg release. Updates require maintainer review, replacing the fixed URL/hash/root together and rerunning capability, pixel and integration tests.

FFmpeg.org's download page links to Gyan's Windows builds. FFmpeg.org itself distributes source releases, not this Windows executable. The essentials build includes `libzimg`/`zscale` and `prores_ks`; LogForge also checks those at runtime instead of assuming that any file called ffmpeg.exe is suitable.

## Automatic discovery

Discovery visits managed/saved paths, PATH, common locations, then accessible fixed/removable volumes. **A filename is not authorization.** Unknown candidates are listed without running even `-version`. Only approved hashes enter capability/numeric checks. `ffmpeg-trust.json` binds canonical paths and SHA-256 for both ffmpeg and ffprobe. Legacy settings and merely residing inside the managed directory do not grant trust. A change to either binary requires fresh approval.

Manual selection shows paths/hashes and an execution confirmation (No by default). The CLI authorization command is `--approve-ffmpeg --ffmpeg PATH`. Executable images remain open with deny-write/delete sharing during use. The trust store is not a security boundary against an attacker who already controls the user's settings/application. Approval of a shared-library build also requires trusting its DLL dependencies; the pair hashes do not attest every loaded DLL. The pinned provider build is static.

Execution uses the resolved canonical pair, not the discovered alias. A malicious ffprobe placed beside a file alias to an approved ffmpeg must never substitute for the approved sibling. A probe resolving outside the canonical pair's directory is rejected. The alias-sibling regression uses a real approved FFmpeg and an executable canary; it runs where Windows permits symbolic links.

**Verified FFmpeg** passes feature checks and integer reference-signal tests for BT.2020 matrix/range, float transport, left/center chroma phase, ProRes HQ and post-encode samples. Limits are `2e-6` float error and 2 ten-bit code values after encoding. **Compatible but unverified FFmpeg** is available for capability-only inspection and cannot transcode. Numeric failures are errors. Reports record paths, version, both hashes, trust type and actual measured errors.

Scanning is read-only apart from LogForge's local logs/cache. Synthetic capability/numeric files are produced only after approval. It never changes PATH or deletes another installation. The UI reports actual directory/candidate counts and supports cancellation. Inaccessible, unavailable and offline locations are skipped; reparse subdirectories, recycle bins and protected restore data are excluded. Remote network drives are not scanned. This is a search of accessible local storage, not a guarantee about unreadable or disconnected disks.

Only after this search completes unsuccessfully do **Download FFmpeg** and **Locate FFmpeg...** appear. Both disappear when a numerically verified installation is ready. A cancelled scan offers **Search drives again** without falsely claiming a complete search.

## Installation mechanics

`FFmpegBuildProvider` supplies a `DownloadSpec`; `GyanReleaseProvider` implements the current spec. `FFmpegDownloader` handles transport, SHA-256 verification and installation independently of the provider.

- WinHTTP performs certificate-validated HTTPS using Windows TLS and system proxy discovery; HTTPS-to-HTTP redirection is prohibited.
- Three download attempts, bounded network timeouts, exact byte progress where Content-Length is available, 512 MiB archive limit, and cancellation checks.
- At least 1 GiB free space is required before extraction.
- The archive is extracted only after SHA-256 matches, using Windows' built-in `tar.exe`. Archive paths are checked for traversal/absolute paths before extraction.
- A uniquely named staging directory is used. After the archive hash passes, a managed-sha256 receipt records both executable hashes before execution. Capability and numeric tests must pass before publication. The receipt is recorded for the final path too.
- The provider's LICENSE, README, documentation and binaries are kept together. No global PATH, system environment, registry installation, Program Files, service or administrator changes occur.
- Settings and logs remain per-user. No videos are sent to the provider.

Interrupted/failed installers may leave a clearly named `install-PID-ticks` staging directory for diagnosis. It is not treated as an installed tool. V1 does not delete arbitrary older tool directories or user-selected installations. A maintainer should review retained staging files when diagnosing an installation failure.

## Redistribution

LogForge's MIT license covers its own source, not the independently obtained FFmpeg binary or its enabled libraries. The portable ZIP intentionally excludes FFmpeg. If downstream distributors bundle FFmpeg, they must separately satisfy its GPL and component obligations, including corresponding source as applicable. Preserve the provider's notices. See FFmpeg's own [license discussion](https://ffmpeg.org/legal.html).

Manual builds are supported after explicit approval and successful capability plus numerical verification. Their origin, updates and licensing remain the user's responsibility. No private Apple encoder is downloaded or impersonated.
