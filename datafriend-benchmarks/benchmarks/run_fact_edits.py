#!/usr/bin/env python3
"""Matched old/new native edit timings for entity versus fact storage (macOS)."""
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
parser.add_argument('--current-library', type=Path, required=True)
parser.add_argument('--rounds', type=int, default=3)
args = parser.parse_args()
source = ROOT/'datafriend-benchmarks/benchmarks/fact_edits.cpp'
exe = ROOT/'datafriend-benchmarks/build/fact-edits-comparison'
libraries = {'entities':args.baseline_library.resolve(), 'facts':args.current_library.resolve()}
subprocess.run(['clang++','-std=c++20','-O3','-DNDEBUG','-Wall','-Wextra','-pthread',
    '-I'+str(ROOT/'datafriend-benchmarks/include'),'-I'+str(ROOT/'third_party/unordered_dense/include'),
    str(source),'-L'+str(libraries['facts'].parent),'-ldatafriend',
    '-Wl,-rpath,'+str(libraries['facts'].parent),'-o',str(exe)],check=True)
runs=[]
for repeat in range(1,args.rounds+1):
    shapes=['task5','wide32','blob32k']
    if repeat%2==0: shapes.reverse()
    for shape in shapes:
        for workload in ['toggle','two_fields']:
            labels=list(libraries)
            if (repeat+(workload=='two_fields'))%2: labels.reverse()
            for label in labels:
                load=os.getloadavg()
                result=subprocess.run([str(exe),shape,workload],
                    env={**os.environ,'DYLD_LIBRARY_PATH':str(libraries[label].parent)},
                    capture_output=True,text=True,check=True,timeout=120)
                row=next(csv.DictReader(io.StringIO(result.stdout)))
                runs.append(dict(storage=label,round=repeat,load_average=load,**row))
                print(f'{label} {repeat} {shape} {workload}: {row}',flush=True)
out=ROOT/'docs/benchmarks'
with (out/'datafriend-fact-edits-native.csv').open('w',newline='') as f:
    writer=csv.DictWriter(f,fieldnames=[k for k in runs[0] if k!='load_average'])
    writer.writeheader(); writer.writerows({k:v for k,v in r.items() if k!='load_average'} for r in runs)
sources=[source,Path(__file__).resolve(),ROOT/'datafriend-benchmarks/native/engine.cpp',
    ROOT/'datafriend-benchmarks/schema/attributes.json',ROOT/'datafriend-benchmarks/include/datafriend/attributes.h']
metadata=dict(platform=platform.platform(),runs=runs,
    scope='1000 entities; 12 row queries, one counter and one order list. 500 warmups, 2000 measured edits. '
          'Async flushing enabled for both; initial seed explicitly flushed before timing. '
          'Native blocking edit includes owner dispatch, storage/index/query updates. No UI/FFI. '
          'Exact notification IDs, query counts and final record contents checked. '
          'One driver alternates preserved entity-record and new fact-record libraries; fresh store each process.',
    source_sha256={str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in sources},
    binary_sha256={k:hashlib.sha256(p.read_bytes()).hexdigest() for k,p in {**libraries,'driver':exe}.items()})
(out/'datafriend-fact-edits-native-metadata.json').write_text(json.dumps(metadata,indent=2)+'\n')
