# Contributing

Use C++20, CMake and MSVC x64. Run the tests described in the README before submitting a change. The `.clang-format` file defines the code layout.

Keep public documentation in English. Add both English and Simplified Chinese entries to `Messages.inc` for application-owned text, retaining stable message IDs instead of parsing translated strings. Update tests when placeholders change. The first-run defaults are English, dark theme and creative adjustments off. Save/Cancel semantics must preserve the selected input, output, exposure and unrelated settings.

Version and build identifiers live in CMake and generate the C++/Windows resources. Do not hand-edit generated files. Resource tests verify both executables. On an interactive Windows desktop, `tests/gui_smoke.py` exercises both languages/themes, multiple DPI sizes, settings Save/Cancel and optional generated-video conversion. The missing-FFmpeg UI case is explicitly injected; real discovery/capability tests run independently through CTest.

Use `tests/package_audit.py` after CPack. Packaging allows only the GUI EXE and explicitly listed public docs/licenses/screenshot. `.gitignore` excludes builds, packages, tool binaries, preferences, logs and generated media; `.git` is the existing repository database, not an upload filter. Review `git status` and `git diff --cached` before committing, and never force-add private files.

Color changes need an inspectable primary source, double reference points, edge/knee/range tests, and end-to-end pixel measurements. Visual resemblance is not a numerical test. Never substitute a generic log transfer ID, an HLG tag or a device-model string for an Apple Log transform.

FFmpeg provider updates must replace the pinned URL, archive root, version and SHA-256 together, retain license information, and pass the actual installed-binary capability and generated-media suite. Do not bundle FFmpeg in the portable release by accident.

Do not add guessed private MOV atoms. Document reference provenance and editor behavior before making compatibility changes. Keep device/compatibility metadata modes separate from pixel processing.

Tests generate synthetic media under the ignored build directory. Do not commit personal recordings, extracted frames, audio, or raw metadata, or include them in release packages. Keep any consented private reference material outside the project and delete local test derivatives after use. Documentation screenshots must use synthetic fixtures. The media ignore rules reduce accidental additions; they do not sanitize files or make `git add -f` safe.

Logs and reference reports contain file paths and metadata. Remove personal data, location tags, device identifiers and private-media fingerprints before attaching them to an issue. Include the LogForge/FFmpeg versions, source stream properties, a minimal synthetic reproducible test and a redacted validation report when reporting a defect.
