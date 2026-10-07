# LogForge iOS 1.3.1

This is an iOS patch release. The Windows project stays at 1.3.0 (26929A), and
the existing v1.3.0 tag and historical release assets remain unchanged.
The app and download filenames show the iOS version without an alphanumeric
release build. Apple still requires a numeric `CFBundleVersion`; it remains in
technical diagnostics to identify the exact binary used for a conversion.

## AAC and timecode preservation

A supplied batch report for `A001_10071348_C002.mov` shows Writer passthrough
changing AAC from 111 to 112 samples and moving its start from 2048/48000 seconds
to zero. The timecode sample duration also differs. The media file itself has
not been supplied, so this report does not establish its complete MOV structure.

When strict track validation detects a mismatch and the common timing offset is
zero, the native pipeline restores the original retained tracks' stored chunks,
sample descriptions, timing tables and edit lists into the unpublished MOV.
It copies only retained chunks using a bounded buffer; it does not copy the
source video or invoke an external program. Chunk offsets and movie-clock header
durations are updated, while the retained media clocks and edit media times are
preserved. Timecode/chapter associations retain the original track IDs.

Both video validation and strict audio/timecode validation run again before
publication. The checks are not weakened. External media references, fragmented
MOVs and invalid chunk tables are not eligible for restoration. Nonzero common
timeline shifts retain the existing passthrough path and strict validation.

## License navigation and version display

The license destination caches bundled text and displays paragraphs lazily.
The glass panel is applied to the fixed scroll viewport rather than the full
document's changing layout height, addressing the reported flashing white
background. The settings sheet keeps a consistent background during navigation;
license text retains selection and glass styling. A navigation regression opens,
scrolls and closes this screen repeatedly. Visual flicker still requires a
physical-device check.

The homepage and settings show **1.3.1**. iOS marketing versions are taken from
the native Xcode project instead of being overwritten by Windows CMake.
Packaging produces `LogForge-1.3.1-iOS-unsigned.ipa`, its SHA-256,
`LogForge-1.3.1-iOS-RelinkKit.zip` and its SHA-256. The IPA needs a valid Apple
signature and provisioning profile before installation.

## Verification

Windows static checks cover Swift grammar, project membership, plist/localization
consistency, release identity, actual Bash packaging text and the Actions workflow.
They do not compile Swift or execute Apple frameworks. Cloud XCTest and archive
results must be checked in [the main iOS workflow](https://github.com/HongHuo0728/LogForge/actions/workflows/build-ios.yml).
The generated test movie reproduces the reported AAC count and start time; it is
not the user's original recording. Device performance, visual transitions and
editor recognition remain separate checks.

Apple references: [AAC encoder delay](https://developer.apple.com/documentation/quicktime-file-format/background_aac_encoding),
[sample-to-chunk tables](https://developer.apple.com/documentation/quicktime-file-format/sample-to-chunk_atom),
[edit lists](https://developer.apple.com/documentation/quicktime-file-format/edit_list_atom),
[chunk offsets](https://developer.apple.com/documentation/quicktime-file-format/chunk_offset_atom).
