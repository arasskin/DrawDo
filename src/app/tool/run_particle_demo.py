#!/usr/bin/env python3
"""Run the real Flutter particle demo on Android and preserve frame-level samples."""
import argparse
import csv
import hashlib
import json
import math
from pathlib import Path
import random
import shlex
import statistics
import subprocess
import time

root = Path(__file__).resolve().parents[3]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--adb', default=str(Path.home()/'Library/Android/sdk/platform-tools/adb'))
parser.add_argument('--serial', default='ZY227364TP')
parser.add_argument('--rounds', type=int, default=2)
parser.add_argument('--samples', type=int, default=240)
parser.add_argument('--warmup', type=int, default=60)
parser.add_argument('--counts', nargs='+', type=int, default=[100, 300, 1000, 3000])
parser.add_argument('--modes', nargs='+', choices=['local', 'all'], default=['local', 'all'])
parser.add_argument('--output-prefix', default='datafriend-android-particles')
parser.add_argument('--renderer', choices=['canvas', 'cells'], default='canvas')
parser.add_argument('--skip-install', action='store_true')
args = parser.parse_args()
adb = [args.adb, '-s', args.serial]
package = 'dev.drawdo.drawdo.particles'
component = package+'/dev.drawdo.drawdo.MainActivity'
remote = '/sdcard/Android/data/'+package+'/files/particle-result.json'
apk = root/'src/app/build/app/outputs/flutter-apk/app-profile.apk'
out = root/'docs/benchmarks'
raw = out/(args.output_prefix+'-runs')
raw.mkdir(parents=True, exist_ok=True)

def run(*parts, **kwargs):
    return subprocess.run(parts, check=True, text=True, capture_output=True, **kwargs).stdout

def shell(*parts):
    return run(*adb, 'shell', shlex.join(map(str, parts)))

def state():
    return {name: shell(*command) for name, command in {
        'battery': ['dumpsys', 'battery'],
        'thermal': ['dumpsys', 'thermalservice'],
        'load': ['cat', '/proc/loadavg'],
        'frequencies': ['cat', '/sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq',
                        '/sys/devices/system/cpu/cpu4/cpufreq/scaling_cur_freq'],
    }.items()}

def percentile(values, quantile):
    return sorted(values)[math.ceil(len(values)*quantile)-1]

metadata = {'started': time.time(), 'arguments': vars(args), 'runs': [],
            'scope': 'Flutter profile/AOT, real vsync, native production router, async disk flush, no network; '
                     'fixed 1/60 step per rendered tick, no catch-up; region views remain subscribed.',
            'apk_sha256': hashlib.sha256(apk.read_bytes()).hexdigest(),
            'source_sha256': {str(p.relative_to(root)): hashlib.sha256(p.read_bytes()).hexdigest() for p in [
                root/'src/app/lib/particle_main.dart', root/'src/app/lib/particles/simulation.dart',
                root/'src/app/lib/data/datafriend.dart', root/'datafriend/src/engine.cpp',
                Path(__file__).resolve()]}}
summaries = []
try:
    metadata['device'] = shell('getprop', 'ro.product.model')
    metadata['display'] = shell('dumpsys', 'display')
    if not args.skip_install:
        print(run(*adb, 'install', '-r', str(apk)).strip(), flush=True)
    shell('input', 'keyevent', 'KEYCODE_WAKEUP')
    shell('wm', 'dismiss-keyguard')
    for repeat in range(args.rounds):
        cases = [(n, mode) for n in args.counts for mode in args.modes]
        random.Random(934+repeat).shuffle(cases)
        for count, mode in cases:
            name = f'{count}-{mode}-{repeat+1}'
            config = {'benchmark': True, 'count': count, 'local': mode == 'local',
                      'warmup': args.warmup, 'samples': args.samples, 'renderer': args.renderer}
            shell('am', 'force-stop', package)
            shell('rm', '-f', remote)
            before = state()
            temperature = [int(line.split(':')[1]) for line in before['battery'].splitlines()
                           if line.strip().startswith('temperature:')]
            if temperature and max(temperature) >= 400:
                raise RuntimeError('Battery reached 40 C; stop to avoid heat-biased measurements')
            started = time.time()
            shell('am', 'start', '-W', '-n', component, '--es', 'particle_config', json.dumps(config))
            while shell('sh', '-c', f'test -s {shlex.quote(remote)} && echo ready || true').strip() != 'ready':
                if time.time()-started > 180:
                    raise RuntimeError('Particle benchmark timed out: '+name)
                time.sleep(1)
            file = raw/(name+'.json')
            run(*adb, 'pull', remote, str(file))
            data = json.loads(file.read_text())
            if 'error' in data:
                raise RuntimeError(json.dumps(data))
            assert len(data['rows']) == args.samples and data['config'] == config
            assert data['checks'] == 'cell memberships, values, counts and frame timing coverage passed'
            rows = data['rows']
            summary = {'count': count, 'mode': mode, 'round': repeat+1,
                       'samples': len(rows), 'setup_us': data['setup_us'],
                       'fps': 1e6/statistics.mean(r['interval_us'] for r in rows),
                       'interval_over_25ms_percent': 100*sum(r['interval_us']>25000 for r in rows)/len(rows),
                       'step_over_16_7ms_percent': 100*sum(r['step_us']>16667 for r in rows)/len(rows)}
            for metric in rows[0]:
                if metric == 'frame_number':
                    continue
                values = [r[metric] for r in rows]
                summary[metric+'_p50'] = statistics.median(values)
                summary[metric+'_p95'] = percentile(values, .95)
            summaries.append(summary)
            metadata['runs'].append({'name': name, 'config': config, 'before': before, 'after': state(),
                                     'elapsed_seconds': time.time()-started, 'checks': data['checks']})
            with (out/(args.output_prefix+'.csv')).open('w') as f:
                writer = csv.DictWriter(f, fieldnames=list(summaries[0]))
                writer.writeheader(); writer.writerows(summaries)
            (out/(args.output_prefix+'-metadata.json')).write_text(json.dumps(metadata, indent=2)+'\n')
            print(f'{name}: {summary["fps"]:.1f} fps, step median {summary["step_us_p50"]/1000:.2f} ms, '
                  f'p95 {summary["step_us_p95"]/1000:.2f} ms, {summary["moved_p50"]} moved', flush=True)
finally:
    metadata['finished'] = time.time()
    (out/(args.output_prefix+'-metadata.json')).write_text(json.dumps(metadata, indent=2)+'\n')
