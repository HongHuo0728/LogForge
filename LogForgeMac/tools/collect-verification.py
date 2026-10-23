"""Require real XCTest/pose evidence; keep it separate from geometry unit tests."""
import json
from pathlib import Path
import subprocess
import os

root = Path(__file__).resolve().parents[1]
pose = json.loads((root / 'build/logs/duo/results.json').read_text(encoding='utf-8'))
expected = {'open-landscape', 'open-portrait', 'book', 'tabletop', 'closed-outer',
            'closed-outer-landscape'}
if (pose.get('passed') is not True or pose.get('syntheticLayout') is not False
    or pose.get('commit') != os.environ['GITHUB_SHA']
    or not expected.issubset({p['name'] for p in pose['poses'] if p.get('passed')})):
    raise SystemExit('Required real simulator poses have not all passed on this commit')
# Release scope: build, actual inner/outer layouts and core native UI. Additional
# posture/keyboard/multitasking diagnostics remain in the evidence, including
# failures; they must never be reported as passed merely to publish a build.
summaries = {}
for role in ('duo', 'phone26', 'ipad'):
    bundle = root / f'build/Tests-{role}.xcresult'
    summary = json.loads(subprocess.check_output(['xcrun', 'xcresulttool', 'get', 'test-results',
                          'summary', '--path', str(bundle)], text=True))
    (root / f'build/logs/tests-{role}-summary.json').write_text(json.dumps(summary, indent=2)+'\n', encoding='utf-8')
    if summary.get('failedTests', 0) or summary.get('passedTests', 0) < 1:
        raise SystemExit(f'XCTest results missing or failed: {role}')
    summaries[role] = summary
result = {'commit':os.environ['GITHUB_SHA'], 'run':os.environ['GITHUB_RUN_ID'],
          'passed':True, 'xctest':summaries, 'duo':pose}
(root / 'build/artifacts/verification.json').write_text(json.dumps(result, indent=2)+'\n', encoding='utf-8')
print('Verified XCTest results and six required inner/outer Duo layouts; extra diagnostics retained.')
