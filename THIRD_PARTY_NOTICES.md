# Third-party notices

## nlohmann/json 3.12.0

- Source: https://github.com/nlohmann/json/tree/v3.12.0
- Vendored file: `third_party/nlohmann/json.hpp`
- License: MIT, copyright Niels Lohmann and contributors.
- Full license: `third_party/nlohmann/LICENSE.MIT`, also included in the portable package.

## FFmpeg / Gyan.dev Windows essentials 8.1.2

- Downloaded on demand as an independent executable; not included in this repository or the portable release and not linked to LogForge.
- Provider: https://www.gyan.dev/ffmpeg/builds/
- Provider repository: https://github.com/GyanD/codexffmpeg
- FFmpeg source: https://github.com/FFmpeg/FFmpeg/tree/n8.1.2
- This build enables GPL and version 3. Its provider labels it GPLv3. FFmpeg and bundled libraries retain their respective licenses; the archive's complete `LICENSE`, `README.txt` and documentation are retained by installation.
- FFmpeg license information: https://ffmpeg.org/legal.html
- The MIT license for LogForge does not cover FFmpeg. Anyone separately redistributing FFmpeg must meet the applicable license requirements, including corresponding source requirements. A link in this document is not a substitute for those obligations.

## Color standards and reference material

Apple's vendor-supplied ACES input transform and ITU specifications were consulted to implement the mathematical functions. LogForge does not bundle Apple LUTs, Apple logos, Apple media samples, or Apple SDK code. Technical references and the retrieval limitations are recorded in `docs/TECHNICAL_REFERENCES.md`.

## Windows components

Win32, Common Controls, WinHTTP, BCrypt and Windows Imaging Component are operating-system APIs. MSVC Release code uses the static runtime. Visual Studio and the Windows SDK are build prerequisites, not project-distributed dependencies.

## NVIDIA CUDA compiler-generated device code

The CUDA kernel source is LogForge project code. The checked-in PTX is generated
by NVIDIA NVRTC 12.8 with precise math options and may contain NVIDIA device-math
implementation portions. Those portions are not relicensed under LogForge's MIT
license. See the bundled `licenses/NVIDIA-CUDA-NOTICE.txt` and the
[CUDA 12.8.1 EULA](https://docs.nvidia.com/cuda/archive/12.8.1/eula/index.html).
The portable package places the notice under `licenses/NVIDIA-CUDA-NOTICE.txt`.

No NVIDIA Toolkit, NVRTC, cudart or driver DLL is redistributed. Runtime loads the
installed system NVIDIA CUDA driver. Compiler provenance, options and source/PTX
SHA-256 values are in `src/color/ColorKernel.manifest.json`; regeneration requires
an explicitly supplied licensed NVRTC installation. Running LogForge does not
require that development compiler or DaVinci Resolve.
