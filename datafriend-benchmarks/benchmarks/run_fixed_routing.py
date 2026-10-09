#!/usr/bin/env python3
"""Alternate old/new native libraries in a tight fixed-entity edit benchmark (macOS)."""
import argparse
import csv
import hashlib
import io
import json
import os
from pathlib import Path
import platform
import subprocess

ROOT = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--baseline-library', type=Path, required=True)
parser.add_argument('--current-library', type=Path, default=ROOT/'datafriend-benchmarks/build/todo-demo-native/libdatafriend.dylib')
parser.add_argument('--rounds', type=int, default=3)
args = parser.parse_args()
source = ROOT/'datafriend-benchmarks/benchmarks/fixed_routing.cpp'
exe = ROOT/'datafriend-benchmarks/build/routing-comparison'
libraries = {'baseline': args.baseline_library.resolve(), 'current': args.current_library.resolve()}
subprocess.run(['clang++', '-std=c++20', '-O3', '-DNDEBUG', '-Wall', '-Wextra', '-pthread',
                '-I'+str(ROOT/'datafriend-benchmarks/include'),
                '-I'+str(ROOT/'third_party/unordered_dense/include'), str(source),
                '-L'+str(libraries['current'].parent), '-ldatafriend',
                '-Wl,-rpath,'+str(libraries['current'].parent), '-o', str(exe)], check=True)
runs = []
for repeat in range(1, args.rounds+1):
    for subscribers in ([12,100,1000] if repeat % 2 else [1000,100,12]):
        for label in (list(libraries) if repeat % 2 else list(reversed(libraries))):
            expected = subscribers if label == 'baseline' else 1
            load = os.getloadavg()
            result = subprocess.run([str(exe), str(subscribers), str(expected)],
                env={**os.environ, 'DYLD_LIBRARY_PATH': str(libraries[label].parent)},
                capture_output=True, text=True, check=True, timeout=120)
            row = next(csv.DictReader(io.StringIO(result.stdout)))
            runs.append(dict(engine=label, round=repeat, load_average=load, **row))
            print(f'{label} round={repeat} subscribers={subscribers}: {row}', flush=True)
out = ROOT/'docs/benchmarks'
with (out/'datafriend-fixed-routing-native.csv').open('w', newline='') as f:
    writer = csv.DictWriter(f, fieldnames=[k for k in runs[0] if k != 'load_average'])
    writer.writeheader()
    writer.writerows({k:v for k,v in run.items() if k != 'load_average'} for run in runs)
sources = [source, Path(__file__).resolve(), ROOT/'datafriend-benchmarks/native/engine.cpp']
metadata = dict(platform=platform.platform(), runs=runs,
    scope='1000 entities; 12/100/1000 fixed row subscriptions; 500 warmups and 3000 title edits per process; '
          'async disk flush; blocking native edit including owner dispatch, DB, indexes and query publication; '
          'no UI or FFI; notification and routed-group counts asserted; alternating libraries in one benchmark executable.',
    source_sha256={str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in sources},
    binary_sha256={label:hashlib.sha256(p.read_bytes()).hexdigest() for label,p in {**libraries,'benchmark':exe}.items()})
(out/'datafriend-fixed-routing-native-metadata.json').write_text(json.dumps(metadata, indent=2)+'\n')
