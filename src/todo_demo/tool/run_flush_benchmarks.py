#!/usr/bin/env python3
"""Compare Datafriend's async, synchronous and buffered disk flushing in Flutter Release."""
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
parser.add_argument('--smoke', action='store_true')
parser.add_argument('--rounds', type=int, default=3)
parser.add_argument('--samples', type=int, default=30)
parser.add_argument('--warmup', type=int, default=5)
parser.add_argument('--output-prefix', default='datafriend-async-flush')
args = parser.parse_args()
if not args.skip_build:
    subprocess.run(['python3', str(PROJECT/'tool/build_demo.py'), '--flutter', args.flutter], check=True)
app = PROJECT/'build/macos/Build/Products/Release/datafriend_todo_lab.app'
exe = app/'Contents/MacOS/datafriend_todo_lab'
out = ROOT/'docs/benchmarks'
raw_dir = out/(args.output_prefix+'-runs')
raw_dir.mkdir(parents=True, exist_ok=True)
runs, summary = [], []


def run(profile, count, mode, repeat, samples, warmup):
    name = f'{mode}-{profile}-{count}-{repeat}'
    output = raw_dir/(name+'.json')
    with tempfile.TemporaryDirectory(prefix='datafriend-flush-bench-') as directory:
        config = dict(engine='datafriend', count=count, durability=profile, mode=mode,
                      samples=samples, warmup=warmup, paced=True,
                      directory=directory+'/db', output=str(output))
        path = Path(directory)/'config.json'
        path.write_text(json.dumps(config))
        start, load = time.time(), os.getloadavg()
        result = subprocess.run([str(exe)], env={**os.environ, 'TODO_BENCH_CONFIG': str(path)},
                                capture_output=True, text=True, timeout=300)
        runs.append(dict(name=name, config=config, started_unix=start,
                         elapsed_seconds=time.time()-start, load_average=load,
                         returncode=result.returncode, stdout=result.stdout, stderr=result.stderr))
        if result.returncode or not output.exists():
            raise RuntimeError(f'{name}: {result.returncode}\n{result.stdout}\n{result.stderr}')
        data = json.loads(output.read_text())
        assert data['checks'] == 'CRUD, checksums and persistence passed', data
        assert data['settings']['durability'] == profile, data['settings']
        groups = {}
        for row in data['rows']:
            for metric, value in row.items():
                if metric.endswith('_us'):
                    groups.setdefault((row['workload'], metric), []).append(value)
        for metric in ['setup_us', 'reopen_read_us', 'flush_barrier_us']:
            groups['lifecycle', metric] = [data[metric]]
        for (workload, metric), values in groups.items():
            values.sort()
            summary.append(dict(durability=profile, entities=count, mode=mode, round=repeat,
                workload=workload, metric=metric, samples=len(values),
                p50=statistics.median(values), p95=values[int(len(values)*.95)],
                maximum=max(values), mean=statistics.mean(values)))
        print(f'{name}: PASS; setup={data["setup_us"]/1000:.2f} ms', flush=True)
        for (workload, metric), values in groups.items():
            if metric in ['publish_us', 'wait_for_frame_us', 'framework_frame_us', 'post_frame_us']:
                print(f'  {workload} {metric}: p50={statistics.median(values):.1f} p95={values[int(len(values)*.95)]}', flush=True)


try:
    if args.smoke:
        run('asynchronous', 7, 'store', 0, 3, 2)
        run('asynchronous', 1000, 'frames', 0, 3, 2)
    else:
        profiles = ['asynchronous', 'synchronous', 'buffered']
        for repeat in range(1, args.rounds+1):
            order = profiles[repeat-1:] + profiles[:repeat-1]
            for count in ([100, 1000] if repeat % 2 else [1000, 100]):
                for profile in order:
                    run(profile, count, 'store', repeat, args.samples, args.warmup)
            for profile in order:
                run(profile, 1000, 'frames', repeat, 20, args.warmup)
finally:
    if summary:
        with (out/(args.output_prefix+'.csv')).open('w', newline='') as f:
            writer = csv.DictWriter(f, fieldnames=list(summary[0]))
            writer.writeheader()
            writer.writerows(summary)
    sources = [*PROJECT.glob('lib/*.dart'), PROJECT/'pubspec.lock', Path(__file__).resolve(),
        ROOT/'src/app/lib/data/datafriend.dart', ROOT/'src/app/lib/data/datafriend_builder.dart',
        ROOT/'src/app/lib/data/app_views.dart',
        ROOT/'datafriend/src/engine.cpp', ROOT/'datafriend/src/async_flush.h',
        ROOT/'datafriend/src/c_api.cpp', ROOT/'datafriend/include/datafriend/engine.h']
    binaries = [exe, app/'Contents/Frameworks/libdatafriend.dylib',
                app/'Contents/Frameworks/App.framework/Versions/A/App']
    meta = dict(platform=platform.platform(), arguments=vars(args), runs=runs,
        scope='Datafriend only, Flutter macOS Release. Async = MDB_NOSYNC plus a 100 ms '
              'coalescing flush worker; synchronous = per-commit sync; buffered = no automatic flush. '
              'All cases explicitly flush and verify reopen outside edit timers. '
              'Paced edits sleep 16 ms before the action; UI cases pause 250 ms after end-of-frame. '
              'Frame-start and end callbacks separate scheduling wait from framework frame work; '
              'update build/raster durations are matched by engine frame number. No network.',
        source_sha256={str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in sources},
        binary_sha256={str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in binaries})
    (out/(args.output_prefix+'-metadata.json')).write_text(json.dumps(meta, indent=2)+'\n')
