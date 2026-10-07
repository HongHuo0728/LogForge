"""Repository checks; does NOT compile Swift, run Metal or encode video.

Install requirements-static.txt outside the repository, then:
python static_check.py --parser-root <directory> --report <json>
"""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import plistlib
import re
import sys
import xml.etree.ElementTree as ET
from datetime import datetime, timezone


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--parser-root')
    parser.add_argument('--report')
    args = parser.parse_args()
    if args.parser_root:
        sys.path.insert(0, args.parser_root)
    from openstep_parser import OpenStepDecoder
    from tree_sitter import Language, Parser
    import tree_sitter_swift
    root = Path(__file__).resolve().parents[1] / 'LogForge For iPhone'
    app = root / 'LogForge For iPhone'
    findings = []
    swift_parser = Parser(Language(tree_sitter_swift.language()))
    sources = list(root.rglob('*.swift'))
    for path in sources:
        assert not swift_parser.parse(path.read_bytes()).root_node.has_error, f'Swift grammar: {path}'
    findings.append(f'Swift grammar: {len(sources)} files (not type checking)')
    project = OpenStepDecoder.ParseFromString((root/'LogForge For iPhone.xcodeproj/project.pbxproj').read_text(encoding='utf-8'))
    objects = project['objects']
    references = {}

    def walk(identifier, parent):
        obj = objects[identifier]
        base = root if obj.get('sourceTree') == 'SOURCE_ROOT' else parent
        if obj['isa'] in ('PBXGroup', 'PBXVariantGroup'):
            base = base / obj.get('path', '')
            for child in obj.get('children', []):
                walk(child, base)
        elif obj['isa'] == 'PBXFileReference' and obj.get('sourceTree') != 'BUILT_PRODUCTS_DIR':
            path = base / obj['path']
            assert path.exists(), f'Missing project file: {path}'
            references[identifier] = path.resolve()

    walk(objects[project['rootObject']]['mainGroup'], root)
    members = set()
    for obj in objects.values():
        if obj['isa'] == 'PBXBuildFile':
            assert obj['fileRef'] in objects
        if obj['isa'] in ('PBXSourcesBuildPhase', 'PBXResourcesBuildPhase'):
            for build in obj['files']:
                assert build in objects
                ref = objects[build]['fileRef']
                if ref in references:
                    members.add(references[ref])
    for path in sources:
        assert path.resolve() in members, f'Source not in target: {path}'
    findings.append(f'Xcode objects: {len(objects)}; file references and Swift target membership resolve')
    for target in objects.values():
        if target['isa'] != 'PBXNativeTarget':
            continue
        source_members = []
        for phase in target['buildPhases']:
            if objects[phase]['isa'] == 'PBXSourcesBuildPhase':
                source_members += [references[objects[b]['fileRef']] for b in objects[phase]['files']]
        name = target['name']
        expected = 25 if name == 'LogForge For iPhone' else 2 if name.endswith('Tests') and not name.endswith('UITests') else 3
        assert len(source_members) == expected, f'Sources in {name}: {len(source_members)}'
        if name == 'LogForge For iPhone':
            assert not any('Tests' in str(p.relative_to(root)) for p in source_members)
            phases = [objects[p]['isa'] for p in target['buildPhases']]
            assert phases.index('PBXShellScriptBuildPhase') < phases.index('PBXSourcesBuildPhase')
    findings.append('Target source separation and codec build-before-Sources phase verified')
    for path in root.rglob('*.json'):
        json.loads(path.read_text(encoding='utf-8'))
    for path in root.rglob('*.plist'):
        plistlib.loads(path.read_bytes())
    for suffix in ('*.xcscheme', '*.xcworkspacedata', '*.svg'):
        for path in root.rglob(suffix):
            ET.parse(path)
    tables = {}
    for lang in ('en','zh-Hans','zh-Hant','fr','es'):
        path = app / f'{lang}.lproj/Localizable.strings'
        pairs = re.findall(r'^("(?:[^"\\]|\\.)*")\s*=\s*("(?:[^"\\]|\\.)*");$',path.read_text(encoding='utf-8'),re.M)
        table = {json.loads(key):json.loads(value) for key,value in pairs}
        assert len(table) == len(pairs)
        tables[lang] = table
        permission_pairs = re.findall(r'^("(?:[^"\\]|\\.)*")\s*=\s*("(?:[^"\\]|\\.)*");$',(app/f'{lang}.lproj/InfoPlist.strings').read_text(encoding='utf-8'),re.M)
        permissions = {json.loads(k):json.loads(v) for k,v in permission_pairs}
        assert set(permissions) == {'CFBundleDisplayName','NSPhotoLibraryUsageDescription','NSPhotoLibraryAddUsageDescription'}
    for lang, table in tables.items():
        assert table.keys() == tables['en'].keys(), f'Localization key mismatch: {lang}'
        for key in table:
            assert re.findall(r'%[@df]',table[key]) == re.findall(r'%[@df]',tables['en'][key]), f'Format mismatch: {lang}/{key}'
    for path in sources:
        for key in re.findall(r'(?:L10n\.text|NativeFailure)\("([^"\\]+)"',path.read_text(encoding='utf-8')):
            assert key in tables['en'], f'Missing localization {key}: {path}'
    info = plistlib.loads((app/'Info.plist').read_bytes())
    assert set(info['CFBundleLocalizations']) == set(tables)
    assert info['LogForgeReleaseBuild'] == '$(LOGFORGE_RELEASE_BUILD)'
    assert info['CFBundleVersion'] == '$(CURRENT_PROJECT_VERSION)'
    spec = importlib.util.spec_from_file_location('logforge_release_info', root.parent/'tools/release-info.py')
    identity = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(identity)
    version = identity.release_identity(root.parent)
    release_build = identity.release_build(root.parent)
    for obj in objects.values():
        settings = obj.get('buildSettings', {})
        if 'MARKETING_VERSION' in settings:
            assert settings['MARKETING_VERSION'] == version
            assert settings['LOGFORGE_RELEASE_BUILD'] == release_build
            if settings.get('GENERATE_INFOPLIST_FILE') == 'YES':
                assert settings['INFOPLIST_FILE'] == 'TestReleaseInfo.plist'
                test_info = plistlib.loads((root/'TestReleaseInfo.plist').read_bytes())
                assert test_info['LogForgeReleaseBuild'] == '$(LOGFORGE_RELEASE_BUILD)'
    findings.append(f'iOS release identity: {version} ({release_build}); Apple bundle build stays numeric and internal')
    findings.append(f'Five localizations: {len(tables["en"])} identical keys and format arguments')
    icon = app/'LogForge.icon'
    manifest = json.loads((icon/'icon.json').read_text())
    assert len(manifest['groups']) == 4
    for group in manifest['groups']:
        for layer in group['layers']:
            assert (icon/'Assets'/layer['image-name']).exists()
    findings.append('Icon package: four layers resolve (Icon Composer compilation unverified)')
    metal = (root/'MetalColorProcessor.swift').read_text(encoding='utf-8')
    shader = (root/'LogForgeShaders.metal').read_text(encoding='utf-8')
    assert 'bytesNoCopy' not in metal and '[weak job]' not in metal
    assert 'index: 6' in metal and '[[buffer(6)]]' in shader
    assert 'logForgeColorReferenceKernel' in shader and '2e-6' in metal
    project_text = (root/'LogForge For iPhone.xcodeproj/project.pbxproj').read_text(encoding='utf-8')
    assert '17.2' not in project_text and 'MTL_FAST_MATH = YES' not in project_text
    findings.append('Metal parameter ABI, owned buffers, retained jobs, precision checks and iOS 26 deployment inspected')
    pipeline = (app/'NativePipeline.swift').read_text(encoding='utf-8')
    assert 'CVPixelBufferPoolCreatePixelBufferWithAuxAttributes' in pipeline
    assert pipeline.index('let outputPixels = try await NativeSamples.pooledBuffer') > pipeline.index('while pendingVideo != nil')
    assert 'writer.movieTimeScale = try NativeSamples.movieScale' in pipeline
    assert 'input.mediaTimeScale = try NativeSamples.mediaScale' in pipeline
    assert 'thread_type=FF_THREAD_SLICE' in (root/'NativeCodec/LogForgeCodec.m').read_text(encoding='utf-8')
    assert 'UIApplication.openSettingsURLString' in (app/'LanguageManager.swift').read_text(encoding='utf-8')
    for filename in ('ContentView.swift','GlassComponents.swift','VideoInfoView.swift'):
        assert not re.search(r'\.(?:thinMaterial|ultraThinMaterial|regularMaterial|thickMaterial)',(app/filename).read_text(encoding='utf-8'))
    findings.append('Per-frame pool allocation, independent media clocks and bounded MOV clock, synchronous codec threading and clear glass source constraints inspected')
    baseline = json.loads((root.parent/'docs/STATIC_AUDIT_2026-10-04.json').read_text(encoding='utf-8'))['inventory']
    for item in baseline:
        assert (root/item['path']).is_file(), f'Original file removed: {item["path"]}'
    original_icon = next(i for i in baseline if i['path'].endswith('.PNG'))
    assert hashlib.sha256((root/original_icon['path']).read_bytes()).hexdigest() == original_icon['sha256']
    findings.append(f'All {len(baseline)} original paths and original raster icon retained')
    report = {'generated_at_utc':datetime.now(timezone.utc).isoformat(),'checks':findings,'apple_platform_execution':False,'swift_type_checking':False,
              'unverified':['Xcode build','FFmpeg iOS linking','Metal execution','media integration tests','Icon Composer compilation','Liquid Glass contrast on device','haptics','editor Apple Log recognition'],
              'files':[{'path':str(path.relative_to(root)),'sha256':hashlib.sha256(path.read_bytes()).hexdigest()} for path in sorted(root.rglob('*')) if path.is_file() and '__MACOSX' not in path.parts]}
    if args.report:
        Path(args.report).write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    print('\n'.join(findings))


if __name__ == '__main__':
    main()
