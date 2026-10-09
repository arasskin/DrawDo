#!/usr/bin/env python3
"""Run the real engine on the idle Android device; no host/ADB time in samples."""
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
import subprocess
import time
import uuid

root = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser()
parser.add_argument('--serial', default='ZY227364TP')
parser.add_argument('--sdk', default=str(Path.home()/'Library/Android/sdk'))
parser.add_argument('--build', default='datafriend-benchmarks/build/android-network')
parser.add_argument('--compare-build', help='Optional baseline build; interleave before/after on each case')
parser.add_argument('--variant', action='append', nargs=2, metavar=('NAME', 'BUILD'),
                    help='Explicit named builds for a multi-way comparison; repeat for each build')
parser.add_argument('--quick', action='store_true')
parser.add_argument('--case', action='append', nargs=5,
                    metavar=('VIEWS', 'WIDTH', 'PERCENT', 'MEMBERSHIP', 'ROUTING'),
                    help='Run only these cases; repeat to select more than one')
parser.add_argument('--rounds', type=int, default=2)
parser.add_argument('--samples', type=int, default=15)
parser.add_argument('--cpu', type=int, help='Optional process CPU affinity; does not change device settings')
parser.add_argument('--output-prefix', default='datafriend-android-simulation')
args = parser.parse_args()
if args.cpu is not None and not 0 <= args.cpu < 64:
    parser.error('CPU must be between 0 and 63')
if args.variant and (args.compare_build or len({name for name, _ in args.variant}) != len(args.variant)
                     or any(not re.fullmatch(r'[a-zA-Z0-9_-]+', name) for name, _ in args.variant)):
    parser.error('Named variants require unique safe names and cannot use --compare-build')
adb = [str(Path(args.sdk)/'platform-tools/adb'), '-s', args.serial]
remote = '/data/local/tmp/datafriend-simulation-' + uuid.uuid4().hex
build = root/args.build
variants = [('before', root/args.compare_build), ('after', build)] if args.compare_build else [('current', build)]
if args.variant:
    variants = [(name, root/directory) for name, directory in args.variant]
    build = variants[0][1]
output = root/'docs/benchmarks'
output.mkdir(exist_ok=True)
metadata = {'serial': args.serial, 'started': time.time(), 'runs': [],
            'cpu_affinity': args.cpu,
            'timing': 'native modify call; asynchronous durability; no FFI/UI/network',
            'binaries': {p.name: hashlib.sha256(p.read_bytes()).hexdigest()
                         for p in [build/'libdatafriend.so', build/'simulation_benchmark', build/'network_test']}}
metadata['variants'] = {name: {p.name: hashlib.sha256(p.read_bytes()).hexdigest()
                             for p in [directory/'libdatafriend.so', directory/'simulation_benchmark', directory/'network_test']}
                        for name, directory in variants}
metadata['source_hashes'] = {str(p.relative_to(root)): hashlib.sha256(p.read_bytes()).hexdigest()
                             for p in [root/'datafriend-benchmarks/native/engine.cpp',
                                       root/'datafriend-benchmarks/benchmarks/simulation.cpp',
                                       root/'datafriend-benchmarks/CMakeLists.txt']}
metadata['build_options'] = {name: [line for line in (directory/'CMakeCache.txt').read_text().splitlines()
                                   if line.startswith(('DATAFRIEND_', 'CMAKE_BUILD_TYPE:', 'ANDROID_ABI:', 'ANDROID_PLATFORM:'))]
                             for name, directory in variants}
def run(*parts):
    return subprocess.run(parts, check=True, capture_output=True, text=True).stdout
def shell(command):
    return run(*adb, 'shell', command)
def state():
    return {'battery': shell('dumpsys battery'), 'thermal': shell('dumpsys thermalservice'),
            'cpu_frequency': shell('cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq /sys/devices/system/cpu/cpu4/cpufreq/scaling_cur_freq'),
            'battery_saver': shell('settings get global low_power'),
            'load': shell('cat /proc/loadavg'), 'time': time.time()}
rows = []
cases = [(v, 1, p, 'stable', 'fixed') for v in (100, 1000) for p in (1, 10)]
cases += [(v, w, p, mode, 'group') for v in (100, 1000)
          for w in (1, 4, 16) for p in (1, 10) for mode in ('stable', 'churn')]
if args.quick:
    cases = [(1000, 1, 10, 'stable', 'fixed'), (1000, 4, 1, 'stable', 'group'),
             (1000, 4, 10, 'stable', 'group'), (1000, 4, 10, 'churn', 'group')]
if args.case:
    cases = [(int(v), int(w), int(p), mode, routing) for v, w, p, mode, routing in args.case]
try:
    metadata['device'] = shell('getprop ro.product.model')
    shell('mkdir ' + shlex.quote(remote))
    runtime = Path(args.sdk)/'ndk/28.2.13676358/toolchains/llvm/prebuilt/darwin-x86_64/sysroot/usr/lib/aarch64-linux-android/libc++_shared.so'
    metadata['network_test'] = {}
    for name, directory in variants:
        dest = remote+'/'+name
        shell('mkdir ' + shlex.quote(dest))
        for p in [directory/'libdatafriend.so', directory/'simulation_benchmark', directory/'network_test', runtime]:
            run(*adb, 'push', str(p), dest+'/')
        shell(f'chmod 700 {dest}/simulation_benchmark {dest}/network_test')
        metadata['network_test'][name] = shell(f'LD_LIBRARY_PATH={dest} TMPDIR={remote} {dest}/network_test')
        print(name + ': ' + metadata['network_test'][name].strip(), flush=True)
    for round_index in range(args.rounds):
        order = list(cases); random.Random(9321+round_index).shuffle(order)
        paired = []
        for index, case in enumerate(order):
            if args.variant:
                # Each variant occupies every position over four rounds in a
                # four-way comparison, independent of shuffled case ordering.
                offset = round_index % len(variants)
                sequence = variants[offset:] + variants[:offset]
            else:
                sequence = variants if (round_index+index)%2 == 0 else list(reversed(variants))
            paired.extend((case, variant) for variant in sequence)
        for index, (case, (variant, _)) in enumerate(paired):
            before = state()
            battery = before['battery']
            temperatures = [int(line.split(':')[1]) for line in battery.splitlines() if line.strip().startswith('temperature:')]
            if temperatures and max(temperatures) >= 400:
                raise RuntimeError('Battery reached 40 C; stopping benchmark')
            directory = f'{remote}/case-{round_index}-{index}'
            dest = remote+'/'+variant
            command = f'LD_LIBRARY_PATH={dest} {dest}/simulation_benchmark {directory} ' + ' '.join(map(str, case)) + f' {args.samples}'
            if args.cpu is not None:
                command = f'LD_LIBRARY_PATH={dest} taskset {1 << args.cpu:x} {dest}/simulation_benchmark {directory} ' + ' '.join(map(str, case)) + f' {args.samples}'
            raw = shell(command)
            records = list(csv.DictReader(io.StringIO(raw)))
            if len(records) != args.samples:
                raise RuntimeError('Missing native samples')
            for record in records:
                record['round'] = round_index
                if args.compare_build or args.variant:
                    record['variant'] = variant
                rows.append(record)
            metadata['runs'].append({'round': round_index, 'variant': variant, 'case': case, 'before': before, 'after': state()})
            shell('rm -rf ' + shlex.quote(directory))
            print(f'round {round_index+1} case {index+1}/{len(paired)} {variant} {case}', flush=True)
            with (output/(args.output_prefix+'.csv')).open('w') as f:
                # Diagnostic variants can expose additional timing columns.
                fields = list(dict.fromkeys(key for record in rows for key in record))
                writer = csv.DictWriter(f, fieldnames=fields); writer.writeheader(); writer.writerows(rows)
            (output/(args.output_prefix+'-metadata.json')).write_text(json.dumps(metadata, indent=2))
finally:
    shell('rm -rf ' + shlex.quote(remote))
    metadata['finished'] = time.time()
    (output/(args.output_prefix+'-metadata.json')).write_text(json.dumps(metadata, indent=2))
