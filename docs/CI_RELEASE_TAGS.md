# Platform release tags

Windows and iOS have independent release versions. Windows remains **1.3.0
(26929A)**. **1.3.1 (26107B) is an iOS-only release**; Windows skips 1.3.1.

Both workflows first run `.github/scripts/release_target.py` on a small Linux
runner, before allocating a Windows or macOS build runner. The selector reads
the Windows version from `CMakeLists.txt` and the iOS version through the existing
`LogForgeMac/tools/release-info.py` identity reader. It does not change either
application's version or release build.

- A `vX.Y.Z` tag matching the selected platform builds that platform.
- A tag matching only the other platform skips this platform's build job.
- If both platforms have the same version, both build.
- A malformed tag or a version belonging to neither platform fails selection.
- Branch, pull-request and manual builds continue to build the selected platform.
  They do not require its version to match the other platform.

Consequently, with the current source, `v1.3.1` runs the iOS tests/archive and
skips the Windows compiler/tests/package job. `v1.3.0` selects Windows instead.
The existing per-platform tag checks, tests, package audits and checksums remain
enabled whenever that platform is selected. Nothing relabels a Windows 1.3.0
binary as 1.3.1.

Run `python tests/release_target.py` to exercise the routing contract, including
the iOS-only tag, shared-version releases, invalid tags, independent branch
builds and the actual GitHub job output file.

The October 7 Windows run `37646409418` compiled and passed all 27 hosted-runner
CTest cases, then failed because its old packaging check compared iOS tag
`v1.3.1` against Windows CMake version `1.3.0`. The corresponding iOS run
`37646409635` succeeded. Existing historical runs/tags are retained; corrected
workflow code is verified in a new run on the repair commit.
