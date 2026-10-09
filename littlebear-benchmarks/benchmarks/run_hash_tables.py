#!/usr/bin/env python3
"""Reproduce the DrawDo littlebear/unordered_dense comparison."""
import argparse
import hashlib
import json
import os
import platform
import random
import statistics
import subprocess
from datetime import datetime, timezone
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
REPO = ROOT.parent
# 'current' retains its archived meaning: the former two-choice implementation.
ENGINES = ['current', 'dense_prehashed', 'dense_xxhash', 'integrated']

def hashes():
    paths = list((ROOT / 'src').rglob('*.h'))
    paths += list((REPO / 'littlebear-benchmarks/benchmarks/reference').glob('*.h'))
    paths += list((REPO / 'third_party/xxhash').glob('*.h'))
    paths += list((REPO / 'datafriend-benchmarks/include').rglob('*.h'))
    paths += [Path(__file__), ROOT / 'benchmarks/compare_hash_tables.cpp',
              ROOT / 'benchmarks/reference/hash_table.h']
    paths += list((REPO / 'third_party/unordered_dense').rglob('*.h'))
    return {str(p.relative_to(REPO)): hashlib.sha256(p.read_bytes()).hexdigest()
            for p in sorted(paths)}

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--counts', default='64,1024,16384,65536,100000')
    parser.add_argument('--seeds', default='1,2,3,4,5')
    parser.add_argument('--output', type=Path, default=ROOT / 'build/hash-table-results')
    parser.add_argument('--undefined', action='store_true')
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    build = ROOT / 'build/hash-comparison'
    build.mkdir(parents=True, exist_ok=True)
    compiler = os.environ.get('CXX', 'clang++')
    flags = ['-std=c++20', '-Wall', '-Wextra', '-O3', '-DNDEBUG']
    if args.undefined:
        flags = ['-std=c++20', '-Wall', '-Wextra', '-O1', '-g',
                 '-fsanitize=undefined', '-fno-sanitize-recover=all']
    executable = build / ('compare-undefined' if args.undefined else 'compare')
    command = [compiler, *flags, '-I' + str(ROOT / 'src'),
               '-I' + str(REPO / 'third_party/unordered_dense/include'),
               str(ROOT / 'benchmarks/compare_hash_tables.cpp'), '-o', str(executable)]
    before = hashes()
    subprocess.run(command, check=True)
    assert hashes() == before, 'Sources changed during compilation'
    counts = [int(n) for n in args.counts.split(',')]
    seeds = [int(n) for n in args.seeds.split(',')]
    meta = dict(started_utc=datetime.now(timezone.utc).isoformat(),
                platform=platform.platform(), processor=platform.processor(),
                compiler=subprocess.check_output([compiler, '--version'], text=True),
                command=command, seeds=seeds, counts=counts, source_sha256=before,
                unordered_dense_version='5.1.0',
                unordered_dense_commit='b7cf409efee3a4419191bb3db29054b2ff6dc02d',
                memory='Object + requested array capacities, excludes allocator overhead and RSS',
                scope='Serial fresh processes, default growth policies, prehashed random 128-bit keys, 24-byte values; no tree operations or I/O')
    if platform.system() == 'Darwin':
        cpu = subprocess.run(['sysctl', '-n', 'machdep.cpu.brand_string'], text=True, capture_output=True)
        meta['cpu'] = cpu.stdout.strip() if cpu.returncode == 0 else 'unavailable in sandbox'
    (args.output / 'metadata.json').write_text(json.dumps(meta, indent=2) + '\n')
    pairs = [(n, seed) for n in counts for seed in seeds]
    rng = random.Random(928)
    rng.shuffle(pairs)
    rows = []
    with (args.output / 'raw.jsonl').open('w') as out:
        for n, seed in pairs:
            engines = ENGINES.copy()
            rng.shuffle(engines)
            checksums = []
            for engine in engines:
                row = json.loads(subprocess.check_output([str(executable), engine, str(n), str(seed)], text=True, timeout=120))
                rows.append(row)
                out.write(json.dumps(row) + '\n')
                out.flush()
                if row['status'] == 'ok': checksums.append(row['checksum'])
            assert len(set(checksums)) == 1
            print(f'{len(rows)}/{len(pairs)*len(ENGINES)} processes complete', flush=True)
    assert hashes() == before, 'Sources changed during measurement'
    meta['completed_utc'] = datetime.now(timezone.utc).isoformat()
    (args.output / 'metadata.json').write_text(json.dumps(meta, indent=2) + '\n')
    lines = ['# Hash-table comparison', '',
             f'Medians of {len(seeds)} seeds; {len(rows)} serial fresh processes. Times in ns; churn is per erase/insert pair.', '',
             '| Entries | Engine | Insert | Hit | Miss | Update | Churn pair | Erase | Build bytes/entry | Churn bytes/entry | Occupancy |',
             '|---:|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|']
    for n in counts:
        for engine in ENGINES:
            group = [r for r in rows if r['count'] == n and r['engine'] == engine]
            ok = [r for r in group if r['status'] == 'ok']
            if len(ok) != len(group):
                lines.append(f'| {n} | {engine} | capacity failure in {len(group)-len(ok)}/{len(group)} runs | | | | | | | | |')
                continue
            med = lambda f: statistics.median(r[f] for r in ok)
            times = ' | '.join(f'{med(f):.2f}' for f in ['insert_ns', 'hit_ns', 'miss_ns', 'update_ns', 'churn_pair_ns', 'erase_ns'])
            lines.append(f'| {n} | {engine} | {times} | {med("build_bytes")/n:.2f} | {med("churn_bytes")/n:.2f} | {100*n/med("slots"):.1f}% |')
    (args.output / 'summary.md').write_text('\n'.join(lines) + '\n')

if __name__ == '__main__':
    main()
