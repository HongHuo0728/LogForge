# Contributing

Use C++20, CMake and MSVC x64. Run the tests described in the README before submitting a change. The `.clang-format` file defines the code layout.

Color changes need an inspectable primary source, double reference points, edge/knee/range tests, and end-to-end pixel measurements. Visual resemblance is not a numerical test. Never substitute a generic log transfer ID, an HLG tag or a device-model string for an Apple Log transform.

FFmpeg provider updates must replace the pinned URL, archive root, version and SHA-256 together, retain license information, and pass the actual installed-binary capability and generated-media suite. Do not bundle FFmpeg in the portable release by accident.

Do not add guessed private MOV atoms. Document reference provenance and editor behavior before making compatibility changes. Keep device/compatibility metadata modes separate from pixel processing.

Logs and reference reports contain file paths and metadata. Remove personal data before attaching them to an issue. Include the LogForge/FFmpeg versions, source stream properties, a minimal reproducible test and the validation report when reporting a defect.
