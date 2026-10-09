#!/usr/bin/env python3
"""Public reads and Ed25519 owner writes against the real Linux server."""
import os
import argparse
from pathlib import Path
import shutil
import json
import hashlib
import datetime
import subprocess
import tempfile

repo = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument('--linux', action='store_true')
parser.add_argument('--report', help='Optional JSON validation report path')
args = parser.parse_args()
if args.linux:
    os.environ['LITTLEBEAR_SERVER'] = str(repo/'scripts/littlebear_linux_test_server.py')
dart = os.environ.get('DART') or shutil.which('dart') or str(Path.home()/'developer/flutter/bin/cache/dart-sdk/bin/dart')
with tempfile.TemporaryDirectory(prefix='datafriend-tcp-') as directory:
    root = Path(directory)
    result = subprocess.run([dart, '--packages='+str(repo/'src/app/.dart_tool/package_config.json'), str(repo/'datafriend/tests/network_integration.dart'),
                             directory, str(repo)], check=True, timeout=180,
                            text=True, stdout=subprocess.PIPE)
    print(result.stdout, end='')
    if args.report:
        report = json.loads(result.stdout.strip().splitlines()[-1])
        report['time'] = datetime.datetime.now(datetime.timezone.utc).isoformat()
        report['server'] = 'Linux io_uring TCP / Ed25519 writes'
        binary = Path(os.environ.get('DATAFRIEND_LINUX_SERVER', str(repo/'littlebear/build/server'))) if args.linux else repo/'littlebear/build/server'
        report['server_sha256'] = hashlib.sha256(binary.read_bytes()).hexdigest()
        Path(args.report).write_text(json.dumps(report, indent=2) + '\n')
