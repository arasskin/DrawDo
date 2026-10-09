#!/usr/bin/env python3
"""Compare the same wide tree with malloc/free and a shared size-class slab pool."""
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

def hashes():
    paths=list((ROOT/'include').rglob('*.h'))
    paths+=list((ROOT/'benchmarks').glob('*.h'))+list((ROOT/'benchmarks').glob('*.cpp'))
    paths+=list((ROOT/'benchmarks').glob('*.py'))+list((ROOT/'tests').glob('*.cpp'))+[ROOT/'Makefile']
    paths+=list((REPO/'littlebear-benchmarks/src').rglob('*.h'))
    paths += list((REPO / 'littlebear-benchmarks/benchmarks/reference').glob('*.h'))
    paths += list((REPO / 'third_party/xxhash').glob('*.h'))
    paths+=list((REPO/'datafriend-benchmarks/build/reference/negentropy/cpp').rglob('*.h'))
    return {str(p.relative_to(REPO)):hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(paths)}

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--seeds',default='1,2,3,4,5')
    parser.add_argument('--output',type=Path,default=ROOT/'build/allocator-results')
    args=parser.parse_args(); args.output.mkdir(parents=True,exist_ok=True)
    compiler=os.environ.get('CXX','clang++')
    subprocess.run(['make','-C',str(ROOT),f'CXX={compiler}','build/compare'],check=True)
    before=hashes(); seeds=[int(s) for s in args.seeds.split(',')]
    meta={'started_utc':datetime.now(timezone.utc).isoformat(),'platform':platform.platform(),
      'compiler':subprocess.check_output([compiler,'--version'],text=True),
      'flags':'-std=c++20 -O3 -DNDEBUG; see Makefile','seeds':seeds,'source_sha256':before,
      'slabs':{'bytes':65536,'max_block_bytes':4096,'empty_slabs_retained_per_class':1,
        'classes':'unchanged wide-tree capacities, 24-byte header + 8-byte slots',
        'sharing':'one single-threaded pool across all roots in many mode'},
      'replay':'real allocation lifetimes from insertion, half deletion/reinsertion and destruction; first/last bytes touched; no tree copying; four passes',
      'scope':'no threads, disk, sockets or entity payload files'}
    (args.output/'metadata.json').write_text(json.dumps(meta,indent=2)+'\n')
    pairs=[]
    for seed in seeds:
      pairs.append(('alloc',100000,seed,'uniform','churn'))
      for n in [1000,100000,1000000]:
        for keys in ['uniform','sequential']: pairs.append(('micro',n,seed,keys,'churn'))
      for maps,profile in [(20000,'tiny'),(10000,'medium'),(1000,'large'),(10000,'skewed')]:
        pairs.append(('many',maps,seed,'uniform',profile))
      for n in [100000,1000000]: pairs.append(('sync',n,seed,'uniform','sparse'))
    rng=random.Random(928);rng.shuffle(pairs);rows=[]
    with (args.output/'raw.jsonl').open('w') as out:
      for i,(mode,n,seed,keys,scenario) in enumerate(pairs):
        engines=['wide','slab'];rng.shuffle(engines);pair=[]
        for engine in engines:
          row=json.loads(subprocess.check_output([str(ROOT/'build/compare'),mode,engine,str(n),str(seed),keys,scenario],text=True,timeout=180))
          rows.append(row);pair.append(row);out.write(json.dumps(row)+'\n');out.flush()
        fields={'alloc':['allocation_requests','allocator_calls'],
          'micro':['logical_bytes','allocations','copied_bytes','checksum'],
          'many':['records','live_node_bytes_build','surviving_roots_allocations','surviving_roots_copied_bytes','checksum','sync_wire_bytes','sync_turns_total','sync_differences'],
          'sync':['wire_bytes','packets','turns','differences']}[mode]
        for f in fields:
          if pair[0][f]!=pair[1][f]:raise RuntimeError(f'Allocator changed workload: {mode}/{n}/{seed}/{f}')
        if (i+1)%5==0 or i+1==len(pairs): print(f'{2*(i+1)}/{2*len(pairs)} processes complete',flush=True)
    if hashes()!=before:raise RuntimeError('Sources changed during benchmark')
    meta['completed_utc']=datetime.now(timezone.utc).isoformat()
    (args.output/'metadata.json').write_text(json.dumps(meta,indent=2)+'\n')
    groups={}
    for r in rows:groups.setdefault(tuple(r[k] for k in ['mode','count','distribution','scenario','engine']),[]).append(r)
    lines=['# Wide-tree allocator comparison','',f'Medians of {len(seeds)} seeds; paired, serial fresh processes. `wide` = malloc/free; `slab` = size-class slabs.','']
    tables=[('alloc','Allocation replay',[('pass_0_ns_per_call','First pass ns/call',1),('pass_1_ns_per_call','Pass 1 ns/call',1),('pass_2_ns_per_call','Pass 2 ns/call',1),('pass_3_ns_per_call','Pass 3 ns/call',1)]),
      ('micro','Single-map operations',[('reserved_bytes','Reserved/requested MiB',2**20),('rss_build_delta','RSS MiB',2**20),('insert_ns','Insert ns',1),('hit_ns','Lookup ns',1),('replace_ns','Replace ns',1),('erase_ns','Erase ns',1),('reinsert_ns','Reinsert ns',1)]),
      ('many','Many maps',[('accounted_bytes','Accounted MiB',2**20),('rss_build_delta','RSS MiB',2**20),('mixed_ns_per_event','Mixed ns/event',1),('drop_10pct_ms','Drop 10% ms',1),('rebuild_10pct_ms','Refill 10% ms',1),('clear_all_ms','Clear all ms',1)]),
      ('sync','Reconciliation',[('cpu_ms','CPU ms',1),('apply_ms','Apply ms',1),('wire_bytes','Wire KiB',1024),('turns','Turns',1)])]
    for mode,title,fields in tables:
      lines+=['## '+title,'','| Count | Keys/profile | Allocator | '+' | '.join(f[1] for f in fields)+' |',
        '|---:|---|---|'+'---:|'*len(fields)]
      for (m,n,keys,scenario,engine),group in sorted(groups.items()):
        if m!=mode:continue
        med=lambda f:statistics.median(r[f] for r in group)
        lines.append(f'| {n} | {scenario if mode=="many" else keys} | {engine} | '+' | '.join(f'{med(f)/scale:.2f}' for f,_,scale in fields)+' |')
      lines.append('')
    lines+=['Slab memory includes entire slabs, headers, unused blocks and retained empty slabs, plus large malloc fallbacks. Malloc memory counts requested live node capacity, excluding allocator overhead/retention; RSS is reported separately. Many-map accounting includes shared pool and namespace wrappers/directory.','',
      'Each allocator pair has identical allocation/copy counts and live node bytes (where measured), exact record oracles, and identical reconciliation traffic. Raw results include capacity after deletion/refill, retained empty slabs, and explicit trim checks. Replay timing includes loop/marker checks; its malloc heap has already been used to construct the trace.','']
    (args.output/'summary.md').write_text('\n'.join(lines));print(args.output/'summary.md')

if __name__=='__main__':main()
