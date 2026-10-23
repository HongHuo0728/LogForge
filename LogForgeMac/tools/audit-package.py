"""Audit actual distribution bytes before creating or publishing a release."""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import plistlib
import re
import struct
import zipfile

spec = importlib.util.spec_from_file_location('release_info', Path(__file__).with_name('release-info.py'))
identity = importlib.util.module_from_spec(spec)
spec.loader.exec_module(identity)


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def verify_checksum(path):
    line = path.with_name(path.name + '.sha256').read_text(encoding='utf-8').strip()
    match = re.fullmatch(r'([a-fA-F0-9]{64})  (.+)', line)
    if not match or match[2] != path.name or match[1].lower() != digest(path):
        raise ValueError(f'Invalid checksum or filename: {path.name}')


def require(condition, message):
    if not condition:
        raise ValueError(message)


def audit(directory, root):
    version, build = identity.release_identity(root), identity.release_build(root)
    prefix = f'LogForge-{version}-iOS-{build}'
    ipa = directory / (prefix + '-unsigned.ipa')
    kit = directory / (prefix + '-RelinkKit.zip')
    manifest = json.loads((directory / 'release-info.json').read_text(encoding='utf-8'))
    require(manifest['version'] == version and manifest['releaseBuild'] == build,
            'Package manifest and source version differ')
    require(manifest['platform'] == 'iOS' and manifest['signed'] is False, 'Unexpected package identity')
    for path in (ipa, kit):
        verify_checksum(path)
    with zipfile.ZipFile(ipa) as archive:
        require(archive.testzip() is None, 'Corrupt IPA')
        names = [n for n in archive.namelist() if not n.startswith('__MACOSX/')]
        infos = [n for n in names if re.fullmatch(r'Payload/[^/]+\.app/Info.plist', n)]
        require(len(infos) == 1, 'IPA must contain exactly one application')
        info = plistlib.loads(archive.read(infos[0]))
        identity.verify_bundle(info, version, str(manifest['bundleBuild']), build)
        require(info.get('CFBundleIdentifier') == 'Shizhihe.LogForge-For-iPhone', 'Unexpected bundle identifier')
        require(info.get('MinimumOSVersion') in ('26.0', '26'), 'Deployment target must remain iOS 26')
        require(info.get('DTSDKName') == 'iphoneos27.1', 'IPA was not built with the iOS 27.1 SDK')
        require(info.get('DTXcodeBuild') == '27A9275', 'IPA was not built with the pinned Xcode')
        require(not any('.xctest/' in n or '/Fixtures/' in n for n in names), 'Test payload leaked into IPA')
        app = infos[0].removesuffix('Info.plist')
        executable = archive.read(app + info['CFBundleExecutable'])
        require(len(executable) >= 32 and struct.unpack('<II', executable[:8]) == (0xfeedfacf, 0x0100000c),
                'Release executable must be a 64-bit arm64 Mach-O')
    with zipfile.ZipFile(kit) as archive:
        require(archive.testzip() is None, 'Corrupt RelinkKit')
        names = archive.namelist()
        for required in ('RelinkKit/RELINK.txt', 'RelinkKit/SoftwareCodec-LICENSE.txt',
                         'RelinkKit/Source/project.pbxproj', 'RelinkKit/Source/ffmpeg-8.1.2.tar.xz',
                         'RelinkKit/Source/NativeCodec/build-codec.sh',
                         'RelinkKit/Libraries/LogForgeCodec/lib/libLogForgeFFmpeg.a',
                         'RelinkKit/archive.log', 'RelinkKit/xcode-version.txt'):
            require(required in names and archive.getinfo(required).file_size > 0,
                    f'RelinkKit missing {required}')
        require(any(n.startswith('RelinkKit/AppObjects/') and n.endswith('.o') for n in names),
                'RelinkKit missing application object files')
    assets = [ipa, ipa.with_name(ipa.name + '.sha256'), kit, kit.with_name(kit.name + '.sha256')]
    return {'version':version, 'releaseBuild':build, 'bundleBuild':manifest['bundleBuild'],
            'commit':os.environ.get('GITHUB_SHA'), 'run':os.environ.get('GITHUB_RUN_ID'),
            'xcode':'27.1 (27A9275)', 'sdk':'iphoneos27.1', 'minimumOS':'26.0',
            'assets':{p.name:{'sha256':digest(p), 'bytes':p.stat().st_size} for p in assets}, 'passed':True}


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('directory', type=Path)
    args = parser.parse_args()
    result = audit(args.directory, Path(__file__).resolve().parents[1])
    (args.directory / 'package-audit.json').write_text(json.dumps(result, indent=2)+'\n', encoding='utf-8')
    print(json.dumps(result, indent=2))
