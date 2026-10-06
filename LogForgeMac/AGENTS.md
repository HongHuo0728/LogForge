# LogForge native iOS project

These instructions apply to this directory and its descendants.

- Preserve the existing Swift / SwiftUI / Metal architecture. Do not migrate to React Native, Expo, or a web implementation.
- Make future changes directly in the existing native project. Preserve existing features and retain currently unconnected modules unless removal is explicitly requested.
- Before editing, find the real entry point, callers, data formats, and Xcode target membership. Read `docs/NATIVE_ARCHITECTURE_REVIEW_2026-10-04.md` for the initial map, then verify it against current source.
- Development currently runs on Windows. Editing Swift, Metal, plist, localization, and Xcode project text is supported; do not assume Xcode, an Apple SDK, an iOS simulator, or an Apple GPU is installed.
- Perform relevant static checks. Clearly distinguish static inspection and independent formula calculations from Swift compilation, Metal execution, device encoding, and editor compatibility tests. Mark Apple-platform checks that have not been performed.
- Keep GPU and CPU fallback color processing consistent. Check range, reference-white normalization, chroma sampling, V210 stride/packing, timing, and pixel-buffer ownership when changing the pipeline.
- Treat Apple Log pixel conversion, frame attachments, encoded format descriptions, MOV serialization, and editor recognition as separate validation layers. Do not invent private metadata or claim recognition from attachment names alone.
- This native project lives inside the parent Windows/C++ repository. Do not apply the parent's C++ implementation assumptions to this iOS target, and do not modify unrelated parent code for an iOS task.
- `__MACOSX/` contains archive sidecars, not implementation sources. Preserve it; make changes in `LogForge For iPhone/`.
