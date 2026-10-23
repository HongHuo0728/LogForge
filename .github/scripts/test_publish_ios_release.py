"""Exercise release gating and read-back behavior without contacting GitHub."""
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location('publisher', Path(__file__).with_name('publish_ios_release.py'))
publisher = importlib.util.module_from_spec(spec)
spec.loader.exec_module(publisher)


class PublicationTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.directory = Path(temporary.name)
        self.commit = 'a' * 40
        self.environment = {'GITHUB_REPOSITORY':publisher.REPO, 'GITHUB_REF_TYPE':'tag',
            'GITHUB_REF_NAME':'v1.3.2', 'GITHUB_SHA':self.commit, 'GITHUB_RUN_ID':'42'}
        self.package = {'version':'1.3.2', 'xcode':'27.1 (27A9275)', 'bundleBuild':'42',
            'commit':self.commit, 'run':'42', 'passed':True, 'assets':{}}
        for name in ('LogForge-1.3.2-iOS-261023A-unsigned.ipa', 'LogForge-1.3.2-iOS-261023A-RelinkKit.zip'):
            body = ('candidate bytes: '+name).encode()
            (self.directory/name).write_bytes(body)
            (self.directory/(name+'.sha256')).write_text(hashlib.sha256(body).hexdigest()+'  '+name+'\n')
        for path in self.directory.iterdir():
            self.package['assets'][path.name] = {'sha256':publisher.audit.digest(path),'bytes':path.stat().st_size}
        poses = ('open-landscape','open-portrait','book','tabletop','tent-outer','closed-outer',
                 'closed-outer-landscape')
        duo = {'passed':True,'syntheticLayout':False,'commit':self.commit,
               'poses':[{'name':name,'passed':True} for name in poses]}
        for name in ('splitView','keyboard','multiWindow','appearance','fileDrop','conversion'):
            duo[name] = {'passed':True}
        duo['keyboard']['splitVerified'] = True
        self.evidence = {'passed':True,'commit':self.commit,'run':'42','duo':duo,
            'xctest':{role:{'passedTests':5,'failedTests':0} for role in ('duo','phone26','ipad')}}
        name = 'LogForge-1.3.0-Windows-x64.zip'
        body = b'unchanged original Windows 1.3.0 bytes'
        self.windows = {name:body, name+'.sha256':(hashlib.sha256(body).hexdigest()+'  '+name+'\n').encode()}
        self.uploaded = {}
        self.mutations = []
        self.corrupt_readback = False
        self.already_public = False
        self.tag_commit = self.commit
        self.source_corrupt = False
        self.write_evidence()

    def write_evidence(self):
        (self.directory/'package-audit.json').write_text(json.dumps(self.package))
        (self.directory/'verification.json').write_text(json.dumps(self.evidence))

    def api(self, route):
        if route == 'git/ref/tags/v1.3.2':
            return {'object':{'type':'commit','sha':self.tag_commit}}
        if route == 'releases/tags/v1.3.1':
            return {'draft':False,'assets':[{'name':name} for name in self.windows]}
        if route == 'releases/tags/v1.3.2':
            return {'draft':not self.already_public, 'assets':[{'name':name} for name in self.uploaded],
                    'html_url':'https://example.invalid/verified-release'}
        raise AssertionError(route)

    def gh(self, *arguments):
        if arguments[:3] == ('api','--paginate','--slurp'):
            return json.dumps([[{'tag_name':'v1.3.2','draft':False,'target_commitish':self.commit}]
                               if self.already_public else []])
        if arguments[:2] == ('release','download'):
            target = Path(arguments[arguments.index('--dir')+1])
            if arguments[2] == 'v1.3.1':
                name = arguments[arguments.index('--pattern')+1]
                (target/name).write_bytes(self.windows[name] + (b'changed' if self.source_corrupt else b''))
            else:
                for name, body in self.uploaded.items():
                    (target/name).write_bytes(body + (b'changed' if self.corrupt_readback else b''))
            return ''
        if arguments[:2] == ('release','create'):
            self.assertIn('--draft',arguments)
            self.mutations.append('create-draft')
            return ''
        if arguments[:2] == ('release','upload'):
            path = Path(arguments[3])
            self.uploaded[path.name] = path.read_bytes()
            self.mutations.append('upload')
            return ''
        if arguments[:2] == ('release','edit') and '--draft=false' in arguments:
            self.mutations.append('publish')
            self.already_public = True
            return ''
        raise AssertionError(arguments)

    def run_publisher(self):
        expected = {name:hashlib.sha256(body).hexdigest() for name,body in self.windows.items()}
        with patch.dict(os.environ,self.environment), patch.object(publisher,'gh',self.gh), \
             patch.object(publisher,'api',self.api), patch.object(publisher,'WINDOWS',expected), \
             patch.object(publisher.audit,'audit',return_value=self.package):
            publisher.main(self.directory)

    def test_exact_six_assets_are_read_back_before_publication(self):
        self.run_publisher()
        self.assertEqual(self.mutations,['create-draft']+['upload']*6+['publish'])
        self.assertEqual(set(self.uploaded),set(self.package['assets'])|set(self.windows))
        for name,body in self.windows.items():
            self.assertEqual(self.uploaded[name],body)

    def test_corrupt_readback_retains_draft(self):
        self.corrupt_readback = True
        with self.assertRaisesRegex(ValueError,'Uploaded bytes differ'):
            self.run_publisher()
        self.assertEqual(self.mutations,['create-draft']+['upload']*6)

    def test_failed_core_layout_cannot_create_a_release(self):
        for pose in self.evidence['duo']['poses']:
            if pose['name'] == 'tent-outer': continue
            pose['passed'] = False
            self.write_evidence()
            with self.subTest(name=pose['name']), self.assertRaisesRegex(ValueError,'posture evidence'):
                self.run_publisher()
            self.assertEqual(self.mutations,[])
            pose['passed'] = True

    def test_stale_verification_cannot_create_a_release(self):
        self.evidence['commit'] = 'b'*40
        self.write_evidence()
        with self.assertRaisesRegex(ValueError,'this tag, commit and workflow run'):
            self.run_publisher()
        self.assertEqual(self.mutations,[])

    def test_missing_outer_landscape_cannot_create_a_release(self):
        self.evidence['duo']['poses'] = [p for p in self.evidence['duo']['poses']
                                       if p['name'] != 'closed-outer-landscape']
        self.write_evidence()
        with self.assertRaisesRegex(ValueError,'posture evidence'):
            self.run_publisher()
        self.assertEqual(self.mutations,[])

    def test_changed_windows_source_cannot_create_a_release(self):
        self.source_corrupt = True
        with self.assertRaisesRegex(ValueError,'Windows source bytes changed'):
            self.run_publisher()
        self.assertEqual(self.mutations,[])

    def test_moved_tag_cannot_create_a_release(self):
        self.tag_commit = 'b'*40
        with self.assertRaisesRegex(ValueError,'Release tag moved'):
            self.run_publisher()
        self.assertEqual(self.mutations,[])

    def test_existing_public_release_is_never_changed(self):
        self.already_public = True
        with self.assertRaisesRegex(ValueError,'existing published release'):
            self.run_publisher()
        self.assertEqual(self.mutations,[])


if __name__ == '__main__':
    unittest.main()
