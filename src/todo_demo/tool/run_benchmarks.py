#!/usr/bin/env python3
"""Run both real Flutter backends in the same macOS Release app."""
import argparse
import csv
import hashlib
import json
import os
import platform
import statistics
import subprocess
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
PROJECT = ROOT / 'src/todo_demo'
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--flutter', default='flutter')
parser.add_argument('--skip-build', action='store_true')
parser.add_argument('--samples', type=int, default=30)
parser.add_argument('--warmup', type=int, default=5)
parser.add_argument('--rounds', type=int, default=3)
parser.add_argument('--smoke', action='store_true')
parser.add_argument('--output-prefix', default='datafriend-instantdb')
args = parser.parse_args()
if not args.skip_build:
    subprocess.run(['python3', str(PROJECT / 'tool/build_demo.py'), '--flutter', args.flutter], check=True)
app = PROJECT / 'build/macos/Build/Products/Release/datafriend_todo_lab.app'
exe = app / 'Contents/MacOS/datafriend_todo_lab'
out = ROOT / 'docs/benchmarks'
raw_dir = out / f'{args.output_prefix}-runs'
raw_dir.mkdir(parents=True, exist_ok=True)
shots = ROOT / 'docs/validation/todo-demo'
shots.mkdir(parents=True, exist_ok=True)
runs, summary = [], []


def run(engine, count, durable, mode, round_index, samples, warmup):
    name = f'{mode}-{engine}-{count}-{"durable" if durable else "buffered"}-{round_index}'
    result_path = raw_dir / f'{name}.json'
    # A fresh process and fresh database per case. Never touch the interactive demo's data.
    with tempfile.TemporaryDirectory(prefix='todo-lab-bench-') as directory:
        config = {'engine': engine, 'count': count, 'durable': durable, 'mode': mode,
                  'samples': samples, 'warmup': warmup, 'directory': directory + '/db',
                  'output': str(result_path), 'screenshot': str(shots / f'{engine}.png')}
        config_path = Path(directory) / 'config.json'
        config_path.write_text(json.dumps(config))
        started, load = time.time(), os.getloadavg()
        process = subprocess.run([str(exe)], env={**os.environ, 'TODO_BENCH_CONFIG': str(config_path)},
                                 capture_output=True, text=True, timeout=900)
        run_info = {'name': name, 'config': config, 'started_unix': started, 'load_average': load,
                    'elapsed_seconds': time.time()-started, 'returncode': process.returncode,
                    'stdout': process.stdout, 'stderr': process.stderr}
        runs.append(run_info)
        if process.returncode or not result_path.exists():
            raise RuntimeError(f'{name}: {process.returncode}\n{process.stdout}\n{process.stderr}')
        result = json.loads(result_path.read_text())
        if 'error' in result:
            raise RuntimeError(f'{name}: {result}')
        assert result['checks'] == 'CRUD, checksums and persistence passed'
        groups = {}
        for row in result['rows']:
            for metric, value in row.items():
                if metric.endswith('_us'):
                    groups.setdefault((row['workload'], metric), []).append(value)
        for metric in ['setup_us', 'reopen_read_us']:
            groups[('lifecycle', metric)] = [result[metric]]
        for (workload, metric), values in groups.items():
            values.sort()
            summary.append({'engine': engine, 'entities': count, 'durable': durable,
                'round': round_index, 'mode': mode, 'workload': workload, 'metric': metric,
                'samples': len(values), 'p50': statistics.median(values),
                'p95': values[min(len(values)-1, int(len(values)*.95))],
                'max': max(values), 'mean': statistics.mean(values)})
        print(f'{name}: {result["checks"]}; setup={result["setup_us"]/1000:.1f} ms', flush=True)
        for row in summary:
            if row['engine'] == engine and row['entities'] == count and row['round'] == round_index and row['durable'] == durable and row['mode'] == mode and row['metric'] in ['publish_us', 'post_frame_us']:
                print(f'  {row["workload"]} {row["metric"]}: p50={row["p50"]} p95={row["p95"]}', flush=True)


try:
    if args.smoke:
        for engine in ['datafriend', 'instantdb']:
            run(engine, 7, False, 'store', 0, 3, 2)
            run(engine, 6, True, 'capture', 0, 0, 0)
    else:
        for round_index in range(1, args.rounds + 1):
            engines = ['datafriend', 'instantdb'] if round_index % 2 else ['instantdb', 'datafriend']
            cases = [(count, durable) for count in [100, 1000] for durable in [False, True]]
            if round_index % 2 == 0:
                cases.reverse()
            for count, durable in cases:
                for engine in engines:
                    run(engine, count, durable, 'store', round_index, args.samples, args.warmup)
        for engine in ['datafriend', 'instantdb']:
            run(engine, 1000, True, 'frames', 1, 20, 5)
            run(engine, 6, True, 'capture', 1, 0, 0)
finally:
    if summary:
        with (out / f'{args.output_prefix}.csv').open('w', newline='') as stream:
            writer = csv.DictWriter(stream, fieldnames=list(summary[0]))
            writer.writeheader()
            writer.writerows(summary)
    sources = [*PROJECT.glob('lib/*.dart'), PROJECT / 'pubspec.lock', Path(__file__).resolve(),
               ROOT / 'src/app/lib/data/app_views.dart', ROOT / 'src/app/lib/data/datafriend.dart',
               ROOT / 'datafriend/src/engine.cpp', ROOT / 'datafriend/src/c_api.cpp']
    metadata = {'platform': platform.platform(), 'arguments': vars(args), 'runs': runs,
        'scope': 'Same Flutter macOS Release app and UI; actual sqflite platform plugin. '
                 'Community flutter_instantdb 2.3.0; sync disabled for both. '
                 'Buffered: Datafriend MDB_NOSYNC and SQLite synchronous=OFF. '
                 'Durable: backend defaults. No network comparison. '
                 'Store timers separate function return, write completion, query publication and read. '
                 'Frame timings use the real shared UI, with 250 ms pauses after completed operations.',
        'source_sha256': {str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest() for p in sources},
        'binary_sha256': {str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest() for p in
                          [exe, app / 'Contents/Frameworks/libdatafriend.dylib',
                           app / 'Contents/Frameworks/App.framework/Versions/A/App'] if p.exists()}}
    (out / f'{args.output_prefix}-metadata.json').write_text(json.dumps(metadata, indent=2) + '\n')
