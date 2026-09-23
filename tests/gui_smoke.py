"""Exercise actual native windows with isolated settings and synthetic-only inputs.

Requires an interactive Windows desktop. Missing FFmpeg is an explicitly injected
UI state; disk traversal and real capabilities are covered by separate CTests.
"""
import argparse
import json
import os
from pathlib import Path
import subprocess


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--exe', type=Path, required=True)
    parser.add_argument('--ffmpeg', type=Path, required=True)
    parser.add_argument('--work', type=Path, required=True)
    parser.add_argument('--input', type=Path, help='Optional generated HLG fixture, never private footage')
    args = parser.parse_args()
    args.work.mkdir(parents=True, exist_ok=True)
    results = []
    cases = [(language, theme, dpi, False)
             for language in ('en', 'zh-CN') for theme in ('dark', 'light') for dpi in (96, 144, 192)]
    cases += [('en', 'dark', 96, True), ('zh-CN', 'light', 144, True)]
    for language, theme, dpi, missing in cases:
        name = f'{language}-{theme}-{dpi}' + ('-missing' if missing else '')
        case = args.work.resolve() / name
        data = case / 'appdata'
        data.mkdir(parents=True, exist_ok=True)
        creative = dpi != 96
        (data / 'settings.json').write_text(json.dumps({
            'ffmpeg': str(args.ffmpeg.resolve()), 'language': language, 'theme': theme,
            'creative': {'enabled': creative, 'shadow_stops': 3, 'highlight_stops': 1, 'saturation': .85},
        }), encoding='utf-8')
        env = {k.upper(): v for k, v in os.environ.items()}
        env['LOGFORGE_DATA_DIR'] = str(data)
        subprocess.run([str(args.exe.resolve().with_name('LogForge-cli.exe')), '--approve-ffmpeg',
                        '--ffmpeg', str(args.ffmpeg.resolve())], env=env, check=True, capture_output=True)
        # Language / theme are read from settings, so this also exercises startup persistence.
        command = [str(args.exe.resolve()), '--ui-test', str(case), '--dpi', str(dpi)]
        if missing:
            command += ['--missing-ffmpeg']
        subprocess.run(command, env=env, timeout=60, check=True)
        result = json.loads((case / 'ui-test.json').read_text(encoding='utf-8'))
        assert result['passed'] and result['settings_exercised'] and result['snapshot_saved'], result
        assert (result['language'], result['theme'], result['dpi']) == (language, theme, dpi), result
        assert result['creative_enabled'] == creative, result
        assert result['install_visible'] == result['manual_visible'] == missing, result
        for image in ('main.png', 'settings.png', 'switched.png', 'settings-cancel.png'):
            assert (case / image).read_bytes().startswith(b'\x89PNG\r\n\x1a\n'), image
        results.append({'case': name, **result})
        print('PASS:', name, flush=True)
    if args.input:
        case = args.work.resolve() / 'actual-conversion'
        case.mkdir(exist_ok=True)
        data = case / 'appdata'
        data.mkdir(exist_ok=True)
        (data / 'settings.json').write_text(json.dumps({'ffmpeg': str(args.ffmpeg.resolve())}), encoding='utf-8')
        env = {k.upper(): v for k, v in os.environ.items()}
        env['LOGFORGE_DATA_DIR'] = str(data)
        subprocess.run([str(args.exe.resolve().with_name('LogForge-cli.exe')), '--approve-ffmpeg',
                        '--ffmpeg', str(args.ffmpeg.resolve())], env=env, check=True, capture_output=True)
        # Two modes use distinct output names; production overwrite protection stays active.
        for creative in (False, True):
            output = case / ('creative.mov' if creative else 'standard.mov')
            if output.exists():
                output.unlink()  # This generated test fixture only; application keeps no-overwrite.
            command = [str(args.exe.resolve()), '--smoke-test', str(args.input.resolve()), str(output),
                       '--input-chroma-location', 'left']
            if creative:
                command += ['--tone']
            subprocess.run(command, env=env, timeout=180, check=True)
            report = json.loads(Path(str(output) + '.gui-test.json').read_text(encoding='utf-8'))
            assert report['passed'] and report['creative_enabled'] == creative, report
            assert report['convert_enabled'] and not report['cancel_enabled'], report
            results.append({'case': output.stem, **report})
            print('PASS: native drop, probe, transcode, validation:', output.stem, flush=True)
    (args.work / 'gui-suite.json').write_text(json.dumps(results, indent=2), encoding='utf-8')


if __name__ == '__main__':
    main()
