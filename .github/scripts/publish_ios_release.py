"""Publish the validated iOS release with unchanged v1.3.1 Windows assets.

Only the tag job has write permission. All uploads go to a draft, and the draft
remains private if a provenance, checksum, or read-back verification fails.
"""
import argparse
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('audit_package', ROOT / 'LogForgeMac/tools/audit-package.py')
audit = importlib.util.module_from_spec(spec)
spec.loader.exec_module(audit)
REPO = 'HongHuo0728/LogForge'
WINDOWS_SOURCE = 'v1.3.1'
WINDOWS = {
    'LogForge-1.3.0-Windows-x64.zip': '867e751687182ec398ac1232edf0aabbf0e1970a6864b75595d177bf44c6266a',
    'LogForge-1.3.0-Windows-x64.zip.sha256': '2ca511e4b402c31c0d4ac586d0a5c717e1746e199d904cebe0db5b1114e2ea14',
}


def gh(*args):
    return subprocess.check_output(['gh', *args], text=True)


def api(path):
    return json.loads(gh('api', f'repos/{REPO}/{path}'))


def main(directory):
    audit.require(os.environ['GITHUB_REPOSITORY'] == REPO and os.environ['GITHUB_REF_TYPE'] == 'tag',
                  'Publishing is restricted to release tags in the main repository')
    package = audit.audit(directory, ROOT / 'LogForgeMac')
    tag, commit = 'v' + package['version'], os.environ['GITHUB_SHA']
    audit.require(os.environ['GITHUB_REF_NAME'] == tag, 'Tag and package version differ')
    previous_audit = json.loads((directory / 'package-audit.json').read_text())
    verification = json.loads((directory / 'verification.json').read_text())
    for report in (previous_audit, verification):
        audit.require(report.get('passed') is True and report.get('commit') == commit
                      and str(report.get('run')) == os.environ['GITHUB_RUN_ID'],
                      'Evidence must come from this tag, commit and workflow run')
    audit.require(previous_audit['assets'] == package['assets'], 'Package changed after validation')
    expected_poses = {'open-landscape', 'open-portrait', 'book', 'tabletop', 'tent-outer', 'closed-outer'}
    pose = verification['duo']
    audit.require(pose.get('passed') is True and pose.get('syntheticLayout') is False
                  and pose.get('commit') == commit
                  and {p['name'] for p in pose['poses'] if p.get('passed')} == expected_poses,
                  'Required real Duo posture evidence missing')
    for scenario in ('splitView', 'keyboard', 'multiWindow', 'appearance', 'conversion'):
        audit.require(pose.get(scenario, {}).get('passed') is True,
                      'Required Duo scenario evidence missing: ' + scenario)
    audit.require(pose['keyboard'].get('splitVerified') is True, 'Split keyboard evidence missing')
    audit.require(set(verification['xctest']) == {'duo', 'phone26', 'ipad'}, 'Compatibility test evidence missing')
    for role, result in verification['xctest'].items():
        audit.require(result.get('passedTests', 0) > 0 and result.get('failedTests', 0) == 0,
                      f'XCTest validation failed: {role}')
    ref = api('git/ref/tags/' + tag)['object']
    if ref['type'] == 'tag':
        ref = api('git/tags/' + ref['sha'])['object']
    audit.require(ref['type'] == 'commit' and ref['sha'] == commit, 'Release tag moved')
    source = api('releases/tags/' + WINDOWS_SOURCE)
    audit.require(not source['draft'], 'Windows source release must already be published')
    old_assets = {a['name']:a for a in source['assets']}
    with tempfile.TemporaryDirectory(prefix='logforge-release-') as work:
        work = Path(work)
        old = work / 'windows'
        old.mkdir()
        for name, expected in WINDOWS.items():
            audit.require(name in old_assets, 'Windows source asset is missing: ' + name)
            gh('release', 'download', WINDOWS_SOURCE, '--repo', REPO, '--pattern', name, '--dir', str(old))
            audit.require(audit.digest(old / name) == expected, 'Windows source bytes changed: ' + name)
        audit.verify_checksum(old / 'LogForge-1.3.0-Windows-x64.zip')
        files = [directory / name for name in package['assets']] + [old / name for name in WINDOWS]
        expected_assets = {p.name:audit.digest(p) for p in files}
        audit.require(len(expected_assets) == 6, 'Release must have exactly six assets')
        run_url = f'https://github.com/{REPO}/actions/runs/{os.environ["GITHUB_RUN_ID"]}'
        notes = (ROOT / f'docs/RELEASE_{package["version"]}_IOS.md').read_text(encoding='utf-8')
        notes += '\n## Verified build and source provenance\n\n'
        notes += f'- Source: [{commit}](https://github.com/{REPO}/commit/{commit}); tag `{tag}`.\n'
        notes += f'- [Successful build, tests and diagnostic artifacts]({run_url}).\n'
        notes += f'- Xcode {package["xcode"]}, iOS 27.1 SDK; numeric bundle build `{package["bundleBuild"]}`.\n'
        for role, result in verification['xctest'].items():
            notes += f'- XCTest `{role}`: {result["passedTests"]} passed, {result.get("failedTests",0)} failed.\n'
        notes += '- Real Duo simulator: unfolded landscape/portrait, book, tabletop, tent outer display and closed outer display passed. Geometry unit tests are recorded separately.\n'
        notes += '- Native Split View, shared multi-window queue, system split keyboard input, accessibility/appearance, and real conversion continuity checks passed.\n'
        notes += f'- Windows 1.3.0 files copied byte-for-byte from [{WINDOWS_SOURCE}](https://github.com/{REPO}/releases/tag/{WINDOWS_SOURCE}).\n\n'
        notes += '| Asset | SHA-256 |\n|---|---|\n'
        for name, digest in expected_assets.items():
            notes += f'| `{name}` | `{digest}` |\n'
        notes_file = work / 'notes.md'
        notes_file.write_text(notes, encoding='utf-8')
        # A retry may complete our existing draft, but never edits a public release.
        releases = json.loads(gh('api', '--paginate', '--slurp', f'repos/{REPO}/releases?per_page=100'))
        existing = next((r for page in releases for r in page if r['tag_name'] == tag), None)
        if existing:
            audit.require(existing['draft'] and existing['target_commitish'] == commit,
                          'Refusing to replace an existing published release or unrelated draft')
            gh('release', 'edit', tag, '--repo', REPO, '--title', tag, '--notes-file', str(notes_file))
        else:
            gh('release', 'create', tag, '--repo', REPO, '--verify-tag', '--target', commit,
               '--title', tag, '--notes-file', str(notes_file), '--draft')
        release = api('releases/tags/' + tag)
        uploaded = {a['name'] for a in release['assets']}
        audit.require(uploaded <= expected_assets.keys(), 'Draft contains unexpected assets')
        for path in files:
            if path.name not in uploaded:
                gh('release', 'upload', tag, str(path), '--repo', REPO)
        downloaded = work / 'readback'
        downloaded.mkdir()
        gh('release', 'download', tag, '--repo', REPO, '--dir', str(downloaded))
        audit.require({p.name for p in downloaded.iterdir()} == expected_assets.keys(), 'Uploaded asset set differs')
        for name, expected in expected_assets.items():
            audit.require(audit.digest(downloaded / name) == expected, 'Uploaded bytes differ: ' + name)
        gh('release', 'edit', tag, '--repo', REPO, '--draft=false', '--latest')
        published = api('releases/tags/' + tag)
        audit.require(not published['draft'] and len(published['assets']) == 6, 'Publication not confirmed')
        print(published['html_url'])


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('directory', type=Path)
    main(parser.parse_args().directory.resolve())
