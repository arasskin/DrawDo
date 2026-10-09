#!/usr/bin/env python3
"""Compare blocking app operations in Dart/native Datafriend and production Redux."""
import argparse
import csv
import hashlib
import io
import json
import os
import platform
import statistics
import subprocess
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
REDUX = ROOT / 'datafriend-benchmarks/benchmarks/redux'
BUILD = ROOT / 'datafriend-benchmarks/build'
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--dart', default='dart')
p.add_argument('--node', default='node')
p.add_argument('--samples', type=int, default=3000)
p.add_argument('--warmup', type=int, default=2000)
p.add_argument('--rounds', type=int, default=3)
p.add_argument('--output-prefix', default='datafriend-redux')
p.add_argument('--baseline-library', type=Path, help='Previous Datafriend native library; same current Dart harness')
args = p.parse_args()
if min(args.samples, args.rounds) < 1 or args.warmup < 0:
    p.error('samples/rounds must be positive and warmup nonnegative')
env = {**os.environ, 'NODE_ENV': 'production'}
if not (REDUX / 'node_modules/@reduxjs/toolkit/package.json').exists():
    p.error('First run npm ci --ignore-scripts --no-audit --no-fund in benchmarks/redux')

# Separate uninstrumented Release build: never accidentally measure a diagnostic
# library or include test-only failure injection. All builds precede timings.
native = BUILD / 'redux-native'
subprocess.run(['cmake', '-S', str(ROOT / 'datafriend-benchmarks'), '-B', str(native),
                '-DCMAKE_BUILD_TYPE=Release', '-DDATAFRIEND_TESTS=OFF',
                '-DDATAFRIEND_PROFILE=OFF', '-DDATAFRIEND_BUFFER_QUERIES=OFF',
                '-DDATAFRIEND_DETAIL_VIEWS=OFF', '-DDATAFRIEND_SQLITE_BENCHMARK=OFF'], check=True)
subprocess.run(['cmake', '--build', str(native), '-j4'], check=True)
exe = BUILD / 'dart-redux'
subprocess.run([args.dart, 'compile', 'exe', 'tool/benchmark_redux.dart', '-o', str(exe)],
               cwd=ROOT / 'src/app', check=True)
suffix = 'dylib' if platform.system() == 'Darwin' else 'so'
library = native / f'libdatafriend.{suffix}'
runs, rows = [], []
implementations = ['datafriend', 'rtk', 'rtk_no_freeze', 'redux_manual']
if args.baseline_library:
    args.baseline_library=args.baseline_library.resolve()
    implementations.insert(0,'datafriend_before')
cases = [(impl, count, mode) for count in [100, 1000]
         for mode in ['edit', 'read', 'visible'] for impl in implementations]

def run(impl, count, mode, samples, warmup, validation=False):
    command = ([str(exe), str(args.baseline_library if impl=='datafriend_before' else library), mode]
               if impl.startswith('datafriend') else
               [args.node, str(REDUX / 'benchmark.mjs'), impl, mode])
    command += [str(count), str(samples), str(warmup)]
    started = time.time()
    load = os.getloadavg()
    result = subprocess.run(command, text=True, capture_output=True, env=env)
    runs.append({'command': command, 'validation': validation, 'started_unix': started,
                 'load_average': load, 'elapsed_seconds': time.time() - started,
                 'returncode': result.returncode, 'stderr': result.stderr})
    if result.returncode:
        raise RuntimeError(f'{command}\n{result.stdout}\n{result.stderr}')
    parsed = list(csv.DictReader(io.StringIO(result.stdout)))
    assert len(parsed) == 1 and parsed[0]['phase'] == 'total', result.stdout
    for row in parsed:
        row['implementation'] = impl
        row['entities'] = count
        row['mode'] = mode
    return parsed

# Both final parities; tiny and >15 row fixtures catch truncation/count issues.
for impl in implementations:
    for mode in ['edit', 'read', 'visible']:
        for count, samples, warmup in [(7, 3, 2), (23, 4, 2)]:
            run(impl, count, mode, samples, warmup, validation=True)
print(f'All {len(implementations)*3} odd/even pairs ({len(implementations)*6} validation processes) passed.', flush=True)
for round_index in range(args.rounds):
    # Rotate system order within every workload as well as workload order.
    offset = round_index % len(cases)
    selected = cases[offset:] + cases[:offset]
    if round_index % 2:
        selected = list(reversed(selected))
    for impl, count, mode in selected:
        for row in run(impl, count, mode, args.samples, args.warmup):
            row['round'] = round_index + 1
            rows.append(row)
            print(f"round={round_index + 1} {impl:14} {mode:7} {count:4} "
                  f"p50={row['p50']} us p99={row['p99']} us", flush=True)

out = ROOT / 'docs/benchmarks'
out.mkdir(parents=True, exist_ok=True)
with (out / f'{args.output_prefix}.csv').open('w', newline='') as f:
    writer = csv.DictWriter(f, fieldnames=list(rows[0]))
    writer.writeheader()
    writer.writerows(rows)
files = ['src/app/tool/benchmark_redux.dart', 'src/app/tool/benchmark_dart_phases.dart',
         'src/app/lib/data/app_views.dart', 'src/app/lib/data/datafriend.dart',
         'src/app/lib/data/datafriend_profile.dart', 'src/app/lib/model/document.dart',
         'src/app/pubspec.lock', 'datafriend-benchmarks/CMakeLists.txt',
         'datafriend-benchmarks/native/c_api.cpp', 'datafriend-benchmarks/native/engine.cpp',
         'datafriend-benchmarks/native/aelmdb.c', 'datafriend-benchmarks/native/aelmdb_midl.c',
         'datafriend-benchmarks/include/datafriend/engine.h', 'datafriend-benchmarks/include/datafriend/c_api.h',
         'datafriend-benchmarks/benchmarks/redux/benchmark.mjs',
         'datafriend-benchmarks/benchmarks/redux/package.json',
         'datafriend-benchmarks/benchmarks/redux/package-lock.json',
         'datafriend-benchmarks/benchmarks/run_redux.py']
def output(command):
    return subprocess.check_output(command, text=True, cwd=ROOT, env=env).strip()
metadata = {
    'platform': platform.platform(), 'machine': platform.machine(),
    'arguments': {k:str(v) if isinstance(v,Path) else v for k,v in vars(args).items()},
    'node_versions': json.loads(output([args.node, '-p', 'JSON.stringify(process.versions)'])),
    'dart_version': output([args.dart, '--version']),
    'packages': json.loads(output(['npm', 'ls', '--prefix', str(REDUX), '--all', '--json'])),
    'git_head': output(['git', 'rev-parse', 'HEAD']),
    'scope': 'Synchronous task toggle, three query/selector subscriptions and publication listener; '
             'optional all task fields or 15 visible rows plus completed count. No Flutter/React '
             'rendering, network or synchronous disk flush. Datafriend still executes database '
             'transactions/index maintenance/query evaluation; Redux only maintains JS state.',
    'configuration': {'datafriend': 'Dart AOT + C++ Release, MDB_NOSYNC',
                      'rtk': 'NODE_ENV=production, default middleware, Immer auto-freeze on, DevTools off',
                      'rtk_no_freeze': 'Same as rtk, setAutoFreeze(false)',
                      'redux_manual': 'Redux createStore, hand-written immutable reducer; same selectors'},
    'source_sha256': {f: hashlib.sha256((ROOT / f).read_bytes()).hexdigest() for f in files},
    'binary_sha256': {str(f): hashlib.sha256(f.read_bytes()).hexdigest() for f in
                      [exe, library] + ([args.baseline_library] if args.baseline_library else [])},
    'cmake_cache': (native / 'CMakeCache.txt').read_text(),
    'runs': runs,
}
(out / f'{args.output_prefix}-metadata.json').write_text(json.dumps(metadata, indent=2) + '\n')
print('\nMedian of round medians (us):')
for impl, count, mode in cases:
    group = [r for r in rows if r['implementation'] == impl and r['entities'] == count and r['mode'] == mode]
    print(f'{impl:14} {mode:7} {count:4}: ' + ', '.join(
        f'{key}={statistics.median(float(r[key]) for r in group):.3f}' for key in ['p50', 'p99']))
