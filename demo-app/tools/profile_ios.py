#!/usr/bin/env python3
"""Run the shared rendered task workloads on a connected iPhone."""
import argparse, hashlib, json, random, subprocess, time, uuid
from pathlib import Path
from profile_report import names, summarize
root=Path(__file__).resolve().parents[2]
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--device',default='Phone')
p.add_argument('--counts',nargs='+',type=int,default=[12,1000])
p.add_argument('--cases',nargs='+',choices=names,default=names[:7])
p.add_argument('--rounds',type=int,default=2)
p.add_argument('--frames',type=int,default=360)
p.add_argument('--warmup',type=int,default=60)
p.add_argument('--prefix',default='datafriend-ios-task-profile')
a=p.parse_args();out=root/'docs/benchmarks';raw=out/(a.prefix+'-runs');raw.mkdir(parents=True,exist_ok=True)
bundle='dev.drawdo.sokol'
def run(parts,check=True):return subprocess.run(['xcrun','devicectl',*parts],check=check,capture_output=True,text=True)
def launch(args=(),env=None):
 options=['--environment-variables',json.dumps(env)] if env else []
 return run(['device','process','launch','--device',a.device,'--terminate-existing',*options,bundle,*map(str,args)])
meta={'started':time.time(),'args':vars(a),'scope':'same scripted app-thread workloads, Sokol/Metal, async disk flush, no network; Metal drawable presentation timestamps; excludes hardware input and keyboard latency',
      'sources_sha256':{str(f.relative_to(root)):hashlib.sha256(f.read_bytes()).hexdigest() for directory in ['demo-app/src','datafriend/src','datafriend/include','datafriend/platform'] for f in sorted((root/directory).rglob('*')) if f.is_file()},'runs':[]}
meta['sources_sha256'].update({str(f.relative_to(root)):hashlib.sha256(f.read_bytes()).hexdigest() for f in sorted((root/'containers').glob('*.h'))})
binary=root/'demo-app/build/ios/Release-iphoneos/drawdo_sokol.app/drawdo_sokol'
meta['binary_sha256']=hashlib.sha256(binary.read_bytes()).hexdigest()
meta['build_files_sha256']={str(f.relative_to(root)):hashlib.sha256(f.read_bytes()).hexdigest() for f in [root/'demo-app/CMakeLists.txt',root/'datafriend/CMakeLists.txt',root/'demo-app/tools/build_ios.py',Path(__file__),root/'demo-app/tools/profile_report.py']}
cache=root/'demo-app/build/ios/CMakeCache.txt'
meta['compiler_settings']=[line for line in cache.read_text().splitlines() if line.startswith(('CMAKE_C_FLAGS_RELEASE:','CMAKE_OBJC_FLAGS_RELEASE:'))]
info=root/'demo-app/build/iphone-details.json'
if info.exists():
 device=json.loads(info.read_text())['result']
 meta['device']={k:device.get('hardwareProperties',{}).get(k) for k in ['marketingName','productType','cpuType']}
 meta['device']['os']=device.get('deviceProperties',{}).get('osVersionNumber')
summaries=[]
try:
 for repeat in range(a.rounds):
  cases=[(count,case) for count in a.counts for case in a.cases];random.Random(801+repeat).shuffle(cases)
  for count,case in cases:
   name=f'{count}-{case}-{repeat+1}';token=uuid.uuid4().hex
   launch(['--task-profile',count,names.index(case),a.warmup,a.frames],{'DATAFRIEND_PROFILE_ID':token})
   started=time.monotonic();target=raw/(name+'.json')
   while True:
    time.sleep(2)
    result=run(['device','copy','from','--device',a.device,'--domain-type','appDataContainer','--domain-identifier',bundle,
                '--source','Library/Application Support/task-profile.json','--destination',str(target)],check=False)
    if result.returncode==0:
     d=json.loads(target.read_text())
     if d.get('run_id')==token:break
    if time.monotonic()-started>max(50,a.frames/20):raise RuntimeError('Profile timeout '+name+'\n'+result.stderr)
   assert d['checks']=='passed' and d['count']==count and d['case']==names.index(case) and len(d['rows'])==a.frames
   s=summarize(d);s['round']=repeat+1;summaries.append(s);meta['runs'].append({'name':name,'checks':d['checks'],'thermal_state':d['thermal_state'],'low_power':d['low_power']})
   (out/(a.prefix+'.json')).write_text(json.dumps(summaries,indent=2)+'\n')
   (out/(a.prefix+'-metadata.json')).write_text(json.dumps(meta,indent=2)+'\n')
   if s['presentation_coverage']<.99:raise RuntimeError('Incomplete presentation timestamps in '+name+'; ensure the phone screen is awake and retry. Raw data retained.')
   print(f"{name}: action {s['active_action_ms_p50']:.3f}/{s['active_action_ms_p95']:.3f} ms; frame {s['active_total_ms_p50']:.2f}/{s['active_total_ms_p95']:.2f} ms; {s['present_fps']:.1f} fps, {s['present_over_25ms']} gaps >25ms, coverage {s['presentation_coverage']:.1%}",flush=True)
finally:
 meta['finished']=time.time();(out/(a.prefix+'-metadata.json')).write_text(json.dumps(meta,indent=2)+'\n');launch()
