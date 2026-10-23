"""Release routing regressions; no SDK, compiler, GPU or network required."""
import importlib.util
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / '.github/scripts/release_target.py'
spec = importlib.util.spec_from_file_location('release_target', SCRIPT)
release = importlib.util.module_from_spec(spec)
spec.loader.exec_module(release)


class ReleaseTargetTests(unittest.TestCase):
    def versions(self, platform):
        return {'windows': '1.3.0', 'ios': '1.3.1'}[platform]

    def test_ios_only_tag_skips_windows_without_renumbering(self):
        selected, version, reason = release.select_target('windows', 'tag', 'v1.3.1', self.versions)
        self.assertFalse(selected)
        self.assertEqual(version, '1.3.0')
        self.assertIn('ios', reason)

    def test_ios_only_tag_builds_ios(self):
        self.assertEqual(release.select_target('ios', 'tag', 'v1.3.1', self.versions)[:2], (True, '1.3.1'))

    def test_windows_tag_builds_windows_and_skips_ios(self):
        self.assertTrue(release.select_target('windows', 'tag', 'v1.3.0', self.versions)[0])
        self.assertFalse(release.select_target('ios', 'tag', 'v1.3.0', self.versions)[0])

    def test_shared_version_builds_both(self):
        for platform in release.PLATFORMS:
            self.assertTrue(release.select_target(platform, 'tag', 'v2.0.0', lambda _: '2.0.0')[0])

    def test_ios_132_reuses_windows_130_without_rebuilding_it(self):
        versions = lambda platform: {'ios':'1.3.2', 'windows':'1.3.0'}[platform]
        self.assertTrue(release.select_target('ios', 'tag', 'v1.3.2', versions)[0])
        self.assertFalse(release.select_target('windows', 'tag', 'v1.3.2', versions)[0])

    def test_unknown_release_version_is_an_error(self):
        for platform in release.PLATFORMS:
            with self.assertRaisesRegex(ValueError, 'matches neither'):
                release.select_target(platform, 'tag', 'v9.9.9', self.versions)

    def test_malformed_tags_are_errors(self):
        for tag in ('v1.3', 'v1.3.1-extra', 'v1.3.1\n', '1.3.1'):
            with self.subTest(tag=tag), self.assertRaises(ValueError):
                release.select_target('windows', 'tag', tag, self.versions)

    def test_non_tag_builds_do_not_depend_on_other_platform(self):
        for platform in release.PLATFORMS:
            def own_version(asked):
                self.assertEqual(asked, platform)
                return self.versions(asked)
            for ref in ('main', 'feature/example', '123/merge'):
                self.assertTrue(release.select_target(platform, 'branch', ref, own_version)[0])

    def test_real_source_versions_and_github_outputs(self):
        windows = release.platform_version('windows')
        ios = release.platform_version('ios')
        self.assertRegex(windows, r'^\d+\.\d+\.\d+$')
        self.assertRegex(ios, r'^\d+\.\d+\.\d+$')
        with tempfile.TemporaryDirectory(prefix='logforge-release-') as temporary:
            output = Path(temporary) / 'outputs'
            env = dict(os.environ, GITHUB_REF_TYPE='tag', GITHUB_REF_NAME=f'v{ios}', GITHUB_OUTPUT=str(output))
            done = subprocess.run([sys.executable, str(SCRIPT), '--platform', 'windows'], env=env,
                                  capture_output=True, text=True)
            self.assertEqual(done.returncode, 0, done.stderr)
            self.assertEqual(output.read_text(encoding='utf-8'),
                             f'should_build={str(windows == ios).lower()}\nversion={windows}\n')
            output.unlink()
            env['GITHUB_REF_NAME'] = 'v9999.9999.9999'
            done = subprocess.run([sys.executable, str(SCRIPT), '--platform', 'windows'], env=env,
                                  capture_output=True, text=True)
            self.assertNotEqual(done.returncode, 0)
            self.assertFalse(output.exists(), 'An invalid tag published successful job outputs')


if __name__ == '__main__':
    unittest.main(verbosity=2)
