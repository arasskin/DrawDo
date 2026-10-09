#!/usr/bin/env python3
"""Compare the historical whole-document adapter and direct view callback body."""
import argparse
import csv
import hashlib
import io
import json
import platform
import subprocess
from pathlib import Path
root = Path(__file__).resolve().parents[2]
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--dart', default='dart')
p.add_argument('--samples', type=int, default=1000)
p.add_argument('--rounds', type=int, default=3)
p.add_argument('--output-prefix', default='datafriend-direct-views')
p.add_argument('--baseline-exe', type=Path, help='Prior AOT reader executable; uses the same current native library')
args = p.parse_args()
if min(args.samples, args.rounds) < 1:
    p.error('samples and rounds must be positive')
build = root / 'datafriend-benchmarks/build'
exe = build / 'dart-direct-views'
subprocess.run([args.dart, 'compile', 'exe', 'tool/benchmark_direct_views.dart', '-o', str(exe)], cwd=root / 'src/app', check=True)
suffix = 'dylib' if platform.system() == 'Darwin' else 'so'
library = build / f'native/libdatafriend.{suffix}'
rows, runs = [], []
cases = []
for count in [100, 1000]:
    for mode in ['legacy', 'direct_edit', 'direct_read', 'direct_visible']:
        if args.baseline_exe and mode in ['direct_edit', 'direct_read']:
            cases.append(('before', count, mode))
        cases.append(('current', count, mode))
def run(implementation, count, mode, samples):
    binary = args.baseline_exe if implementation == 'before' else exe
    command = [str(binary), str(library), mode, str(count), str(samples)]
    result = subprocess.run(command, text=True, capture_output=True, check=True)
    runs.append({'command': command, 'stderr': result.stderr})
    parsed = list(csv.DictReader(io.StringIO(result.stdout)))
    for row in parsed: row['implementation'] = implementation
    return parsed
for implementation, count, mode in cases:
    run(implementation, 7, mode, 3)
for round_index in range(args.rounds):
    offset = round_index % len(cases)
    selected = cases[offset:] + cases[:offset]
    if round_index % 2: selected = list(reversed(selected))
    for implementation, count, mode in selected:
        for row in run(implementation, count, mode, args.samples):
            row['round'] = round_index + 1
            rows.append(row)
            print(f"round={round_index + 1} {implementation} {row['workload']} p50={row['p50']} us p99={row['p99']} us", flush=True)
out = root / 'docs/benchmarks'
with (out / f'{args.output_prefix}.csv').open('w', newline='') as f:
    writer = csv.DictWriter(f, fieldnames=list(rows[0])); writer.writeheader(); writer.writerows(rows)
files = ['src/app/tool/benchmark_direct_views.dart', 'src/app/tool/benchmark_dart_phases.dart',
         'src/app/tool/support/legacy_document_store.dart', 'src/app/lib/data/app_views.dart',
         'src/app/lib/data/datafriend.dart', 'src/app/lib/app_controller.dart',
         'datafriend-benchmarks/native/c_api.cpp', 'datafriend-benchmarks/native/engine.cpp', 'datafriend-benchmarks/include/datafriend/engine.h', 'datafriend-benchmarks/benchmarks/run_direct_views.py']
(out / f'{args.output_prefix}-metadata.json').write_text(json.dumps({
    'platform': platform.platform(), 'machine': platform.machine(), 'arguments': {k: str(v) if isinstance(v, Path) else v for k,v in vars(args).items()},
    'warmup': 100, 'durability': 'MDB_NOSYNC',
    'scope': 'Dart AOT app-view callback body with a publication listener; optional complete projection readback or 15 visible rows plus native completion count; excludes Flutter scheduling/rendering and network',
    'source_sha256': {f: hashlib.sha256((root/f).read_bytes()).hexdigest() for f in files},
    'binary_sha256': {str(f): hashlib.sha256(f.read_bytes()).hexdigest() for f in [exe, library] + ([args.baseline_exe] if args.baseline_exe else [])},
    'runs': runs,
}, indent=2) + '\n')
