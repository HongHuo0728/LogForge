"""Windows-compatible checks for main-repository iOS release packaging identity."""
from pathlib import Path
import importlib.util
import tempfile
import unittest
spec = importlib.util.spec_from_file_location('logforge_release_info', Path(__file__).with_name('release-info.py'))
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
release_identity, verify_bundle = module.release_identity, module.verify_bundle


class ReleaseIdentityTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.main = Path(self.temp.name)
        self.native = self.main / 'LogForgeMac'
        project = self.native / 'LogForge For iPhone/LogForge For iPhone.xcodeproj/project.pbxproj'
        project.parent.mkdir(parents=True)
        project.write_text('MARKETING_VERSION = 1.3.0;\nLOGFORGE_RELEASE_BUILD = 26106A;\n', encoding='utf-8')

    def test_standalone_project_defaults(self):
        self.assertEqual(release_identity(self.native), ('1.3.0', '26106A'))

    def test_main_windows_version_is_authoritative(self):
        (self.main / 'CMakeLists.txt').write_text('project(LogForge VERSION 1.4.0 LANGUAGES CXX)', encoding='utf-8')
        self.assertEqual(release_identity(self.native), ('1.4.0', '26106A'))

    def test_malformed_main_version_cannot_silently_fall_back(self):
        (self.main / 'CMakeLists.txt').write_text('project(LogForge VERSION invalid)', encoding='utf-8')
        with self.assertRaises(ValueError):
            release_identity(self.native)

    def test_built_bundle_identity_and_numeric_build(self):
        info = {'CFBundleShortVersionString':'1.3.0', 'LogForgeReleaseBuild':'26106A', 'CFBundleVersion':'20'}
        verify_bundle(info, '1.3.0', '26106A', '20')
        for key in info:
            bad = dict(info); bad[key] = 'wrong'
            with self.subTest(key=key), self.assertRaises(ValueError):
                verify_bundle(bad, '1.3.0', '26106A', '20')
        for value in ['26106A', '0', '', '1.2.3.4']:
            bad = dict(info); bad['CFBundleVersion'] = value
            with self.subTest(value=value), self.assertRaises(ValueError):
                verify_bundle(bad, '1.3.0', '26106A', value)


if __name__ == '__main__':
    unittest.main()
