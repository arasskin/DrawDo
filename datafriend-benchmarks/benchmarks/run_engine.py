#!/usr/bin/env python3
"""Run after the release CMake build and flutter pub get; writes raw measurements."""
import argparse
import datetime
import json
import platform
import subprocess
from pathlib import Path

root = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser()
parser.add_argument('--dart', default='dart', help='Dart SDK executable for AOT benchmark')
args = parser.parse_args()
build = root / 'datafriend-benchmarks/build'
output = root / 'docs/benchmarks'
output.mkdir(exist_ok=True)
subprocess.run([args.dart, 'compile', 'exe', str(root / 'src/app/tool/benchmark_datafriend.dart'),
                '-o', str(build / 'dart-benchmark')], check=True)
with (output / 'datafriend-engine.csv').open('w') as f:
    f.write('entities,identical_subscribers,additional_queries,query_evaluations,durable,p50_us,p95_us,p99_us,max_us\n')
    for entities, fanout, different, durable in [(1000, 1, 0, 1), (10000, 1, 0, 1), (10000, 10, 0, 1),
                                     (10000, 100, 0, 1), (100000, 10, 0, 1), (10000, 10, 0, 0), (10000, 10, 10, 0)]:
        result = subprocess.run([str(build / 'native/engine_benchmark'), str(entities), str(fanout), str(durable), str(different)],
                                capture_output=True, text=True, check=True)
        f.write(result.stdout)
        f.flush()
        print(result.stdout, end='', flush=True)
suffix = 'dylib' if platform.system() == 'Darwin' else 'so'
for durable in [1, 0]:
    result = subprocess.run([str(build / 'dart-benchmark'), str(build / f'native/libdatafriend.{suffix}'), str(durable)],
                            capture_output=True, text=True, check=True)
    name = 'datafriend-dart.csv' if durable else 'datafriend-dart-nosync.csv'
    (output / name).write_text(result.stdout)
    print(f'Dart durable={durable}', flush=True)
    print(result.stdout, end='', flush=True)
(output / 'datafriend-engine-environment.json').write_text(json.dumps({
    'timestamp_utc': datetime.datetime.now(datetime.timezone.utc).isoformat(),
    'host': platform.platform(), 'machine': platform.machine(),
    'native': 'CMake Release, 100 warmup + 1000 measured edits',
    'dart': 'AOT executable, 50 warmup + 200 measured edits',
    'durability': 'AELMDB default durable unless durable column is 0',
    'scope': 'CPU/FFI/commit/publication; excludes Flutter widget layout/rasterization and network',
}, indent=2) + '\n')
