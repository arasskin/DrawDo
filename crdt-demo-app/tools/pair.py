#!/usr/bin/env python3
"""Pair two installed CRDT demos using public keys only; restart both apps."""
import argparse
import ipaddress
import os
from pathlib import Path
import subprocess
import tempfile
import time

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--android', default='ZY227364TP')
p.add_argument('--iphone', default='Phone')
p.add_argument('--host', default='147.182.181.182')
p.add_argument('--port', type=int, default=7777)
p.add_argument('--timing', action='store_true')
a = p.parse_args()
subdir = 'timing/' if a.timing else ''
ipaddress.ip_address(a.host)
if not 0 < a.port < 65536:
    p.error('Invalid port')
package = 'dev.drawdo.crdt'
sdk = Path(os.environ.get('ANDROID_HOME', Path.home() / 'Library/Android/sdk'))
adb = [str(sdk / 'platform-tools/adb'), '-s', a.android]
ios = ['xcrun', 'devicectl', '--timeout', '30', 'device']
container = ['--device', a.iphone, '--domain-type', 'appDataContainer', '--domain-identifier', package]

def run(command, **kwargs):
    for attempt in range(3 if command[0] == 'xcrun' else 1):
        result = subprocess.run(list(map(str, command)), **kwargs)
        if result.returncode == 0:
            return result
        if attempt < 2 and command[0] == 'xcrun':
            time.sleep(1)
    result.check_returncode()

with tempfile.TemporaryDirectory(prefix='crdt-demo-pair-') as directory:
    path = Path(directory)
    android_key = run(adb + ['exec-out', 'run-as', package, 'cat', 'files/' + subdir + 'public-key.txt'],
                      capture_output=True, text=True).stdout.strip()
    run(ios + ['copy', 'from', *container, '--source', 'Library/Application Support/' + subdir + 'public-key.txt',
               '--destination', path / 'ios-key.txt'])
    ios_key = (path / 'ios-key.txt').read_text().strip()
    for key in (android_key, ios_key):
        if len(key) != 64 or any(c not in '0123456789abcdef' for c in key):
            raise SystemExit('Invalid public key; launch each app once before pairing')
    if android_key == ios_key:
        raise SystemExit('Each installation must have its own identity')
    config = f'{a.host} {a.port} {ios_key}\n'
    run(adb + ['shell', 'run-as', package, 'sh', '-c', "'cat > files/" + subdir + "peer.txt'"], input=config.encode())
    (path / 'peer.txt').write_text(f'{a.host} {a.port} {android_key}\n')
    run(ios + ['copy', 'to', *container, '--source', path / 'peer.txt',
               '--destination', 'Library/Application Support/' + subdir + 'peer.txt'])
run(adb + ['shell', 'am', 'force-stop', package])
run(adb + ['shell', 'am', 'start', '-W', '-n', package + '/.MainActivity'])
run(ios + ['process', 'launch', '--device', a.iphone, '--terminate-existing', package])
print(f'Paired Android and iPhone through {a.host}:{a.port}')
