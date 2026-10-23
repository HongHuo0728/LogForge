# Platform release tags

Windows and iOS have independent release versions. Windows remains **1.3.0
(26929A)**. **1.3.2 (261023A) is an iOS-only update**; Windows skips its build.

Both workflows first run `.github/scripts/release_target.py` on a small Linux
runner, before allocating a Windows or macOS build runner. The selector reads
the Windows version from `CMakeLists.txt` and the iOS version through the existing
`LogForgeMac/tools/release-info.py` identity reader. It does not change either
application's version or release build.

- A `vX.Y.Z` tag matching the selected platform selects that platform's release route.
- A tag matching only the other platform skips this platform's build job.
- If both platforms have the same version, both build.
- A malformed tag or a version belonging to neither platform fails selection.
- Branch, pull-request and manual builds continue to build the selected platform.
  They do not require its version to match the other platform.

Consequently, with the current source, `v1.3.2` promotes verified iOS artifacts and
skips the Windows compiler/tests/package job. `v1.3.0` selects Windows instead.
The existing per-platform tag checks, tests, package audits and checksums remain
enabled whenever that platform is selected. Nothing relabels a Windows 1.3.0
binary as 1.3.2.

Ordinary iOS commits run source checks, a device archive/package audit and nine
layout/import unit tests. They do not install Appium or execute UI/media/posture
matrices. Artifacts use `core` in their name and record `releaseReady: false`.
Windows builds ignore changes limited to iOS, iOS-only workflows and documentation.

Before publishing a new version, run the workflow on `main` with `duo_checks` to
produce release evidence for that exact commit. `extended_checks` additionally
runs the full native/UI suites and iOS 26/iPad compatibility checks. After this
successful run and screenshot/package review, create the tag on the same commit.
The tag rechecks evidence and hashes without rebuilding or retesting. Only the tag's
publication job has `contents: write`; builds retain read-only permissions.
`.github/scripts/publish_ios_release.py` creates a draft, uploads the four new
iOS files, downloads the original Windows ZIP and SHA-256 from **v1.3.1**, checks
their pinned digests, and uploads them without renaming or rebuilding. It downloads
all six draft attachments and compares their hashes before publishing. A failure
leaves the draft unpublished. It never replaces an already published release.

The artifact's `verification.json` and `package-audit.json` must match the tag's
commit and successful main push or manual workflow run. All six required real Duo
poses and the focused native/UI tests must pass; iOS 26/iPad checks are optional.
A core-only green check or geometric test cannot satisfy the real posture gate.
Do not create a new tag to bypass a failed run, or replace a historical tag.

Run `python tests/release_target.py` to exercise the routing contract, including
the iOS-only tag, shared-version releases, invalid tags, independent branch
builds and the actual GitHub job output file.

The October 7 Windows run `37646409418` compiled and passed all 27 hosted-runner
CTest cases, then failed because its old packaging check compared iOS tag
`v1.3.1` against Windows CMake version `1.3.0`. The corresponding iOS run
`37646409635` succeeded. Existing historical runs/tags are retained; corrected
workflow code is verified in a new run on the repair commit.
