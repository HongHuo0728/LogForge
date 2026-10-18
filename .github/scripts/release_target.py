"""Route shared release tags to the platform that actually owns the version."""
import argparse
import importlib.util
import os
from pathlib import Path
import re


ROOT = Path(__file__).resolve().parents[2]
PLATFORMS = ('windows', 'ios')


def platform_version(platform, root=ROOT):
    if platform == 'windows':
        text = (root / 'CMakeLists.txt').read_text(encoding='utf-8')
        versions = re.findall(r'^\s*project\s*\(\s*LogForge\s+VERSION\s+(\d+\.\d+\.\d+)\b',
                              text, re.MULTILINE)
        if len(versions) != 1:
            raise ValueError('Expected one Windows version in CMakeLists.txt')
        return versions[0]
    if platform == 'ios':
        path = root / 'LogForgeMac/tools/release-info.py'
        spec = importlib.util.spec_from_file_location('logforge_ios_release_info', path)
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        return module.release_identity(root / 'LogForgeMac')
    raise ValueError(f'Unknown platform: {platform}')


def select_target(platform, ref_type, ref_name, version_reader=platform_version):
    if platform not in PLATFORMS:
        raise ValueError(f'Unknown platform: {platform}')
    version = version_reader(platform)
    if ref_type != 'tag':
        return True, version, 'Branch, pull request or manual build'
    if not re.fullmatch(r'v\d+\.\d+\.\d+', ref_name):
        raise ValueError(f'Invalid release tag {ref_name!r}; expected vMAJOR.MINOR.PATCH')
    if ref_name == f'v{version}':
        return True, version, f'{ref_name} matches {platform} {version}'
    other = 'ios' if platform == 'windows' else 'windows'
    other_version = version_reader(other)
    if ref_name == f'v{other_version}':
        return False, version, (f'{ref_name} belongs to {other} {other_version}; '
                                f'{platform} remains {version} and is skipped')
    raise ValueError(f'Tag {ref_name!r} matches neither {platform} {version} '
                     f'nor {other} {other_version}')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--platform', choices=PLATFORMS, required=True)
    args = parser.parse_args()
    try:
        build, version, reason = select_target(args.platform, os.environ.get('GITHUB_REF_TYPE', ''),
                                                os.environ.get('GITHUB_REF_NAME', ''))
    except (ValueError, OSError) as error:
        parser.exit(1, f'Release selection failed: {error}\n')
    print(reason)
    if output := os.environ.get('GITHUB_OUTPUT'):
        with open(output, 'a', encoding='utf-8') as file:
            file.write(f'should_build={str(build).lower()}\nversion={version}\n')


if __name__ == '__main__':
    main()
