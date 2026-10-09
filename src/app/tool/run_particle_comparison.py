#!/usr/bin/env python3
"""Matched headless Dart/C++ simulation, same APK/engine/thread affinity."""
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

root=Path(__file__).resolve().parents[3]
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--serial',default='ZY227364TP')
p.add_argument('--rounds',type=int,default=2)
p.add_argument('--counts',type=int,nargs='+',default=[100,300,1000,3000])
p.add_argument('--modes',nargs='+',choices=['local','all'],default=['local','all'])
p.add_argument('--warmup',type=int,default=60)
p.add_argument('--samples',type=int,default=240)
p.add_argument('--cpu',type=int,default=0)
p.add_argument('--skip-install',action='store_true')
p.add_argument('--output-prefix',default='datafriend-particle-native')
a=p.parse_args()
adb=[str(Path.home()/'Library/Android/sdk/platform-tools/adb'),'-s',a.serial]
pkg='dev.drawdo.drawdo.particles'
remote=f'/sdcard/Android/data/{pkg}/files/particle-comparison.json'
apk=root/'src/app/build/app/outputs/flutter-apk/app-profile.apk'
out=root/'docs/benchmarks';raw=out/(a.output_prefix+'-runs');raw.mkdir(parents=True,exist_ok=True)
def run(*args):return subprocess.run(args,check=True,text=True,capture_output=True).stdout
def shell(*args):return run(*adb,'shell',shlex.join(map(str,args)))
def state():return {k:shell(*v) for k,v in {'battery':['dumpsys','battery'],'thermal':['dumpsys','thermalservice'],'load':['cat','/proc/loadavg'],'frequency':['cat','/sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq']}.items()}
meta={'started':time.time(),'arguments':vars(a),'runs':[],'apk_sha256':hashlib.sha256(apk.read_bytes()).hexdigest(),
 'scope':'Same process, production engine, async flush, no rendering/pacing/network. Caller pinned before engine threads are created; no device clock changes.',
 'sources':{str(f.relative_to(root)):hashlib.sha256(f.read_bytes()).hexdigest() for f in [root/'src/app/benchmarks/particles.cpp',root/'src/app/benchmarks/CMakeLists.txt',root/'src/app/lib/particles/comparison.dart',root/'src/app/lib/particles/simulation.dart',root/'src/app/lib/data/datafriend.dart',root/'datafriend/src/engine.cpp',Path(__file__).resolve()]}}
summaries=[]
try:
 meta['device']=shell('getprop','ro.product.model')
 if not a.skip_install:print(run(*adb,'install','-r',str(apk)).strip(),flush=True)
 shell('input','keyevent','KEYCODE_WAKEUP');shell('wm','dismiss-keyguard')
 for repeat in range(a.rounds):
  cases=[(n,m) for n in a.counts for m in a.modes];random.Random(883+repeat).shuffle(cases)
  for index,(count,mode) in enumerate(cases):
   name=f'{count}-{mode}-{repeat+1}'
   variants=['dart','native'] if (repeat+index)%2==0 else ['native','dart']
   config={'compare':True,'count':count,'local':mode=='local','warmup':a.warmup,'samples':a.samples,'cpu':a.cpu,'variants':variants}
   shell('am','force-stop',pkg);shell('rm','-f',remote)
   before=state()
   if any(int(l.split(':')[1])>=400 for l in before['battery'].splitlines() if l.strip().startswith('temperature:')):raise RuntimeError('Battery reached 40 C')
   start=time.time();shell('am','start','-W','-n',pkg+'/dev.drawdo.drawdo.MainActivity','--es','particle_config',json.dumps(config))
   while not shell('sh','-c',f'test -s {shlex.quote(remote)} && echo ready || true').strip():
    if time.time()-start>300:raise RuntimeError('Timeout: '+name)
    time.sleep(1)
   file=raw/(name+'.json');run(*adb,'pull',remote,str(file));data=json.loads(file.read_text())
   if 'error' in data:raise RuntimeError(json.dumps(data))
   assert data['config']==config
   for variant in variants:
    rows=data['results'][variant]['rows'];assert len(rows)==a.samples
    summary={'count':count,'mode':mode,'round':repeat+1,'variant':variant,'profile':data['profile'],'max_coordinate_error':data['max_coordinate_error']}
    for k in rows[0]:
     values=sorted(r[k] for r in rows)
     summary[k+'_p50']=statistics.median(values);summary[k+'_p95']=values[math.ceil(len(values)*.95)-1]
    summaries.append(summary)
   meta['runs'].append({'name':name,'config':config,'before':before,'after':state(),'checks':data['checks'],'seconds':time.time()-start})
   fields=list(dict.fromkeys(k for row in summaries for k in row))
   with (out/(a.output_prefix+'.csv')).open('w') as f:
    writer=csv.DictWriter(f,fieldnames=fields);writer.writeheader();writer.writerows(summaries)
   (out/(a.output_prefix+'-metadata.json')).write_text(json.dumps(meta,indent=2)+'\n')
   print(name+': '+', '.join(f'{s["variant"]} {s["step_us_p50"]/1000:.3f} ms (p95 {s["step_us_p95"]/1000:.3f})' for s in summaries[-2:])+f'; state error {data["max_coordinate_error"]:.3g}',flush=True)
finally:
 meta['finished']=time.time();(out/(a.output_prefix+'-metadata.json')).write_text(json.dumps(meta,indent=2)+'\n')
