#!/usr/bin/env python3
"""AOT Dart controls and coarse phase profiles, with sequential rotated runs."""
import argparse
import csv
import datetime
import hashlib
import io
import json
import platform
import subprocess
from pathlib import Path

root = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--dart', default='dart')
parser.add_argument('--rounds', type=int, default=3)
parser.add_argument('--samples', type=int, default=1000)
parser.add_argument('--skip-build', action='store_true')
args = parser.parse_args()
if args.rounds < 1 or args.samples < 1:
    parser.error('rounds/samples must be positive')
build = root / 'datafriend-benchmarks/build'
output = root / 'docs/benchmarks'
suffix = 'dylib' if platform.system() == 'Darwin' else 'so'
library = build / f'native/libdatafriend.{suffix}'
workloads = ['views_shared_add_read', 'views_shared_edit_read',
             'views_mixed_add_read', 'views_mixed_edit_read', 'views_mixed_edit_only',
             'views_mixed_snapshot_reuse', 'app_100', 'app_1000', 'app_noop']
if not args.skip_build:
    for mode in ['control', 'profile']:
        command = [args.dart, 'compile', 'exe']
        if mode == 'profile':
            command.append('-DDATAFRIEND_PROFILE=true')
        command += ['tool/benchmark_dart_phases.dart', '-o', str(build / f'dart-phases-{mode}')]
        subprocess.run(command, cwd=root / 'src/app', check=True)
rows, runs = [], []


def run(workload, mode, samples, entities=10000):
    command = [str(build / f'dart-phases-{mode}'), str(library), workload,
               str(samples), str(entities)]
    result = subprocess.run(command, capture_output=True, text=True, check=True)
    runs.append({'command': command, 'stderr': result.stderr})
    return list(csv.DictReader(io.StringIO(result.stdout)))


# Odd final parity and uneven group sizes, for both compiled configurations.
for mode in ['control', 'profile']:
    for workload in workloads:
        run(workload, mode, 3, 237)

for round_index in range(args.rounds):
    offset = round_index % len(workloads)
    order = workloads[offset:] + workloads[:offset]
    for workload in order:
        for mode in (['control', 'profile'] if round_index % 2 == 0 else ['profile', 'control']):
            result = run(workload, mode, args.samples)
            for row in result:
                row['round'] = round_index + 1
                rows.append(row)
                if row['phase'] == 'total':
                    print(f"round={round_index + 1} {mode} {workload}: p50={row['p50']} us p99={row['p99']} us", flush=True)
output.mkdir(exist_ok=True)
with (output / 'datafriend-dart-phases.csv').open('w', newline='') as stream:
    writer = csv.DictWriter(stream, fieldnames=list(rows[0]))
    writer.writeheader()
    writer.writerows(rows)
sources = ['src/app/tool/benchmark_dart_phases.dart',
           'src/app/lib/data/datafriend.dart',
           'src/app/lib/data/datafriend_profile.dart',
           'src/app/lib/data/datafriend_document_store.dart',
           'src/app/lib/model/document.dart',
           'datafriend-benchmarks/benchmarks/run_dart_phases.py']
metadata = {
    'timestamp_utc': datetime.datetime.now(datetime.timezone.utc).isoformat(),
    'platform': platform.platform(), 'machine': platform.machine(),
    'dart': subprocess.run([args.dart, '--version'], text=True, capture_output=True, check=True).stdout.strip(),
    'arguments': vars(args), 'warmup': 100,
    'durability': 'MDB_NOSYNC; no synchronous disk flush or network',
    'native': 'Uninstrumented Release library; native FFI time includes owner-thread waits and C ABI serialization',
    'scope': 'Dart AOT; excludes Flutter widget build/layout/rasterization',
    'source_sha256': {path: hashlib.sha256((root / path).read_bytes()).hexdigest() for path in sources},
    'binary_sha256': {str(path): hashlib.sha256(path.read_bytes()).hexdigest()
                      for path in [library, build / 'dart-phases-control', build / 'dart-phases-profile']},
    'runs': runs,
}
(output / 'datafriend-dart-phases-metadata.json').write_text(json.dumps(metadata, indent=2) + '\n')
