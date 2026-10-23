"""Record core CI separately from the real pose evidence needed for publication."""
import argparse
import json
from pathlib import Path
import subprocess
import os

def collect(root, core_only=False):
    pose = {'status': 'not_run', 'reason': 'Duo UI and poses require manual duo_checks'}
    if not core_only:
        pose = json.loads((root / 'build/logs/duo/results.json').read_text(encoding='utf-8'))
        expected = {'open-landscape', 'open-portrait', 'book', 'tabletop', 'closed-outer',
                    'closed-outer-landscape'}
        if (pose.get('passed') is not True or pose.get('syntheticLayout') is not False
            or pose.get('commit') != os.environ['GITHUB_SHA']
            or not expected.issubset({p['name'] for p in pose['poses'] if p.get('passed')})):
            raise SystemExit('Required real simulator poses have not all passed on this commit')
    summaries = {}
    roles = ('duo', 'phone26', 'ipad') if not core_only and os.environ.get('LOGFORGE_COMPAT_SUITE') == 'full' else ('duo',)
    for role in roles:
        bundle = root / f'build/Tests-{role}.xcresult'
        summary = json.loads(subprocess.check_output(['xcrun', 'xcresulttool', 'get', 'test-results',
                              'summary', '--path', str(bundle)], text=True))
        (root / f'build/logs/tests-{role}-summary.json').write_text(json.dumps(summary, indent=2)+'\n', encoding='utf-8')
        if summary.get('failedTests', 0) or summary.get('passedTests', 0) < 1:
            raise SystemExit(f'XCTest results missing or failed: {role}')
        summaries[role] = summary
    result = {'commit':os.environ['GITHUB_SHA'], 'run':os.environ['GITHUB_RUN_ID'],
              'passed':True, 'scope':'core' if core_only else 'release',
              'releaseReady':not core_only, 'xctest':summaries, 'duo':pose}
    (root / 'build/artifacts/verification.json').write_text(json.dumps(result, indent=2)+'\n', encoding='utf-8')
    return result


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--core-only', action='store_true')
    args = parser.parse_args()
    result = collect(Path(__file__).resolve().parents[1], args.core_only)
    print('Core tests passed; Duo UI/poses not run, not release-ready.' if args.core_only else
          'Verified XCTest results and six required inner/outer Duo layouts; extra diagnostics retained.')
