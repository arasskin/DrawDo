#!/usr/bin/env python3
"""Compare bulk-list and per-row queries, checking real UI updates and rebuild scope."""
import argparse
import csv
import hashlib
import json
import os
from pathlib import Path
import platform
import statistics
import subprocess
import tempfile
import time

ROOT = Path(__file__).resolve().parents[3]
PROJECT = ROOT / 'src/todo_demo'
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--flutter', default='flutter')
parser.add_argument('--skip-build', action='store_true')
parser.add_argument('--rounds', type=int, default=3)
parser.add_argument('--samples', type=int, default=30)
parser.add_argument('--warmup', type=int, default=5)
parser.add_argument('--output-prefix', default='datafriend-row-queries')
parser.add_argument('--baseline-app', type=Path, help='Preserved app bundle to alternate with the current build')
parser.add_argument('--baseline-engine-source', type=Path, help='Preserved engine.cpp for baseline provenance')
parser.add_argument('--uis', nargs='+', choices=['legacy', 'bulk', 'rows', 'inline', 'flutter'], default=['bulk', 'rows'])
parser.add_argument('--workloads', nargs='+', choices=['toggle', 'title', 'order'], default=['toggle', 'title', 'order'])
args = parser.parse_args()
if not args.skip_build:
    subprocess.run(['python3', str(PROJECT/'tool/build_demo.py'), '--flutter', args.flutter], check=True)
app = PROJECT/'build/macos/Build/Products/Release/datafriend_todo_lab.app'
exe = app/'Contents/MacOS/datafriend_todo_lab'
engines = {'current': app}
if args.baseline_app:
    engines = {'baseline': args.baseline_app.resolve(), **engines}
out = ROOT/'docs/benchmarks'
raw = out/(args.output_prefix+'-runs')
raw.mkdir(parents=True, exist_ok=True)
runs, groups = [], {}
try:
    for repeat in range(1, args.rounds+1):
        for count in ([100, 1000] if repeat % 2 else [1000, 100]):
            for ui in (args.uis if repeat % 2 else list(reversed(args.uis))):
                for workload in args.workloads:
                    variants = list(engines.items())
                    if (repeat + (count == 1000) + args.workloads.index(workload)) % 2:
                        variants.reverse()
                    for engine, bundle in variants:
                        name = f'{ui}-{workload}-{count}-{repeat}'
                        if args.baseline_app:
                            name = engine+'-'+name
                        result_path = raw/(name+'.json')
                        with tempfile.TemporaryDirectory(prefix='datafriend-widget-bench-') as directory:
                            config = dict(mode='widgets', ui=ui, workload=workload, count=count, samples=args.samples,
                                          warmup=args.warmup, directory=directory+'/db', output=str(result_path))
                            path = Path(directory)/'config.json'
                            path.write_text(json.dumps(config))
                            started, load = time.time(), os.getloadavg()
                            p = subprocess.run([str(bundle/'Contents/MacOS/datafriend_todo_lab')],
                                               env={**os.environ, 'TODO_BENCH_CONFIG':str(path)},
                                               capture_output=True, text=True, timeout=180)
                            runs.append(dict(name=name, engine=engine, config=config, returncode=p.returncode,
                                             elapsed_seconds=time.time()-started, started_unix=started,
                                             load_average=load, stdout=p.stdout, stderr=p.stderr))
                            if p.returncode or not result_path.exists():
                                raise RuntimeError(f'{name}: {p.stdout}\n{p.stderr}')
                            data = json.loads(result_path.read_text())
                            expected = 'rendered updates and counts passed' if ui == 'flutter' else 'rendered updates, counts and persistence passed'
                            assert data['checks'] == expected, data
                            for row in data['rows']:
                                assert sum(row[k] for k in ['data_ready_us', 'wait_for_frame_us',
                                    'framework_frame_us', 'post_frame_resume_us']) == row['post_frame_us']
                                for metric, value in row.items():
                                    if metric.endswith(('_us', '_builds')):
                                        key = (ui, count, workload, metric)
                                        if args.baseline_app:
                                            key = (engine, *key)
                                        groups.setdefault(key, []).append(value)
                            print(f'{name}: PASS; ready p50={statistics.median(r["data_ready_us"] for r in data["rows"]):.1f} us; '
                                  f'build p50={statistics.median(r["build_us"] for r in data["rows"]):.1f} us', flush=True)
finally:
    with (out/(args.output_prefix+'.csv')).open('w', newline='') as f:
        writer = csv.writer(f)
        writer.writerow((['engine'] if args.baseline_app else []) + ['ui','entities','workload','metric','samples','p50','p95','maximum'])
        for key, values in sorted(groups.items()):
            values.sort()
            writer.writerow([*key,len(values),statistics.median(values),values[int(.95*len(values))],max(values)])
    sources = [*PROJECT.glob('lib/*.dart'), Path(__file__).resolve(),
               ROOT/'src/app/lib/data/datafriend_builder.dart', ROOT/'src/app/lib/data/datafriend.dart',
               ROOT/'src/app/lib/data/app_views.dart', ROOT/'datafriend/src/engine.cpp',
               ROOT/'datafriend/src/async_flush.h', ROOT/'datafriend/src/c_api.cpp',
               ROOT/'src/app/lib/data/attributes.dart', ROOT/'datafriend/schema/attributes.json',
               ROOT/'datafriend/include/datafriend/attributes.h']
    binaries = [exe, app/'Contents/Frameworks/libdatafriend.dylib', app/'Contents/Frameworks/App.framework/Versions/A/App']
    metadata = dict(platform=platform.platform(), arguments={k:str(v) if isinstance(v,Path) else v for k,v in vars(args).items()}, runs=runs,
        scope='macOS Release. Datafriend uses async flush; flutter is in-memory screen-owned setState, no database. '
              'Checkbox callbacks for toggle; direct triple edits or State methods for title/order. '
              'Flutter recomputes filtering, sorting and completion count during build. '
              'Bulk uses the previous full-list query; rows uses order-only list plus mounted row queries. Counter query shared by both. '
              '30 measured edits after 5 warmups by default, 250 ms pause after each frame. '
              'Actual edited frame build/raster matched by engine frame number. Bulk and rows have identical visual layout. '
              'App builder callbacks counted; per-action row rebuild scope checked. No network; seed, lookup, checks and close/reopen outside timers.',
        source_sha256={str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in sources},
        binary_sha256={str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in binaries})
    if args.baseline_app:
        metadata['baseline_binary_sha256'] = {
            str(p.relative_to(app)): hashlib.sha256((args.baseline_app/p.relative_to(app)).read_bytes()).hexdigest()
            for p in binaries}
    if args.baseline_engine_source:
        metadata['baseline_engine_source_sha256'] = hashlib.sha256(args.baseline_engine_source.read_bytes()).hexdigest()
    (out/(args.output_prefix+'-metadata.json')).write_text(json.dumps(metadata, indent=2)+'\n')
