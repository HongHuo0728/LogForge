"""Ensure lightweight CI cannot masquerade as a posture-verified release."""
import importlib.util
import json
import os
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location('verification', Path(__file__).with_name('collect-verification.py'))
verification = importlib.util.module_from_spec(spec)
spec.loader.exec_module(verification)


class VerificationTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        (self.root / 'build/logs/duo').mkdir(parents=True)
        (self.root / 'build/artifacts').mkdir()
        self.summary = {'passedTests':9, 'failedTests':0}
        self.pose = {'passed':True, 'syntheticLayout':False, 'commit':'verified-commit',
                     'poses':[{'name':name, 'passed':True} for name in
                              ('open-landscape','open-portrait','book','tabletop',
                               'closed-outer','closed-outer-landscape')]}

    def collect(self, core=False):
        with patch.dict(os.environ, {'GITHUB_SHA':'verified-commit', 'GITHUB_RUN_ID':'21',
                                      'LOGFORGE_COMPAT_SUITE':'release-smoke'}), \
             patch.object(verification.subprocess, 'check_output', return_value=json.dumps(self.summary)):
            return verification.collect(self.root, core)

    def save_poses(self):
        (self.root / 'build/logs/duo/results.json').write_text(json.dumps(self.pose))

    def test_core_success_is_not_release_evidence(self):
        result = self.collect(core=True)
        self.assertTrue(result['passed'])
        self.assertFalse(result['releaseReady'])
        self.assertEqual(result['duo']['status'], 'not_run')

    def test_failed_core_tests_still_fail_ci(self):
        self.summary['failedTests'] = 1
        with self.assertRaisesRegex(SystemExit, 'XCTest results missing or failed'):
            self.collect(core=True)
        self.assertFalse((self.root / 'build/artifacts/verification.json').exists())

    def test_zero_executed_tests_cannot_pass(self):
        self.summary['passedTests'] = 0
        with self.assertRaises(SystemExit):
            self.collect(core=True)

    def test_release_requires_all_real_poses(self):
        self.save_poses()
        self.assertTrue(self.collect()['releaseReady'])
        self.pose['poses'].pop()
        self.save_poses()
        with self.assertRaisesRegex(SystemExit, 'real simulator poses'):
            self.collect()

    def test_synthetic_poses_cannot_be_release_evidence(self):
        self.pose['syntheticLayout'] = True
        self.save_poses()
        with self.assertRaises(SystemExit):
            self.collect()


if __name__ == '__main__':
    unittest.main()
