"""Audit the portable ZIP's publication boundary without extracting media."""
import argparse
import hashlib
from pathlib import Path
from zipfile import ZipFile


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--zip', type=Path, required=True)
    parser.add_argument('--exe', type=Path, required=True)
    args = parser.parse_args()
    expected = {'LogForge.exe', 'README.md', 'CHANGELOG.md', 'LICENSE', 'THIRD_PARTY_NOTICES.md',
                'licenses/nlohmann-json-MIT.txt', 'docs/images/LogForge.png'}
    expected |= {f'docs/{name}.md' for name in (
        'ARCHITECTURE', 'COLOR_PIPELINE', 'CREATIVE_ADJUSTMENTS', 'FFMPEG_PROVIDER',
        'ICON', 'METADATA', 'TECHNICAL_REFERENCES', 'VALIDATION', 'APPLE_LOG_IDENTIFICATION')}
    with ZipFile(args.zip) as archive:
        files = [entry for entry in archive.infolist() if not entry.is_dir()]
        names = [entry.filename for entry in files]
        assert len(set(names)) == len(names), 'Duplicate archive entries'
        assert set(names) == expected, f'Unexpected/missing package files: {set(names) ^ expected}'
        assert sum(entry.file_size for entry in files) < 16 * 1024 * 1024, 'Unexpected package size'
        assert archive.testzip() is None, 'Archive CRC failure'
        packaged = hashlib.sha256(archive.read('LogForge.exe')).hexdigest()
        tested = hashlib.sha256(args.exe.read_bytes()).hexdigest()
        assert packaged == tested, 'Packaged EXE differs from tested Release EXE'
    print(f'PASS: {len(files)} allowed files; tested EXE identical; no FFmpeg, personal media, logs or settings')
    print('ZIP SHA-256:', hashlib.sha256(args.zip.read_bytes()).hexdigest())


if __name__ == '__main__':
    main()
