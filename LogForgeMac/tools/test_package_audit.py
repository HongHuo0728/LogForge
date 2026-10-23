"""Reject corrupt/relabelled payloads even when their filenames look correct."""
import importlib.util
import json
from pathlib import Path
import plistlib
import struct
import tempfile
import unittest
import zipfile

spec = importlib.util.spec_from_file_location('package_audit', Path(__file__).with_name('audit-package.py'))
audit = importlib.util.module_from_spec(spec)
spec.loader.exec_module(audit)


class PackageAuditTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        project = self.root / 'LogForge For iPhone/LogForge For iPhone.xcodeproj/project.pbxproj'
        project.parent.mkdir(parents=True)
        project.write_text('MARKETING_VERSION = 1.3.2;\nLOGFORGE_RELEASE_BUILD = 261023A;\n')
        self.output = self.root / 'artifacts'
        self.output.mkdir()
        self.ipa = self.output / 'LogForge-1.3.2-iOS-261023A-unsigned.ipa'
        self.kit = self.output / 'LogForge-1.3.2-iOS-261023A-RelinkKit.zip'
        self.info = {'CFBundleShortVersionString':'1.3.2', 'CFBundleVersion':'12',
            'LogForgeReleaseBuild':'261023A', 'CFBundleIdentifier':'Shizhihe.LogForge-For-iPhone',
            'CFBundleExecutable':'LogForge', 'MinimumOSVersion':'26.0',
            'DTSDKName':'iphoneos27.1', 'DTXcodeBuild':'27A9275'}
        (self.output / 'release-info.json').write_text(json.dumps({'version':'1.3.2',
            'releaseBuild':'261023A', 'bundleBuild':'12', 'platform':'iOS', 'signed':False}))
        self.write_ipa()
        self.write_kit()

    def checksum(self, path):
        path.with_name(path.name+'.sha256').write_text(audit.digest(path)+'  '+path.name+'\n')

    def write_ipa(self, cpu=0x0100000c, extra=None):
        with zipfile.ZipFile(self.ipa, 'w') as z:
            z.writestr('Payload/LogForge.app/Info.plist', plistlib.dumps(self.info))
            z.writestr('Payload/LogForge.app/LogForge', struct.pack('<II', 0xfeedfacf, cpu)+bytes(24))
            if extra:
                z.writestr(extra, 'test fixture')
        self.checksum(self.ipa)

    def write_kit(self, omit=None):
        files = ['RELINK.txt', 'SoftwareCodec-LICENSE.txt', 'Source/project.pbxproj',
            'Source/ffmpeg-8.1.2.tar.xz', 'Source/NativeCodec/build-codec.sh',
            'Libraries/LogForgeCodec/lib/libLogForgeFFmpeg.a', 'archive.log',
            'xcode-version.txt', 'AppObjects/arm64/ContentView.o']
        with zipfile.ZipFile(self.kit, 'w') as z:
            for name in files:
                if name != omit:
                    z.writestr('RelinkKit/'+name, b'fixture')
        self.checksum(self.kit)

    def test_valid_identity_and_four_hashed_ios_assets(self):
        result = audit.audit(self.output, self.root)
        self.assertTrue(result['passed'])
        self.assertEqual(len(result['assets']), 4)
        self.assertEqual(result['releaseBuild'], '261023A')

    def test_new_filename_does_not_hide_old_binary(self):
        self.info['CFBundleShortVersionString'] = '1.3.1'
        self.write_ipa()
        with self.assertRaisesRegex(ValueError, 'CFBundleShortVersionString'):
            audit.audit(self.output, self.root)

    def test_wrong_sdk_deployment_or_architecture_rejected(self):
        for key, value in [('DTSDKName','iphonesimulator27.1'), ('MinimumOSVersion','27.1'),
                           ('DTXcodeBuild','27A1234')]:
            original = self.info[key]
            self.info[key] = value
            self.write_ipa()
            with self.subTest(key=key), self.assertRaises(ValueError):
                audit.audit(self.output, self.root)
            self.info[key] = original
        self.write_ipa(cpu=0x01000007)
        with self.assertRaisesRegex(ValueError, 'arm64'):
            audit.audit(self.output, self.root)

    def test_corrupt_or_misnamed_checksum_rejected(self):
        checksum = self.ipa.with_name(self.ipa.name+'.sha256')
        checksum.write_text(audit.digest(self.ipa)+'  Wrong.ipa\n')
        with self.assertRaisesRegex(ValueError, 'checksum'):
            audit.audit(self.output, self.root)
        self.checksum(self.ipa)
        with self.ipa.open('ab') as stream:
            stream.write(b'changed after hashing')
        with self.assertRaisesRegex(ValueError, 'checksum'):
            audit.audit(self.output, self.root)

    def test_relink_source_and_objects_are_mandatory(self):
        for missing in ['Source/ffmpeg-8.1.2.tar.xz', 'AppObjects/arm64/ContentView.o']:
            self.write_kit(omit=missing)
            with self.subTest(missing=missing), self.assertRaisesRegex(ValueError, 'RelinkKit missing'):
                audit.audit(self.output, self.root)

    def test_test_bundle_must_not_ship(self):
        self.write_ipa(extra='Payload/LogForge.app/PlugIns/Tests.xctest/Tests')
        with self.assertRaisesRegex(ValueError, 'Test payload'):
            audit.audit(self.output, self.root)


if __name__ == '__main__':
    unittest.main()
