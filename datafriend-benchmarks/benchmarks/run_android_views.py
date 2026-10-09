#!/usr/bin/env python3
"""Run matched view-layout microbenchmarks on the connected Android phone."""
import argparse
import csv
import hashlib
import io
import json
import os
from pathlib import Path
import random
import re
import shlex
import statistics
import subprocess
import time
import uuid

ROOT = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--sdk', type=Path, default=Path(os.environ.get('ANDROID_HOME', Path.home()/'Library/Android/sdk')))
parser.add_argument('--ndk', type=Path)
parser.add_argument('--serial')
parser.add_argument('--cpu', type=int, help='Restrict this benchmark process to one logical CPU; leave device settings unchanged')
parser.add_argument('--rounds', type=int, default=3)
parser.add_argument('--samples', type=int, default=100)
parser.add_argument('--output-prefix', default='datafriend-android-view-layouts')
args = parser.parse_args()
if args.samples <= 0 or args.samples % 2 or args.rounds <= 0:
    parser.error('positive rounds and positive even samples required')
if args.cpu is not None and not 0 <= args.cpu < 64:
    parser.error('CPU must be between 0 and 63')
adb = args.sdk/'platform-tools/adb'
devices = subprocess.check_output([str(adb), 'devices'], text=True)
serials = [line.split()[0] for line in devices.splitlines()[1:] if line.strip() and line.split()[1]=='device']
serial = args.serial or (serials[0] if len(serials)==1 else None)
if serial not in serials:
    parser.error('specify one authorized connected device with --serial')
base = [str(adb), '-s', serial]

def shell(*words, check=True):
    return subprocess.run([*base, 'shell', shlex.join(map(str, words))], check=check,
                          text=True, capture_output=True, timeout=180)

def status():
    return {name:shell(*cmd, check=False).stdout for name,cmd in {
        'battery':['dumpsys','battery'], 'load':['cat','/proc/loadavg'],
        'battery_saver':['settings','get','global','low_power'],
        'cpu_frequency':['cat','/sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq',
                         '/sys/devices/system/cpu/cpu4/cpufreq/scaling_cur_freq'],
        'thermal':['dumpsys','thermalservice']}.items()}

if 'arm64-v8a' not in shell('getprop','ro.product.cpu.abilist').stdout:
    parser.error('this runner currently builds Android arm64')
ndk = args.ndk or max((args.sdk/'ndk').iterdir(), key=lambda p:tuple(map(int,p.name.split('.'))))
toolchain = next((ndk/'toolchains/llvm/prebuilt').iterdir())
compiler = toolchain/'bin/aarch64-linux-android24-clang++'
source = ROOT/'datafriend-benchmarks/benchmarks/view_layouts.cpp'
exe = ROOT/'datafriend-benchmarks/build/view-layouts-android'
exe.parent.mkdir(parents=True,exist_ok=True)
command = [str(compiler), '-std=c++20','-O3','-DNDEBUG','-Wall','-Wextra','-static-libstdc++',
           '-I'+str(ROOT/'datafriend-benchmarks/include'),'-I'+str(ROOT/'third_party/unordered_dense/include'),
           str(source),'-o',str(exe)]
subprocess.run(command,check=True)
remote = '/data/local/tmp/drawdo-view-layouts-'+uuid.uuid4().hex
out = ROOT/'docs/benchmarks'
raw = out/(args.output_prefix+'-runs')
raw.mkdir(parents=True,exist_ok=True)
runs, all_rows = [], []
metadata = dict(serial=serial,build_command=command,compiler=subprocess.check_output([str(compiler),'--version'],text=True),
    device={p:shell('getprop',p).stdout.strip() for p in ['ro.product.model','ro.build.version.release',
        'ro.build.version.sdk','ro.product.cpu.abilist','ro.build.fingerprint','ro.hardware']},
    scope='Container-only, identical borrowed immutable-box pointers. No query/DB/FFI/Dart work. '
          'Native release arm64; all variants in one executable. Wide tree uses shared slabs and retains '
          'existing count/hash augmentation; hash values zero. Sorted dense reorders and rebuilds the hash '
          'index only after additions/deletions. Sorted merge sorts only changed keys, compacts removals '
          'and merges backwards in-place, with append/suffix fast paths. '
          'append_remove_tail includes two publications; replace_batch16 replaces up to 16 entities in one batch. '
          'Grouped scans visit entities ascending and all fields. '
          'UI order sorts by attribute 1 values. Build includes destruction; slabs reused. '
          '20 warmup samples per metric; inner batches amortize clock overhead. Checks inside/outside timers.',
    arguments={k:str(v) if isinstance(v,Path) else v for k,v in vars(args).items()},
    binary_sha256=hashlib.sha256(exe.read_bytes()).hexdigest(),
    source_sha256={str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in [
        source,Path(__file__).resolve(),ROOT/'datafriend-benchmarks/include/datafriend/wide_tree.h',
        ROOT/'datafriend-benchmarks/include/datafriend/node_allocator.h',ROOT/'third_party/unordered_dense/include/ankerl/unordered_dense.h']})
metadata['source_sha256']['datafriend-benchmarks/include/datafriend/sorted_dense_map.h']=hashlib.sha256(
    (ROOT/'datafriend-benchmarks/include/datafriend/sorted_dense_map.h').read_bytes()).hexdigest()
shell('mkdir','-p',remote)
try:
    subprocess.run([*base,'push',str(exe),remote+'/benchmark'],check=True,capture_output=True)
    shell('chmod','700',remote+'/benchmark')
    variants=['nested','flat','sorted_dense','sorted_merge','wide_slab']
    shapes=[(1,2),(100,5),(1000,1),(1000,5),(1000,32)]
    rng=random.Random(773)
    for repeat in range(1,args.rounds+1):
        cases=list(shapes); rng.shuffle(cases)
        for entities,attrs in cases:
            order=list(variants); rng.shuffle(order)
            for variant in order:
                name=f'{variant}-{entities}x{attrs}-{repeat}'
                before=status()
                # A charge-temperature guard, not a claim of fixed CPU frequency.
                temperature=re.search(r'temperature: (\d+)',before['battery'])
                if temperature and int(temperature[1])>=400:
                    raise RuntimeError('Battery reached 40 C; stop and cool the phone before timing more cases')
                start=time.time()
                affinity=[] if args.cpu is None else ['taskset',format(1 << args.cpu,'x')]
                result=shell(*affinity,remote+'/benchmark',variant,entities,attrs,args.samples,check=False)
                run=dict(name=name,layout=variant,entities=entities,attributes=attrs,round=repeat,
                         elapsed_seconds=time.time()-start,before=before,after=status(),
                         returncode=result.returncode,stderr=result.stderr)
                runs.append(run)
                (raw/(name+'.csv')).write_text(result.stdout)
                if result.returncode or 'PASS checksum=' not in result.stderr:
                    raise RuntimeError(f'{name}: {result.stdout}\n{result.stderr}')
                rows=list(csv.DictReader(io.StringIO(result.stdout)))
                for row in rows:
                    all_rows.append(dict(layout=variant,entities=entities,attributes=attrs,round=repeat,**row))
                medians={metric:round(statistics.median(float(r['us']) for r in rows if r['metric']==metric),3)
                         for metric in ['lookup_cached_entities','grouped_scan','replace_entity']}
                print(f'{name}: PASS {medians}',flush=True)
finally:
    metadata['runs']=runs
    (out/(args.output_prefix+'-metadata.json')).write_text(json.dumps(metadata,indent=2)+'\n')
    if all_rows:
        with (out/(args.output_prefix+'-samples.csv')).open('w',newline='') as f:
            w=csv.DictWriter(f,fieldnames=list(all_rows[0]));w.writeheader();w.writerows(all_rows)
        groups={}
        for row in all_rows:
            k=tuple(row[v] for v in ['layout','entities','attributes','metric'])
            groups.setdefault(k,[]).append(float(row['us']))
        with (out/(args.output_prefix+'.csv')).open('w',newline='') as f:
            w=csv.writer(f);w.writerow(['layout','entities','attributes','metric','samples','p50_us','p95_us','max_us'])
            for k,values in sorted(groups.items()):
                values.sort();w.writerow([*k,len(values),statistics.median(values),values[int(.95*len(values))],max(values)])
    shell('rm','-rf',remote,check=False) # only this run's generated directory
