"""Opt-in real Resolve import test using Blackmagic's installed scripting SDK.

Run against an idle Resolve instance. Pass distinct camera-reference, pre-writer
baseline and final LogForge files. No clip color space, gamma or LUT is assigned.
This is deliberately not a CTest/CI simulation of a commercial editor.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import sys
import uuid


FIELDS = ('Input Color Space', 'IDT', 'Gamma Notes', 'Data Level', 'Input LUT',
          'Camera Manufacturer', 'Camera Type', 'Video Codec', 'Bit Depth',
          'Resolution', 'FPS')


def sha256(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--reference', type=Path, required=True)
    parser.add_argument('--baseline', type=Path, required=True)
    parser.add_argument('--production', type=Path, required=True)
    parser.add_argument('--candidate', action='append', default=[],
                        help='Optional expected-color-space=path pair for an isolated A/B file')
    parser.add_argument('--report', type=Path, required=True)
    parser.add_argument('--resolve-library', type=Path, required=True,
                        help='fusionscript.dll from the installed Resolve')
    parser.add_argument('--sdk-modules', type=Path, default=Path(os.environ['PROGRAMDATA']) /
                        'Blackmagic Design/DaVinci Resolve/Support/Developer/Scripting/Modules')
    args = parser.parse_args()
    assert not args.report.exists(), 'Refusing to overwrite an earlier test report'
    cases = [('native-reference', args.reference, 'Apple Log'),
             ('baseline', args.baseline, 'Rec.2020 (Scene)'),
             ('production', args.production, 'Apple Log')]
    for index, candidate in enumerate(args.candidate):
        expected, path = candidate.split('=', 1)
        cases.append((f'candidate-{index + 1}', Path(path), expected))
    paths = [p.resolve(strict=True) for _, p, _ in cases]
    assert len(set(paths)) == len(paths), 'Use separate files for each A/B case'
    os.environ['RESOLVE_SCRIPT_LIB'] = str(args.resolve_library.resolve(strict=True))
    sys.path.insert(0, str(args.sdk_modules.resolve(strict=True)))
    import DaVinciResolveScript
    resolve = DaVinciResolveScript.scriptapp('Resolve')
    assert resolve, 'Start Resolve with local scripting enabled'
    manager = resolve.GetProjectManager()
    previous = manager.GetCurrentProject()
    previous_name = previous.GetName() if previous else ''
    # Never switch away from a user's editing project that may have unsaved work.
    assert (not previous_name or previous_name == 'Untitled Project' or
            previous_name.startswith('LogForge Identification ')), (
                'Open an empty test session first; refusing to switch an editing project')
    if previous and previous_name.startswith('LogForge Identification '):
        assert manager.SaveProject()
    name = 'LogForge Identification ' + uuid.uuid4().hex[:12]
    project = manager.CreateProject(name)
    assert project
    report = {'product': resolve.GetProductName(), 'version': resolve.GetVersionString(),
              'project': name, 'clip_color_space_set_by_script': False,
              'clip_lut_set_by_script': False, 'results': []}
    try:
        for key, value in [('colorScienceMode', 'davinciYRGBColorManagedv2'),
                           ('isAutoColorManage', '1'),
                           ('colorSpaceInput', 'Rec.709 Gamma 2.4')]:
            assert project.SetSetting(key, value), (key, value)
            assert project.GetSetting(key) == value
        report['color_settings'] = {key: project.GetSetting(key) for key in
            ['colorScienceMode', 'isAutoColorManage', 'colorSpaceInput', 'colorSpaceOutput']}
        for (label, _, expected), path in zip(cases, paths):
            imported = project.GetMediaPool().ImportMedia([str(path)])
            assert imported and len(imported) == 1, label
            properties = imported[0].GetClipProperty()
            entry = {'case': label, 'file': path.name, 'sha256': sha256(path),
                     'expected': expected, 'properties': {k: properties.get(k) for k in FIELDS}}
            report['results'].append(entry)
            print(json.dumps(entry), flush=True)
            assert properties.get('Input Color Space') == expected, entry
            assert properties.get('Input LUT') == '', 'Unexpected input LUT'
            assert properties.get('Data Level') == 'Auto', 'Unexpected manual level override'
        report['passed'] = True
    except BaseException:
        report['passed'] = False
        raise
    finally:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        with args.report.open('x', encoding='utf-8') as file:
            json.dump(report, file, indent=2)
        assert manager.CloseProject(project)
        assert manager.DeleteProject(name)  # only this script's fresh, disposable test
        if previous_name and previous_name != 'Untitled Project':
            assert manager.LoadProject(previous_name)


if __name__ == '__main__':
    main()
