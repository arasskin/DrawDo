#!/usr/bin/env python3
"""Run each case in a fresh process, serially. Preserve raw results + provenance."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import random
import statistics
import subprocess
from datetime import datetime, timezone

ROOT = Path(__file__).resolve().parents[1]
REPO = ROOT.parent


def source_hashes():
    paths = list((REPO / 'littlebear-benchmarks/src').rglob('*.h'))
    paths += list((REPO / 'littlebear-benchmarks/benchmarks/reference').glob('*.h'))
    paths += list((REPO / 'third_party/xxhash').glob('*.h'))
    paths += list((ROOT / 'include').rglob('*.h'))
    paths += list((ROOT / 'benchmarks').glob('*.h'))
    paths += [ROOT / 'benchmarks/compare.cpp']
    return {str(p.relative_to(REPO)): hashlib.sha256(p.read_bytes()).hexdigest()
            for p in sorted(paths)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sizes', default='1000,100000')
    parser.add_argument('--seeds', default='1,2,3')
    parser.add_argument('--output', type=Path, default=ROOT / 'build/results')
    args = parser.parse_args()
    sizes = [int(s) for s in args.sizes.split(',')]
    seeds = [int(s) for s in args.seeds.split(',')]
    args.output.mkdir(parents=True, exist_ok=True)
    initial_hashes = source_hashes()
    compiler = os.environ.get('CXX', 'clang++')
    subprocess.run(['make', '-C', str(ROOT), f'CXX={compiler}', 'build/compare'], check=True)
    metadata = {
        'started_utc': datetime.now(timezone.utc).isoformat(),
        'platform': platform.platform(), 'machine': platform.machine(),
        'compiler': subprocess.check_output([compiler, '--version'], text=True),
        'sizes': sizes, 'seeds': seeds,
        'git_head': subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=REPO, text=True).strip(),
        'source_sha256': initial_hashes,
        'flags': '-std=c++20 -O3 -DNDEBUG; see Makefile',
        'transport': 'encoded packets in memory; no sockets, RTT, payload transfer, or disk I/O',
    }
    if platform.system() == 'Darwin':
        cpu = subprocess.run(['sysctl', '-n', 'machdep.cpu.brand_string'], text=True, capture_output=True)
        metadata['cpu'] = cpu.stdout.strip() if cpu.returncode == 0 else 'unavailable in sandbox'
    (args.output / 'metadata.json').write_text(json.dumps(metadata, indent=2) + '\n')
    jobs = []
    for n in sizes:
        for seed in seeds:
            for distribution in ['uniform', 'sequential', 'clustered']:
                for engine in ['binary', 'wide']:
                    jobs.append(('micro', engine, n, seed, distribution, 'churn'))
            for scenario in ['identical', 'sparse', 'clustered', 'client-empty', 'server-empty', 'disjoint']:
                for engine in ['binary', 'binary-scan', 'wide']:
                    jobs.append(('sync', engine, n, seed, 'uniform', scenario))
    random.Random(911).shuffle(jobs)
    rows = []
    with (args.output / 'raw.jsonl').open('w') as output:
        for i, job in enumerate(jobs):
            result = subprocess.check_output([str(ROOT / 'build/compare'), *map(str, job)], text=True)
            row = json.loads(result)
            rows.append(row)
            output.write(json.dumps(row) + '\n'); output.flush()
            if (i+1) % 12 == 0 or i+1 == len(jobs):
                print(f'{i+1}/{len(jobs)} benchmark processes complete', flush=True)
    if initial_hashes != source_hashes():
        raise RuntimeError('Sources changed during measurement; rerun to obtain a consistent comparison')
    # Balanced scan control must have exactly the same wire transcript size as
    # the cached-count wide tree. Otherwise this is not an apples-to-apples control.
    controls = {}
    for r in rows:
        if r['mode'] == 'sync' and r['engine'] in ('wide', 'binary-scan'):
            key = (r['count'], r['seed'], r['scenario'])
            wire = (r['turns'], r['packets'], r['wire_bytes'], r['differences'])
            if key in controls and controls[key] != wire:
                raise RuntimeError(f'Balanced control transcript differs: {key}')
            controls[key] = wire
    groups = {}
    for r in rows:
        key = tuple(r[k] for k in ('mode', 'count', 'distribution', 'scenario', 'engine'))
        groups.setdefault(key, []).append(r)
    lines = ['# Tree comparison', '',
             'Medians across seeds, each in a separate process. See metadata.json and raw.jsonl.', '',
             '## Tree operations', '',
             '| Records | Keys | Tree | Bytes/record* | RSS delta MiB | Hit ns | Replace ns | Range ns | Insert ns | Erase ns | Reinsert ns |',
             '|---:|---|---|---:|---:|---:|---:|---:|---:|---:|---:|']
    for (mode, n, distribution, scenario, engine), group in sorted(groups.items()):
        if mode != 'micro':
            continue
        med = lambda k: statistics.median(r[k] for r in group)
        lines.append(f'| {n} | {distribution} | {engine} | {med("bytes_per_record"):.1f} | '
                     f'{med("rss_build_delta")/2**20:.2f} | ' + ' | '.join(
                         f'{med(k):.1f}' for k in ['hit_ns', 'replace_ns', 'range_ns', 'insert_ns', 'erase_ns', 'reinsert_ns']) + ' |')
    lines += ['', '*Bytes/record: wide requested allocations including spare capacity; binary high-water slots including reusable free gaps. '
              'Both exclude allocator/page rounding. RSS deltas are noisy process measurements, not exact tree ownership.', '',
              '## Reconciliation', '',
              '| Records | Differences | Server strategy | CPU ms | Sequential turns | Packets | Wire KiB | Apply ms |',
              '|---:|---|---|---:|---:|---:|---:|---:|']
    for (mode, n, distribution, scenario, engine), group in sorted(groups.items()):
        if mode != 'sync':
            continue
        med = lambda k: statistics.median(r[k] for r in group)
        lines.append(f'| {n} | {scenario} | {engine} | {med("cpu_ms"):.2f} | {med("turns"):.0f} | '
                     f'{med("packets"):.0f} | {med("wire_bytes")/1024:.1f} | {med("apply_ms"):.2f} |')
    lines += ['', 'Client always uses wide-tree counts. Server binary uses separator splits; binary-scan computes balanced splits '
              'by enumeration; wide uses cached counts. All use the same codec, 16-way target, 32-entry list threshold, '
              'and 64 KiB packets. Turns include a terminal empty message. Packets sent together do not add turns.', '',
              'CPU ms is process CPU time during in-process reconciliation, including encoding, decoding, and difference collection. '
              'It excludes tree construction, actual network delay, payload transfer, and apply time. '
              'Raw rows also report elapsed time. A latency model may add turns × RTT/2 to elapsed time, '
              'but this is not a measured network result.', '']
    (args.output / 'summary.md').write_text('\n'.join(lines))
    print(f'Results: {args.output / "summary.md"}')


if __name__ == '__main__':
    main()
