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


def find_release(tag):
    # The tag endpoint only resolves published releases; listing includes drafts.
    pages = json.loads(gh('api', '--paginate', '--slurp', f'repos/{REPO}/releases?per_page=100'))
    return next((release for page in pages for release in page if release['tag_name'] == tag), None)


def download_verified(directory):
    runs = api('actions/workflows/build-ios.yml/runs?event=push&branch=main&status=success&per_page=100')
    run = next((r for r in runs['workflow_runs'] if r['head_sha'] == os.environ['GITHUB_SHA']), None)
    audit.require(run is not None, 'No successful main build exists for this exact release commit')
    version = audit.identity.release_identity(ROOT / 'LogForgeMac')
    build = audit.identity.release_build(ROOT / 'LogForgeMac')
    artifact = f'LogForge-{version}-iOS-{build}-unsigned-{run["run_number"]}'
    directory.mkdir(parents=True, exist_ok=True)
    gh('run', 'download', str(run['id']), '--repo', REPO, '--name', artifact, '--dir', str(directory))
    os.environ['LOGFORGE_VERIFIED_RUN'] = str(run['id'])


def main(directory):
    audit.require(os.environ['GITHUB_REPOSITORY'] == REPO and os.environ['GITHUB_REF_TYPE'] == 'tag',
                  'Publishing is restricted to release tags in the main repository')
    package = audit.audit(directory, ROOT / 'LogForgeMac')
    tag, commit = 'v' + package['version'], os.environ['GITHUB_SHA']
    audit.require(os.environ['GITHUB_REF_NAME'] == tag, 'Tag and package version differ')
    evidence_run = os.environ.get('LOGFORGE_VERIFIED_RUN', os.environ['GITHUB_RUN_ID'])
    source_run = api('actions/runs/' + evidence_run)
    audit.require(source_run.get('head_sha') == commit and source_run.get('conclusion') == 'success'
                  and source_run.get('event') == 'push' and source_run.get('head_branch') == 'main'
                  and source_run.get('path') == '.github/workflows/build-ios.yml'
                  and source_run.get('head_repository', {}).get('full_name') == REPO,
                  'Build evidence must be a successful main workflow for this exact release commit')
    previous_audit = json.loads((directory / 'package-audit.json').read_text())
    verification = json.loads((directory / 'verification.json').read_text())
    for report in (previous_audit, verification):
        audit.require(report.get('passed') is True and report.get('commit') == commit
                      and str(report.get('run')) == evidence_run,
                      'Evidence must come from this tag, commit and workflow run')
    audit.require(previous_audit['assets'] == package['assets'], 'Package changed after validation')
    expected_poses = {'open-landscape', 'open-portrait', 'book', 'tabletop', 'closed-outer',
                      'closed-outer-landscape'}
    pose = verification['duo']
    audit.require(pose.get('passed') is True and pose.get('syntheticLayout') is False
                  and pose.get('commit') == commit
                  and expected_poses.issubset({p['name'] for p in pose['poses'] if p.get('passed')}),
                  'Required real Duo posture evidence missing')
    audit.require('duo' in verification['xctest'] and
                  set(verification['xctest']) <= {'duo', 'phone26', 'ipad'}, 'Duo native test evidence missing')
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
        run_url = f'https://github.com/{REPO}/actions/runs/{evidence_run}'
        notes = (ROOT / f'docs/RELEASE_{package["version"]}_IOS.md').read_text(encoding='utf-8')
        notes += '\n## Verified build and source provenance\n\n'
        notes += f'- Source: [{commit}](https://github.com/{REPO}/commit/{commit}); tag `{tag}`.\n'
        notes += f'- [Successful build, tests and diagnostic artifacts]({run_url}).\n'
        notes += '- The release promotes these verified bytes without rebuilding or repeating the simulator suite.\n'
        notes += f'- Xcode {package["xcode"]}, iOS 27.1 SDK; numeric bundle build `{package["bundleBuild"]}`.\n'
        for role, result in verification['xctest'].items():
            notes += f'- XCTest `{role}`: {result["passedTests"]} passed, {result.get("failedTests",0)} failed.\n'
        notes += '- Release smoke checks passed on the actual Duo simulator: unfolded landscape/portrait, book, tabletop, and closed outer display in portrait/landscape, including import/settings toolbar interactions. Geometry unit tests are separate evidence.\n'
        tent = next((p for p in pose['poses'] if p['name'] == 'tent-outer'), {})
        notes += '- Tent posture: ' + ('verified on the outer display.\n' if tent.get('passed') else
            'not verified by the automation; outer-display landscape is verified separately.\n')
        for name in ('splitView','keyboard','multiWindow','appearance','fileDrop','conversion'):
            result = pose.get(name, {})
            passed = result.get('passed') is True and (name != 'keyboard' or result.get('splitVerified') is True)
            notes += f'- Additional `{name}` diagnostic: ' + ('passed.\n' if passed else
                ('not verified; diagnostic retained.\n' if result else 'not run in the focused release suite.\n'))
        notes += f'- [Source ZIP](https://github.com/{REPO}/archive/refs/tags/{tag}.zip) / [source tarball](https://github.com/{REPO}/archive/refs/tags/{tag}.tar.gz).\n'
        notes += f'- Windows 1.3.0 files copied byte-for-byte from [{WINDOWS_SOURCE}](https://github.com/{REPO}/releases/tag/{WINDOWS_SOURCE}).\n\n'
        notes += '| Asset | SHA-256 |\n|---|---|\n'
        for name, digest in expected_assets.items():
            notes += f'| `{name}` | `{digest}` |\n'
        notes_file = work / 'notes.md'
        notes_file.write_text(notes, encoding='utf-8')
        # A retry may complete our existing draft, but never edits a public release.
        existing = find_release(tag)
        if existing:
            audit.require(existing['draft'] and existing['target_commitish'] == commit,
                          'Refusing to replace an existing published release or unrelated draft')
            gh('release', 'edit', tag, '--repo', REPO, '--title', tag, '--notes-file', str(notes_file))
        else:
            gh('release', 'create', tag, '--repo', REPO, '--verify-tag', '--target', commit,
               '--title', tag, '--notes-file', str(notes_file), '--draft')
        release = find_release(tag)
        audit.require(release is not None and release['draft']
                      and release['target_commitish'] == commit, 'Release draft was not confirmed')
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
    parser.add_argument('--download-verified', action='store_true')
    arguments = parser.parse_args()
    directory = arguments.directory.resolve()
    if arguments.download_verified:
        download_verified(directory)
    main(directory)
