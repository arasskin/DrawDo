#!/usr/bin/env python3
"""Run sequential, rotated comparisons; record raw rounds and instrumented phases.

Configure both native and profile Release builds with
-DDATAFRIEND_SQLITE_BENCHMARK=ON (and DATAFRIEND_PROFILE=ON for profile).
"""
import argparse
import csv
import hashlib
import io
import json
import os
import platform
import subprocess
from pathlib import Path

root = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--output-prefix', help='result filename prefix, preserving historical runs')
parser.add_argument('--baseline-dir', type=Path, help='saved native/profile binaries for interleaved before/after runs (streaming only)')
parser.add_argument('--rounds', type=int, default=3)
parser.add_argument('--entities', type=int, default=10000)
parser.add_argument('--samples', type=int, default=1000)
parser.add_argument('--breakdown', action='store_true',
                    help='compare streaming and buffered evaluation; repeat phase profiles')
parser.add_argument('--native-breakdown', action='store_true',
                    help='also measure Datafriend using native-buffered/profile-buffered builds')
parser.add_argument('--view-breakdown', action='store_true',
                    help='profile view passes using native-detailed/profile-detailed builds')
args = parser.parse_args()
if args.view_breakdown:
    args.native_breakdown = True
if args.native_breakdown:
    args.breakdown = True
if min(args.rounds, args.entities, args.samples) < 1:
    parser.error('dimensions must be positive')
if args.baseline_dir and args.breakdown:
    parser.error('--baseline-dir supports streaming comparisons only')
out = root / 'docs/benchmarks'
out.mkdir(exist_ok=True)
backends = ['datafriend', 'sqlite_eav', 'sqlite_tasks']
variants = [(backend, 'stream') for backend in backends]
if args.breakdown:
    variants = [('datafriend', 'stream'), ('sqlite_eav', 'stream'), ('sqlite_eav', 'buffered'),
                ('sqlite_tasks', 'stream'), ('sqlite_tasks', 'buffered')]
if args.native_breakdown:
    variants.insert(1, ('datafriend', 'buffered'))
    variants = [(backend, 'facts' if backend != 'datafriend' and mode == 'buffered' else mode)
                for backend, mode in variants]
if args.view_breakdown:
    variants = [('datafriend', mode) for mode in ['stream', 'buffered', 'detailed']]
rows, phases, logs = [], [], []


def run(build, backend, extra, entities, samples, mode='stream', revision='after'):
    if backend == 'datafriend' and mode in ['buffered', 'detailed']:
        build += '-' + mode
    binary_dir = args.baseline_dir / build if revision == 'before' else root / f'datafriend-benchmarks/build/{build}'
    command = [str(binary_dir / 'sqlite_comparison'),
               backend, str(extra), str(entities), str(samples), mode]
    environment = dict(os.environ)
    # The saved executable retains the original build's rpath. Pin its library too.
    if revision == 'before':
        environment['DYLD_LIBRARY_PATH'] = str(binary_dir.resolve())
        environment['LD_LIBRARY_PATH'] = str(binary_dir.resolve())
    result = subprocess.run(command, capture_output=True, text=True, check=True, env=environment)
    logs.append({'command': command, 'revision': revision, 'stderr': result.stderr})
    return list(csv.DictReader(io.StringIO(result.stdout)))


cases = [(revision, backend, mode)
         for backend, mode in variants
         for revision in (['before', 'after'] if args.baseline_dir else ['after'])]

# Uneven group sizes and an odd number of toggles exercise expected-result checks.
for revision, backend, mode in cases:
    run('native', backend, 10, 237, 3, mode, revision)
    if mode != 'stream':
        run('profile', backend, 10, 237, 3, mode)

for round_index in range(args.rounds):
    offset = round_index % len(cases)
    order = cases[offset:] + cases[:offset]
    for extra in ([0, 10] if round_index % 2 == 0 else [10, 0]):
        for revision, backend, mode in order:
            result = run('native', backend, extra, args.entities, args.samples, mode, revision)
            for row in result:
                row['round'] = round_index + 1
                row['revision'] = revision
                rows.append(row)
                print(f"round={round_index + 1} {revision} {backend}/{mode} queries={row['queries']} "
                      f"p50={row['p50_us']} us p99={row['p99_us']} us", flush=True)

for round_index in range(args.rounds if args.breakdown or args.baseline_dir else 1):
    offset = round_index % len(cases)
    order = cases[offset:] + cases[:offset]
    for extra in ([0, 10] if round_index % 2 == 0 else [10, 0]):
        for revision, backend, mode in order:
            result = run('profile', backend, extra, args.entities, args.samples, mode, revision)
            for row in result:
                row['round'] = round_index + 1
                row['revision'] = revision
                phases.append(row)
                if row['phase'] == 'call_total':
                    print(f"profile round={round_index + 1} {revision} {backend}/{mode} "
                          f"queries={row['queries']} p50={row['p50_us']} us", flush=True)


def write_csv(name, records):
    with (out / name).open('w', newline='') as stream:
        writer = csv.DictWriter(stream, fieldnames=list(records[0]))
        writer.writeheader()
        writer.writerows(records)


prefix = 'datafriend-sqlite-breakdown' if args.breakdown else 'datafriend-sqlite'
if args.native_breakdown:
    prefix = 'datafriend-native-breakdown'
if args.view_breakdown:
    prefix = 'datafriend-view-breakdown'
if args.output_prefix:
    prefix = args.output_prefix
write_csv(f'{prefix}.csv', rows)
write_csv(f'{prefix}-profile.csv', phases)
sources = ['datafriend-benchmarks/benchmarks/sqlite.cpp', 'datafriend-benchmarks/benchmarks/run_sqlite.py',
           'datafriend-benchmarks/CMakeLists.txt', 'datafriend-benchmarks/native/engine.cpp',
           'datafriend-benchmarks/include/datafriend/persistent_wide_tree.h',
           'datafriend-benchmarks/include/datafriend/profiling.h']
metadata = {
    'platform': platform.platform(), 'machine': platform.machine(),
    'arguments': {k: str(v) if isinstance(v, Path) else v for k, v in vars(args).items()}, 'warmup_edits': 100,
    'source_sha256': {path: hashlib.sha256((root / path).read_bytes()).hexdigest() for path in sources},
    'runs': logs,
    'baseline_sha256': {str(p): hashlib.sha256(p.read_bytes()).hexdigest()
                        for p in args.baseline_dir.rglob('*') if p.is_file()} if args.baseline_dir else {},
}
(out / f'{prefix}-metadata.json').write_text(json.dumps(metadata, indent=2) + '\n')
