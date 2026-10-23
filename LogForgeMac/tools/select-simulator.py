"""Select/create a compatible device from Xcode's actual runtime/type inventory."""
import json
import subprocess
import sys


def select(role):
    inventory = json.loads(subprocess.check_output(['xcrun', 'simctl', 'list', '--json']))
    versions = {'duo': '27.1', 'phone26': '26.5', 'ipad': '27.0'}
    version = versions[role]
    runtime = next((r for r in inventory['runtimes']
                    if r.get('isAvailable') and r['version'] == version and 'iOS' in r['identifier']), None)
    if runtime is None:
        raise RuntimeError(f'Missing iOS {version} runtime for {role}')
    def matches(name):
        return ('iPhone Duo' in name if role == 'duo' else
                name.startswith('iPad') if role == 'ipad' else
                name.startswith('iPhone') and 'Duo' not in name)
    for device in inventory['devices'].get(runtime['identifier'], []):
        if device.get('isAvailable') and matches(device['name']):
            return device['udid']
    types = [d for d in inventory['devicetypes'] if matches(d['name'])]
    for device in reversed(types):
        result = subprocess.run(['xcrun', 'simctl', 'create', f'LogForge-{role}',
                                 device['identifier'], runtime['identifier']], capture_output=True, text=True)
        if result.returncode == 0:
            return result.stdout.strip()
    raise RuntimeError(f'No compatible simulator device type for {role}')


if __name__ == '__main__':
    print(select(sys.argv[1]))
