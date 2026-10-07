"""Windows-compatible checks for main-repository iOS release packaging identity."""
from pathlib import Path
import importlib.util
import os
import re
import shutil
import subprocess
import tempfile
import unittest
spec = importlib.util.spec_from_file_location('logforge_release_info', Path(__file__).with_name('release-info.py'))
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
release_identity, verify_bundle = module.release_identity, module.verify_bundle
release_build = module.release_build


class ReleaseIdentityTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.main = Path(self.temp.name)
        self.native = self.main / 'LogForgeMac'
        project = self.native / 'LogForge For iPhone/LogForge For iPhone.xcodeproj/project.pbxproj'
        project.parent.mkdir(parents=True)
        project.write_text('MARKETING_VERSION = 1.3.1;\nLOGFORGE_RELEASE_BUILD = 26107B;\n', encoding='utf-8')

    def test_standalone_project_defaults(self):
        self.assertEqual(release_identity(self.native), '1.3.1')
        self.assertEqual(release_build(self.native), '26107B')

    def test_ios_patch_version_is_independent_of_windows(self):
        (self.main / 'CMakeLists.txt').write_text('project(LogForge VERSION 1.4.0 LANGUAGES CXX)', encoding='utf-8')
        self.assertEqual(release_identity(self.native), '1.3.1')

    def test_malformed_ios_version_cannot_silently_fall_back(self):
        project = self.native / 'LogForge For iPhone/LogForge For iPhone.xcodeproj/project.pbxproj'
        project.write_text('MARKETING_VERSION = invalid;', encoding='utf-8')
        with self.assertRaises(ValueError):
            release_identity(self.native)

    def test_built_bundle_identity_and_numeric_build(self):
        info = {'CFBundleShortVersionString':'1.3.1', 'CFBundleVersion':'20', 'LogForgeReleaseBuild':'26107B'}
        verify_bundle(info, '1.3.1', '20', '26107B')
        for key in info:
            bad = dict(info); bad[key] = 'wrong'
            with self.subTest(key=key), self.assertRaises(ValueError):
                verify_bundle(bad, '1.3.1', '20', '26107B')
        for value in ['26107B', '0', '', '1.2.3.4']:
            bad = dict(info); bad['CFBundleVersion'] = value
            with self.subTest(value=value), self.assertRaises(ValueError):
                verify_bundle(bad, '1.3.1', value, '26107B')

    def test_missing_invalid_or_inconsistent_release_build_is_rejected(self):
        project = self.native / 'LogForge For iPhone/LogForge For iPhone.xcodeproj/project.pbxproj'
        for value in ['', 'LOGFORGE_RELEASE_BUILD = invalid;',
                      'LOGFORGE_RELEASE_BUILD = 26107B;\nMARKETING_VERSION = 1.3.1;',
                      'LOGFORGE_RELEASE_BUILD = 26107B;\nMARKETING_VERSION = 1.3.1;\nLOGFORGE_RELEASE_BUILD = 26106A;']:
            project.write_text('MARKETING_VERSION = 1.3.1;\n'+value, encoding='utf-8')
            with self.subTest(value=value), self.assertRaises(ValueError):
                release_build(self.native)

    def test_cli_verifies_bundle_and_writes_public_build_manifest(self):
        import json
        import plistlib
        import sys
        info = self.main / 'Info.plist'
        info.write_bytes(plistlib.dumps({'CFBundleShortVersionString':'1.3.1',
            'CFBundleVersion':'20', 'LogForgeReleaseBuild':'26107B'}))
        manifest = self.main / 'release-info.json'
        result = subprocess.run([sys.executable, str(Path(__file__).with_name('release-info.py')),
            '--root', str(self.native), '--app-info', str(info), '--bundle-build', '20',
            '--manifest', str(manifest), '--release-build'], capture_output=True, text=True, check=True)
        self.assertEqual(result.stdout.strip(), '26107B')
        self.assertEqual(json.loads(manifest.read_text()), {'version':'1.3.1',
            'releaseBuild':'26107B', 'bundleBuild':'20', 'platform':'iOS', 'signed':False})

    def test_actual_package_readme_renders_both_languages(self):
        script = Path(__file__).with_name('build-ios.sh').read_text(encoding='utf-8')
        match = re.search(r'(?m)^    cat > build/artifacts/READ-ME.txt <<EOF\n(.*?)^EOF$', script, re.S)
        self.assertIsNotNone(match, 'Missing production package README template')
        template = match.group(1)
        # ASCII identifiers must use braces when followed by Chinese punctuation.
        self.assertIsNone(re.search(r'\$[A-Za-z_][A-Za-z0-9_]*[^\x00-\x7f]', template))
        artifacts = self.main / 'build/artifacts'
        artifacts.mkdir(parents=True)
        env = os.environ.copy()
        env.update(version='1.3.1', release_build='26107B', bundle_build='20',
                   ipa_name='LogForge-1.3.1-iOS-26107B-unsigned.ipa')
        bash = env.get('LOGFORGE_TEST_BASH') or shutil.which('bash')
        self.assertIsNotNone(bash, 'Bash is required to exercise packaging')
        subprocess.run([bash, '-euo', 'pipefail'], input=match.group(0)+'\n',
                       text=True, encoding='utf-8', cwd=self.main, env=env, check=True,
                       capture_output=True)
        text = (artifacts / 'READ-ME.txt').read_text(encoding='utf-8')
        self.assertIn('LogForge iOS 1.3.1 (26107B).', text)
        self.assertIn('LogForge iOS 1.3.1 (26107B)。', text)
        self.assertIn(env['ipa_name'], text)
        self.assertNotIn('$', text)


if __name__ == '__main__':
    unittest.main()
