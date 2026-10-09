#!/usr/bin/env python3
"""Sequential old/new query matrix with identical C ABI harness and oracle checks."""
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

root = Path(__file__).resolve().parents[2]
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--baseline-library', type=Path, required=True)
p.add_argument('--samples', type=int, default=100)
p.add_argument('--warmup', type=int, default=20)
p.add_argument('--rounds', type=int, default=3)
p.add_argument('--counts', type=int, nargs='+', default=[100, 1000, 10000])
p.add_argument('--output-prefix', default='datafriend-query-planner')
p.add_argument('--validate-only', action='store_true')
args = p.parse_args()
if min(args.samples, args.rounds) < 1 or args.warmup < 0 or min(args.counts) < 32:
    p.error('invalid dimensions')
build = root / 'datafriend-benchmarks/build'
native = build / 'query-planner'
exe = build / 'query-matrix'
subprocess.run(['cmake', '-S', str(root/'datafriend-benchmarks'), '-B', str(native),
                '-DCMAKE_BUILD_TYPE=Release', '-DDATAFRIEND_TESTS=OFF', '-DDATAFRIEND_PROFILE=OFF',
                '-DDATAFRIEND_BUFFER_QUERIES=OFF', '-DDATAFRIEND_DETAIL_VIEWS=OFF'], check=True)
subprocess.run(['cmake', '--build', str(native), '-j4'], check=True)
command = ['c++', '-O3', '-DNDEBUG', '-std=c++20', '-Wall', '-Wextra',
           '-I'+str(root/'datafriend-benchmarks/include'), '-I'+str(root/'third_party/unordered_dense/include'),
           str(root/'datafriend-benchmarks/benchmarks/query_matrix.cpp'), '-o', str(exe)]
if platform.system() != 'Darwin':
    command += ['-ldl']
subprocess.run(command, check=True)
suffix = 'dylib' if platform.system() == 'Darwin' else 'so'
libraries = {'before': args.baseline_library.resolve(), 'planned': native/f'libdatafriend.{suffix}'}
shapes = ['wide', 'rare_first', 'rare_last', 'broad', 'forward', 'reverse',
          'chain', 'value_join', 'equality', 'disconnected', 'empty']
workloads = ['subscribe_'+s for s in shapes]
workloads += ['edit_'+s for s in ['wide', 'rare_last', 'forward', 'reverse', 'chain', 'value_join']]
workloads += ['predicate_flip', 'remote', 'delete_reinsert', 'batch32', 'shared32', 'distinct32', 'unrelated', 'noop']
runs, rows = [], []

def run(impl, workload, count, samples, warmup, seed, validation=False):
    cmd = [str(exe), str(libraries[impl]), workload, str(count), str(samples), str(warmup), str(seed)]
    start = time.time()
    load = os.getloadavg()
    result = subprocess.run(cmd, capture_output=True, text=True)
    runs.append({'command': cmd, 'validation': validation, 'returncode': result.returncode,
                 'started_unix': start, 'elapsed_seconds': time.time()-start,
                 'load_average': load, 'stderr': result.stderr})
    if result.returncode:
        raise RuntimeError(f'{cmd}\n{result.stdout}\n{result.stderr}')
    parsed = list(csv.DictReader(io.StringIO(result.stdout)))
    assert len(parsed) == 1, result.stdout
    parsed[0]['implementation'] = impl
    return parsed[0]

def validate_pair(pair):
    for field in ['final_facts', 'result_digest', 'notification_digest']:
        assert pair[0][field] == pair[1][field], (field, pair)

for workload in workloads:
    for samples, warmup in [(3, 2), (4, 2)]:
        validate_pair([run(impl, workload, 37, samples, warmup, 817, True) for impl in libraries])
print(f'{len(workloads)*4} small odd/even validation processes passed.', flush=True)
if args.validate_only:
    raise SystemExit(0)
cases = [(count, w) for count in args.counts for w in workloads]
for round_index in range(args.rounds):
    selected = cases[round_index:] + cases[:round_index]
    if round_index % 2:
        selected = list(reversed(selected))
    for index, (count, workload) in enumerate(selected):
        implementations = list(libraries)
        if (round_index + index) % 2:
            implementations.reverse()
        pair = []
        for impl in implementations:
            row = run(impl, workload, count, args.samples, args.warmup, 817+round_index)
            row['round'] = round_index + 1
            pair.append(row)
            rows.append(row)
        validate_pair(pair)
        print(f'round={round_index+1} n={count:5} {workload:24} ' + ' '.join(
            f"{r['implementation']}={r['p50']}us" for r in pair), flush=True)

out = root / 'docs/benchmarks'
with (out/f'{args.output_prefix}.csv').open('w', newline='') as f:
    writer = csv.DictWriter(f, fieldnames=list(rows[0]))
    writer.writeheader()
    writer.writerows(rows)
files = ['datafriend-benchmarks/native/engine.cpp', 'datafriend-benchmarks/native/c_api.cpp',
         'datafriend-benchmarks/include/datafriend/engine.h', 'datafriend-benchmarks/include/datafriend/codec.h',
         'datafriend-benchmarks/benchmarks/query_matrix.cpp', 'datafriend-benchmarks/benchmarks/run_query_matrix.py',
         'datafriend-benchmarks/tests/engine_test.cpp', 'datafriend-benchmarks/CMakeLists.txt']
metadata = {
    'platform': platform.platform(), 'arguments': {k:str(v) if isinstance(v,Path) else v for k,v in vars(args).items()},
    'scope': 'C ABI blocking call including request decode/reply copy, MDB_NOSYNC, owner handoff and queries. '
             'Subscribe timing includes compilation and initial evaluation, excludes unsubscribe. '
             'Edit request encoding, oracle checks, result export and model updates occur outside timers. '
             'No Dart, rendering, synchronous disk flush or network.',
    'source_sha256': {f:hashlib.sha256((root/f).read_bytes()).hexdigest() for f in files},
    'binary_sha256': {str(f):hashlib.sha256(f.read_bytes()).hexdigest() for f in [exe,*libraries.values()]},
    'compiler': subprocess.check_output(['c++', '--version'], text=True),
    'cmake_cache': (native/'CMakeCache.txt').read_text(), 'runs': runs,
}
(out/f'{args.output_prefix}-metadata.json').write_text(json.dumps(metadata, indent=2)+'\n')
for count, workload in cases:
    medians = {impl:statistics.median(float(r['p50']) for r in rows if r['implementation']==impl
                 and int(r['entities'])==count and r['workload']==workload) for impl in libraries}
    print(f"n={count:5} {workload:24} before={medians['before']:.3f} planned={medians['planned']:.3f} "
          f"speedup={medians['before']/medians['planned']:.2f}x")
