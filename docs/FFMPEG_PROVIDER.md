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

## Installation mechanics

`FFmpegBuildProvider` supplies a `DownloadSpec`; `GyanReleaseProvider` implements the current spec. `FFmpegDownloader` handles transport, SHA-256 verification and installation independently of the provider.

- WinHTTP performs certificate-validated HTTPS using Windows TLS and system proxy discovery; HTTPS-to-HTTP redirection is prohibited.
- Three download attempts, bounded network timeouts, exact byte progress where Content-Length is available, 512 MiB archive limit, and cancellation checks.
- At least 1 GiB free space is required before extraction.
- The archive is extracted only after SHA-256 matches, using Windows' built-in `tar.exe`. Archive paths are checked for traversal/absolute paths before extraction.
- A uniquely named staging directory is used. Both executables and an actual ProRes/float smoke test must pass before publication to the version directory.
- The provider's LICENSE, README, documentation and binaries are kept together. No global PATH, system environment, registry installation, Program Files, service or administrator changes occur.
- Settings and logs remain per-user. No videos are sent to the provider.

Interrupted/failed installers may leave a clearly named `install-PID-ticks` staging directory for diagnosis. It is not treated as an installed tool. V1 does not delete arbitrary older tool directories or user-selected installations. A maintainer should review retained staging files when diagnosing an installation failure.

## Redistribution

LogForge's MIT license covers its own source, not the independently obtained FFmpeg binary or its enabled libraries. The portable ZIP intentionally excludes FFmpeg. If downstream distributors bundle FFmpeg, they must separately satisfy its GPL and component obligations, including corresponding source as applicable. Preserve the provider's notices. See FFmpeg's own [license discussion](https://ffmpeg.org/legal.html).

Manual builds are supported if the capabilities pass. Their origin, updates and licensing remain the user's responsibility. No private Apple encoder is downloaded or impersonated.
