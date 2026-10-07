"""Resolve iOS release identity; verify the built Apple bundle before packaging."""
import argparse
import json
from pathlib import Path
import plistlib
import re


def release_identity(native_root):
    project = native_root / 'LogForge For iPhone/LogForge For iPhone.xcodeproj/project.pbxproj'
    text = project.read_text(encoding='utf-8')
    versions = set(re.findall(r'\bMARKETING_VERSION = ([0-9]+\.[0-9]+\.[0-9]+);', text))
    assignments = re.findall(r'\bMARKETING_VERSION\s*=\s*([^;]+);', text)
    if len(versions) != 1 or any(not re.fullmatch(r'[0-9]+\.[0-9]+\.[0-9]+', value.strip()) for value in assignments):
        raise ValueError('Xcode targets must share one valid iOS release version')
    # iOS patch releases are independent of the Windows/CMake version.
    return versions.pop()


def release_build(native_root):
    project = native_root / 'LogForge For iPhone/LogForge For iPhone.xcodeproj/project.pbxproj'
    text = project.read_text(encoding='utf-8')
    assignments = re.findall(r'\bLOGFORGE_RELEASE_BUILD\s*=\s*([^;]+);', text)
    versions = re.findall(r'\bMARKETING_VERSION\s*=\s*([^;]+);', text)
    values = {value.strip() for value in assignments}
    if len(assignments) != len(versions) or len(values) != 1 or any(not re.fullmatch(r'[0-9]{5}[A-Z]', value) for value in values):
        raise ValueError('Xcode targets must share one valid iOS release build')
    return values.pop()


def verify_bundle(info, version, bundle_build, release_build_code):
    expected = {'CFBundleShortVersionString': version, 'CFBundleVersion': bundle_build,
                'LogForgeReleaseBuild': release_build_code}
    for key, value in expected.items():
        if info.get(key) != value:
            raise ValueError(f'Built bundle {key}: {info.get(key)!r}, expected {value!r}')
    if not re.fullmatch(r'[1-9][0-9]*(?:\.[0-9]+){0,2}', bundle_build):
        raise ValueError('Apple bundle build must be a positive numeric version')


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--root', type=Path, default=Path(__file__).resolve().parent.parent)
    parser.add_argument('--release-build', action='store_true', help='Print the public release build instead of the version')
    parser.add_argument('--app-info', type=Path)
    parser.add_argument('--bundle-build')
    parser.add_argument('--manifest', type=Path)
    args = parser.parse_args()
    version = release_identity(args.root)
    release_build_code = release_build(args.root)
    if args.app_info:
        if not args.bundle_build:
            parser.error('--app-info requires --bundle-build')
        verify_bundle(plistlib.loads(args.app_info.read_bytes()), version, args.bundle_build, release_build_code)
    if args.manifest:
        if not args.app_info:
            parser.error('--manifest requires a verified --app-info')
        args.manifest.write_text(json.dumps({'version': version, 'releaseBuild': release_build_code,
            'bundleBuild': args.bundle_build, 'platform': 'iOS', 'signed': False}, indent=2) + '\n', encoding='utf-8')
    print(release_build_code if args.release_build else version)


if __name__ == '__main__':
    main()
