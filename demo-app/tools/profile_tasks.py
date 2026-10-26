#!/usr/bin/env python3
"""Rendered native task profiles on Android; opt-in disposable data, real EGL frame timestamps."""
import argparse, hashlib, json, math, random, shlex, statistics, subprocess, time
from pathlib import Path
root=Path(__file__).resolve().parents[2]
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--serial',default='ZY227364TP')
p.add_argument('--adb',default=str(Path.home()/'Library/Android/sdk/platform-tools/adb'))
p.add_argument('--counts',nargs='+',type=int,default=[12,1000])
p.add_argument('--cases',nargs='+',default=['idle','toggle','rename','filter','reorder','scroll','add_remove'])
p.add_argument('--rounds',type=int,default=2)
p.add_argument('--frames',type=int,default=360)
p.add_argument('--warmup',type=int,default=60)
p.add_argument('--prefix',default='datafriend-native-task-profile')
a=p.parse_args();adb=[a.adb,'-s',a.serial];package='dev.drawdo.sokol'
names=['idle','toggle','rename','filter','reorder','scroll','add_remove','cache_walk']
out=root/'docs/benchmarks';raw=out/(a.prefix+'-runs');raw.mkdir(parents=True,exist_ok=True)
def run(parts,check=True):return subprocess.run(parts,check=check,text=True,capture_output=True)
def shell(*parts,check=True):return run(adb+['shell',shlex.join(map(str,parts))],check=check).stdout

def device_state():
 return {name:shell(*command,check=False) for name,command in {
  'battery':['dumpsys','battery'],'thermal':['dumpsys','thermalservice'],
  'load':['cat','/proc/loadavg'],'frequencies':['cat','/sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq','/sys/devices/system/cpu/cpu4/cpufreq/scaling_cur_freq']}.items()}

from profile_report import summarize

apk=root/'demo-app/build/android/drawdo-sokol.apk'
source_dirs=['demo-app/src','demo-app/android','datafriend/src','datafriend/include','datafriend/platform']
sources=[p for folder in source_dirs for p in sorted((root/folder).rglob('*'))]
sources += sorted((root/'containers').glob('*.h'))
sources += [root/'demo-app/CMakeLists.txt',root/'datafriend/CMakeLists.txt',Path(__file__)]
meta={'started':time.time(),'args':vars(a),'device':shell('getprop','ro.product.model'),
 'apk_sha256':hashlib.sha256(apk.read_bytes()).hexdigest(),
 'sources_sha256':{str(f.relative_to(root)):hashlib.sha256(f.read_bytes()).hexdigest() for f in sources if f.is_file()},
 'scope':'real Sokol/GLES frames, optimized native engine, async disk flush, no network; automatic actions on the app thread, not Android input delivery or IME timing; EGL display-present timestamps',
 'runs':[]}
summaries=[]
try:
 shell('input','keyevent','KEYCODE_WAKEUP');shell('wm','dismiss-keyguard')
 for repeat in range(a.rounds):
  cases=[(count,case) for count in a.counts for case in a.cases];random.Random(801+repeat).shuffle(cases)
  for count,case in cases:
   name=f'{count}-{case}-{repeat+1}';before=device_state()
   temperatures=[int(l.split(':')[1]) for l in before['battery'].splitlines() if l.strip().startswith('temperature:')]
   if temperatures and max(temperatures)>=400:raise RuntimeError('Battery temperature reached 40 C')
   shell('am','force-stop',package)
   shell('run-as',package,'rm','-f','files/task-profile.json')
   shell('am','start','-W','-n',package+'/.MainActivity','--ez','tasks_profile','true',
         '--ei','profile_count',count,'--ei','profile_case',names.index(case),
         '--ei','profile_warmup',a.warmup,'--ei','profile_frames',a.frames)
   started=time.monotonic()
   while True:
    result=run(adb+['exec-out','run-as',package,'cat','files/task-profile.json'],check=False)
    if result.returncode==0 and result.stdout.lstrip().startswith('{'):
     try:
      d=json.loads(result.stdout)
      break
     except json.JSONDecodeError:pass
    if time.monotonic()-started>max(40,a.frames/20):
     raise RuntimeError('Profile timeout '+name+'\n'+shell('logcat','-d','-s','TaskProfile:I','DrawDoSokol:E','AndroidRuntime:E','*:S')[-5000:])
    time.sleep(.75)
   assert d['checks']=='passed' and d['count']==count and d['case']==names.index(case) and len(d['rows'])==a.frames
   (raw/(name+'.json')).write_text(result.stdout)
   s=summarize(d);s['round']=repeat+1;summaries.append(s)
   meta['runs'].append({'name':name,'before':before,'after':device_state(),'checks':d['checks']})
   (out/(a.prefix+'.json')).write_text(json.dumps(summaries,indent=2)+'\n')
   (out/(a.prefix+'-metadata.json')).write_text(json.dumps(meta,indent=2)+'\n')
   print(f"{name}: action {s['active_action_ms_p50']:.3f}/{s['active_action_ms_p95']:.3f} ms; frame {s['active_total_ms_p50']:.2f}/{s['active_total_ms_p95']:.2f} ms; {s['present_fps']:.1f} presented fps, {s['present_over_25ms']} gaps >25ms, EGL coverage {s['presentation_coverage']:.1%}",flush=True)
finally:
 meta['finished']=time.time();(out/(a.prefix+'-metadata.json')).write_text(json.dumps(meta,indent=2)+'\n')
 # Restore the user's ordinary task screen and database, including on failures.
 shell('am','force-stop',package);shell('am','start','-W','-n',package+'/.MainActivity')
