#!/usr/bin/env python3
"""Separate iteration, native probes, value decoding and app projection reads."""
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
BUILD = ROOT / 'datafriend-benchmarks/build'
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--dart', default='dart')
parser.add_argument('--samples', type=int, default=3000)
parser.add_argument('--warmup', type=int, default=1000)
parser.add_argument('--rounds', type=int, default=3)
parser.add_argument('--counts', type=int, nargs='+', default=[100, 1000])
parser.add_argument('--output-prefix', default='datafriend-read-path')
parser.add_argument('--baseline-executable', type=Path)
parser.add_argument('--baseline-metadata', type=Path)
args = parser.parse_args()
if min(args.samples, args.rounds, *args.counts) < 1 or args.warmup < 0:
    parser.error('Invalid dimensions')
baseline_reference = None
if args.baseline_executable:
    args.baseline_executable = args.baseline_executable.resolve()
    if not args.baseline_metadata:
        parser.error('--baseline-executable requires --baseline-metadata for provenance')
    baseline_reference = json.loads(args.baseline_metadata.read_text())
    digest = hashlib.sha256(args.baseline_executable.read_bytes()).hexdigest()
    if digest not in baseline_reference['binary_sha256'].values():
        parser.error('Baseline executable does not match recorded metadata')
native = BUILD / 'read-path-native'
subprocess.run(['cmake', '-S', str(ROOT / 'datafriend-benchmarks'), '-B', str(native),
                '-DCMAKE_BUILD_TYPE=Release', '-DDATAFRIEND_TESTS=OFF',
                '-DDATAFRIEND_PROFILE=OFF', '-DDATAFRIEND_BUFFER_QUERIES=OFF',
                '-DDATAFRIEND_DETAIL_VIEWS=OFF', '-DDATAFRIEND_SQLITE_BENCHMARK=OFF'], check=True)
subprocess.run(['cmake', '--build', str(native), '-j4'], check=True)
exe = BUILD / 'dart-read-path'
subprocess.run([args.dart, 'compile', 'exe', 'tool/benchmark_read_path.dart', '-o', str(exe)],
               cwd=ROOT / 'src/app', check=True)
library = native / ('libdatafriend.dylib' if platform.system() == 'Darwin' else 'libdatafriend.so')
modes = ['ids_native_iter', 'ids_native_index', 'ids_dart_iter', 'order_sorted',
         'order_unordered', 'probe_fields', 'decode_fields', 'decode_preencoded',
         'construct_models', 'direct_models', 'cached_app_read', 'cached_list_read',
         'native_count', 'fresh_order', 'fresh_materialize', 'cached_export',
         'fresh_app_read', 'fresh_document', 'fresh_visible', 'edit_only',
         'edit_read', 'edit_visible']
runs, rows = [], []


def run(implementation, mode, count, samples, warmup, validation=False):
    selected_exe = args.baseline_executable if implementation == 'before' else exe
    command = [str(selected_exe), str(library), mode, str(count), str(samples), str(warmup)]
    started, load = time.time(), os.getloadavg()
    result = subprocess.run(command, text=True, capture_output=True)
    runs.append({'command': command, 'validation': validation, 'started_unix': started,
                 'load_average': load, 'elapsed_seconds': time.time() - started,
                 'returncode': result.returncode, 'stderr': result.stderr})
    if result.returncode:
        raise RuntimeError(f'{command}\n{result.stdout}\n{result.stderr}')
    parsed = list(csv.DictReader(io.StringIO(result.stdout)))
    assert len(parsed) == 1 and parsed[0]['phase'] == 'total', result.stdout
    return {**parsed[0], 'entities': count, 'mode': mode, 'implementation': implementation}


implementations = ['before', 'current'] if args.baseline_executable else ['current']
for implementation in implementations:
    for mode in modes:
        for count, samples in [(7, 3), (23, 4)]:
            run(implementation, mode, count, samples, 2, validation=True)
print(f'{len(modes) * 2 * len(implementations)} odd/even validation processes passed.', flush=True)
cases = [(mode, count) for count in args.counts for mode in modes]
for round_index in range(args.rounds):
    offset = round_index % len(cases)
    ordered = cases[offset:] + cases[:offset]
    if round_index % 2:
        ordered.reverse()
    for mode, count in ordered:
        pair = implementations if round_index % 2 == 0 else list(reversed(implementations))
        for implementation in pair:
            row = run(implementation, mode, count, args.samples, args.warmup)
            row['round'] = round_index + 1
            rows.append(row)
            print(f"round={round_index + 1} {implementation:7} {mode:20} {count:5} p50={row['p50']} p99={row['p99']} us", flush=True)

out = ROOT / 'docs/benchmarks'
out.mkdir(parents=True, exist_ok=True)
with (out / f'{args.output_prefix}.csv').open('w', newline='') as file:
    writer = csv.DictWriter(file, fieldnames=list(rows[0]))
    writer.writeheader()
    writer.writerows(rows)
sources = ['src/app/tool/benchmark_read_path.dart', 'src/app/tool/benchmark_dart_phases.dart',
           'src/app/lib/data/app_views.dart', 'src/app/lib/data/datafriend.dart',
           'src/app/lib/data/datafriend_profile.dart', 'src/app/lib/model/document.dart',
           'datafriend-benchmarks/native/engine.cpp', 'datafriend-benchmarks/native/c_api.cpp',
           'datafriend-benchmarks/benchmarks/run_read_path.py']
metadata = {
    'platform': platform.platform(), 'machine': platform.machine(),
    'arguments': {k: str(v) if isinstance(v, Path) else v for k, v in vars(args).items()},
    'dart_version': subprocess.check_output([args.dart, '--version'], text=True).strip(),
    'scope': 'Dart AOT, C++ Release, task-only fixture; no rendering, network or fsync. '
             'fresh_* excludes preparatory edits; edit_* includes them. Probes use '
             'containsField (same FFI value lookup, no decoding); decode_preencoded '
             'uses prebuilt Triple.value (same codec, Dart-owned bytes). No per-field timers.',
    'source_sha256': {f: hashlib.sha256((ROOT / f).read_bytes()).hexdigest() for f in sources},
    'binary_sha256': {str(f): hashlib.sha256(f.read_bytes()).hexdigest() for f in
                      [exe, library] + ([args.baseline_executable] if args.baseline_executable else [])},
    'baseline_reference': baseline_reference,
    'cmake_cache': (native / 'CMakeCache.txt').read_text(), 'runs': runs,
}
(out / f'{args.output_prefix}-metadata.json').write_text(json.dumps(metadata, indent=2) + '\n')
print('\nMedian of round medians (us):')
for mode, count in cases:
    for implementation in implementations:
        group = [r for r in rows if r['mode'] == mode and r['entities'] == count and
                 r['implementation'] == implementation]
        print(f'{implementation:7} {mode:20} {count:5}: ' + ', '.join(
            f'{key}={statistics.median(float(r[key]) for r in group):.3f}' for key in ['p50', 'p99']))
