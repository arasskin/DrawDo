#!/usr/bin/env python3
"""Single-map and many-namespace comparison, serial fresh processes."""
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

ROOT=Path(__file__).resolve().parents[1]
REPO=ROOT.parent
ENGINES=['binary','wide','btree','btree-original']


def hashes():
    files=list((REPO/'littlebear-benchmarks/src').rglob('*.h'))
    files += list((REPO / 'littlebear-benchmarks/benchmarks/reference').glob('*.h'))
    files += list((REPO / 'third_party/xxhash').glob('*.h'))
    files+=list((REPO/'datafriend-benchmarks/build/reference/negentropy/cpp').rglob('*.h'))
    files+=list((ROOT/'include').rglob('*.h'))
    files+=list((ROOT/'benchmarks').glob('*.h'))+list((ROOT/'benchmarks').glob('*.cpp'))
    files+=list((ROOT/'benchmarks').glob('*.py'))+[ROOT/'Makefile']
    return {str(p.relative_to(REPO)):hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(files)}


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,default=ROOT/'build/btree-results')
    parser.add_argument('--seeds',default='1,2,3')
    args=parser.parse_args();args.output.mkdir(parents=True,exist_ok=True)
    compiler=os.environ.get('CXX','clang++')
    subprocess.run(['make','-C',str(ROOT),f'CXX={compiler}','build/compare'],check=True)
    before=hashes()
    seeds=[int(s) for s in args.seeds.split(',')]
    metadata={'started_utc':datetime.now(timezone.utc).isoformat(),'platform':platform.platform(),
        'compiler':subprocess.check_output([compiler,'--version'],text=True),
        'crypto':subprocess.check_output(['pkg-config','--modversion','libcrypto'],text=True).strip(),
        'upstream_negentropy':'b3687652922785c6f3b6c00684f51c6977965784',
        'source_sha256':before,'seeds':seeds,'flags':'-std=c++20 -O3 -DNDEBUG',
        'btree_node_bytes':{'original':3952,'xor':2632},
        'warmup':'Native accumulator SHA-256 called once before timers/RSS baseline to initialize OpenSSL',
        'many_trace':'200000 events; 90% traffic to 10% namespaces; 60/25/10/5 lookup/replace/range/erase+insert; empty maps get misses',
        'network':'encoded packets only; no sockets, RTT, payload files, or concurrent execution'}
    (args.output/'metadata.json').write_text(json.dumps(metadata,indent=2)+'\n')
    jobs=[]
    for seed in seeds:
        for n in [1000,100000,1000000]:
            for keys in ['uniform','sequential']:
                for engine in ENGINES:jobs.append(('micro',engine,n,seed,keys,'churn'))
        for n in [100000,1000000]:
            for scenario in ['identical','sparse','clustered']:
                for engine in ENGINES:jobs.append(('sync',engine,n,seed,'uniform',scenario))
        for maps,profile in [(20000,'tiny'),(10000,'medium'),(1000,'large'),(10000,'skewed')]:
            for engine in ENGINES:jobs.append(('many',engine,maps,seed,'uniform',profile))
    random.Random(927).shuffle(jobs)
    rows=[]
    with (args.output/'raw.jsonl').open('w') as out:
        for i,job in enumerate(jobs):
            row=json.loads(subprocess.check_output([str(ROOT/'build/compare'),*map(str,job)],text=True,timeout=180))
            rows.append(row);out.write(json.dumps(row)+'\n');out.flush()
            if (i+1)%12==0 or i+1==len(jobs):print(f'{i+1}/{len(jobs)} processes complete',flush=True)
    if hashes()!=before:raise RuntimeError('Sources changed during benchmark')
    # Count-based peers must have identical boundaries/traffic despite different
    # node layout, accumulator width, and hashing cost.
    wire={}
    for r in rows:
        if r['engine']=='binary' or r['mode']=='micro':continue
        key=tuple(r[k] for k in ['mode','count','seed','scenario'])
        fields=['turns','packets','wire_bytes','differences'] if r['mode']=='sync' else ['sync_turns_total','sync_wire_bytes','sync_differences']
        value=tuple(r[k] for k in fields)
        if key in wire and wire[key]!=value:raise RuntimeError(f'Count-based transcript mismatch: {key}')
        wire[key]=value
    groups={}
    for r in rows:
        key=tuple(r[k] for k in ['mode','count','distribution','scenario','engine'])
        groups.setdefault(key,[]).append(r)
    lines=['# B-tree and namespace comparison','','Medians of three seeds; fresh, serial processes. See metadata.json and raw.jsonl.','',
        '`btree` is the 64-bit XOR/type adaptation. `btree-original` retains upstream 32-byte IDs and accumulator/SHA-256. Both retain upstream 30/60/80 node thresholds, algorithms, and node-ID/hash-table storage.','',
        '## Single map','','| Records | Keys | Index | Node bytes/record* | RSS MiB | Lookup ns | Replace ns | Range ns | Insert ns | Erase ns |',
        '|---:|---|---|---:|---:|---:|---:|---:|---:|---:|']
    for (mode,n,keys,scenario,engine),group in sorted(groups.items()):
        if mode!='micro':continue
        med=lambda k:statistics.median(r[k] for r in group)
        lines.append(f'| {n} | {keys} | {engine} | {med("bytes_per_record"):.1f} | {med("rss_build_delta")/2**20:.2f} | '+' | '.join(f'{med(k):.1f}' for k in ['hit_ns','replace_ns','range_ns','insert_ns','erase_ns'])+' |')
    lines+=['','## Many namespaces','','| Profile | Maps | Records | Index | Accounted MiB* | RSS MiB | Mixed ns/event | Batch p95 ns/event | Drop 10% ms | Rebuild 10% ms |',
            '|---|---:|---:|---|---:|---:|---:|---:|---:|---:|']
    for (mode,n,keys,scenario,engine),group in sorted(groups.items()):
        if mode!='many':continue
        med=lambda k:statistics.median(r[k] for r in group)
        lines.append(f'| {scenario} | {n} | {med("records"):.0f} | {engine} | {med("accounted_bytes")/2**20:.2f} | {med("rss_build_delta")/2**20:.2f} | '+' | '.join(f'{med(k):.2f}' for k in ['mixed_ns_per_event','batch_p95_ns_per_event','drop_10pct_ms','rebuild_10pct_ms'])+' |')
    lines+=['','Accounted bytes include namespace wrappers/directory lower bound for the many-map cases. Binary counts reusable high-water slots; wide counts malloc requests including spare capacity. B-tree counts node-map values and buckets, excluding hash-table allocation bookkeeping. All exclude system allocator/page rounding. RSS includes these effects but is noisy.','',
            '## Namespace reconciliation (256 sessions)','','| Profile | Index | Total CPU ms | p95 elapsed ms/session | Total turns | Wire KiB |',
            '|---|---|---:|---:|---:|---:|']
    for (mode,n,keys,scenario,engine),group in sorted(groups.items()):
        if mode!='many':continue
        med=lambda k:statistics.median(r[k] for r in group)
        lines.append(f'| {scenario} | {engine} | {med("sync_cpu_ms"):.2f} | {med("sync_p95_ms"):.3f} | {med("sync_turns_total"):.0f} | {med("sync_wire_bytes")/1024:.1f} |')
    lines+=['','## Large-map reconciliation','','| Records | Changes | Index | CPU ms | Turns | Wire KiB |',
            '|---:|---|---|---:|---:|---:|']
    for (mode,n,keys,scenario,engine),group in sorted(groups.items()):
        if mode!='sync':continue
        med=lambda k:statistics.median(r[k] for r in group)
        lines.append(f'| {n} | {scenario} | {engine} | {med("cpu_ms"):.2f} | {med("turns"):.0f} | {med("wire_bytes")/1024:.1f} |')
    lines+=['','The native B-tree uses a native B-tree client to match its accumulator; XOR B-tree/binary use a wide client. Every result is checked against an exact record oracle. Bytes/turns are encoded in-process measurements, not real network latency.','']
    (args.output/'summary.md').write_text('\n'.join(lines))
    print(args.output/'summary.md')


if __name__=='__main__':main()
