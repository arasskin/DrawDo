#!/usr/bin/env python3
"""Pair existing native phone installations, run document sync, collect raw results.
Only public keys/config/reports cross the development-tool boundary; signing keys
are created and retained by Datafriend inside each app's secure platform store.
"""
import argparse, subprocess, json, time, uuid, hashlib, statistics, socket, struct, threading, atexit
from one_way_latency import analyze as one_way_latency, frame_start
from pathlib import Path
root=Path(__file__).resolve().parents[2]
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--host',required=True);p.add_argument('--port',type=int,default=17879)
p.add_argument('--iphone',default='Phone');p.add_argument('--android',default='ZY227364TP')
p.add_argument('--case',choices=['interactive','typing','concurrent','offline'],default='concurrent')
p.add_argument('--seed',type=int,default=1000);p.add_argument('--duration',type=int,default=25)
p.add_argument('--room');p.add_argument('--lead',type=int,default=25)
p.add_argument('--prefix',default='datafriend-document-sync')
p.add_argument('--probe-interval-ms',type=int,default=0,help='Enable request timing and same-connection heartbeat probes')
p.add_argument('--clock-host',help='Mac LAN IPv4 address for cross-phone clock calibration and one-way measurements')
a=p.parse_args();bundle='dev.drawdo.sokol';adb=str(Path.home()/'Library/Android/sdk/platform-tools/adb')
out=root/'docs/benchmarks'/a.prefix;out.mkdir(parents=True,exist_ok=True)
room=a.room or ('shared' if a.case=='interactive' else uuid.uuid4().hex)
work=root/'demo-app/build/document-sync';work.mkdir(parents=True,exist_ok=True)
def run(args,check=True,**kwargs):return subprocess.run(list(map(str,args)),check=check,capture_output=True,**kwargs)
def android(*args,**kwargs):return run([adb,'-s',a.android,*args],**kwargs)
def ios(*args,**kwargs):
 check=kwargs.pop('check',True)
 for attempt in range(3):
  result=run(['xcrun','devicectl',*args],check=False,**kwargs)
  if result.returncode==0 or args[:2] not in [('device','copy'),('device','info')]:break
  time.sleep(1)
 if check and result.returncode:raise RuntimeError(result.stderr.decode())
 return result
def get_ios(source,dest):return ios('device','copy','from','--device',a.iphone,'--domain-type','appDataContainer','--domain-identifier',bundle,'--source','Library/Application Support/'+source,'--destination',dest,check=False)
def launch():
 android('shell','input','keyevent','KEYCODE_WAKEUP');android('shell','wm','dismiss-keyguard');android('shell','am','force-stop',bundle)
 android('shell','am','start','-W','-n',bundle+'/.MainActivity')
 process_file=work/'iphone-processes.json'
 ios('device','info','processes','--device',a.iphone,'--json-output',process_file)
 for process in json.loads(process_file.read_text())['result']['runningProcesses']:
  if '/drawdo_sokol.app/drawdo_sokol' in process.get('executable',''):
   ios('device','process','terminate','--device',a.iphone,'--pid',process['processIdentifier'])
 time.sleep(2)  # Let SpringBoard finish the previous scene before creating another.
 ios('device','process','launch','--device',a.iphone,bundle)
# Both apps must have run once to create and export their public identities.
ak=android('exec-out','run-as',bundle,'cat','files/document-public-key.txt').stdout.decode().strip()
f=work/'iphone-public-key.txt';r=get_ios('document-public-key.txt',f)
if r.returncode:raise RuntimeError('Launch the installed iPhone app once to create its identity: '+r.stderr.decode())
ik=f.read_text().strip()
for key in [ak,ik]:
 if len(key)!=64 or any(c not in '0123456789abcdef' for c in key):raise RuntimeError('Invalid public identity')
if ak==ik:raise RuntimeError('Phones must have different identities')
clock_port=0
if a.clock_host:
 clock_socket=socket.socket(socket.AF_INET,socket.SOCK_DGRAM);clock_socket.bind((a.clock_host,0));clock_socket.settimeout(.2)
 clock_port=clock_socket.getsockname()[1];clock_stop=threading.Event()
 def clock_server():
  while not clock_stop.is_set():
   try:
    packet,address=clock_socket.recvfrom(64);received=time.monotonic_ns()//1000
    if len(packet)==12 and packet[:4]==b'DCLK':clock_socket.sendto(packet+struct.pack('!QQ',received,time.monotonic_ns()//1000),address)
   except socket.timeout:pass
   except OSError:break
 clock_thread=threading.Thread(target=clock_server,daemon=True);clock_thread.start()
 def stop_clock():clock_stop.set();clock_thread.join(timeout=1);clock_socket.close()
 atexit.register(stop_clock)
start=int(time.time()*1000)+a.lead*1000 if a.case!='interactive' else 0
for role,key in enumerate([ik,ak]):
 clock_config=f' {a.clock_host} {clock_port}' if a.clock_host else ''
 config=f'{a.host} {a.port} {key} {room} {role} { ["interactive","typing","concurrent","offline"].index(a.case)} {a.seed} {start} {a.duration*1000} {a.probe_interval_ms}{clock_config}\n'
 file=work/f'config-{role}.txt';file.write_text(config)
 if role==0:
  android('shell','run-as',bundle,'sh','-c',"'cat > files/document-config.txt'",input=config.encode())
 else:
  r=ios('device','copy','to','--device',a.iphone,'--domain-type','appDataContainer','--domain-identifier',bundle,'--source',file,'--destination','Library/Application Support/document-config.txt')
launch();print(f'{a.case}: room={room}, server={a.host}:{a.port}, starting in {max(0,(start-time.time()*1000)/1000):.0f}s',flush=True)
metadata={'room':room,'args':vars(a),'host_wall_start_ms':start,'public_keys':{'android':ak,'iphone':ik},'sources':{str(f.relative_to(root)):hashlib.sha256(f.read_bytes()).hexdigest() for directory in ['datafriend/src','datafriend/include','demo-app/src','littlebear/src'] for f in sorted((root/directory).rglob('*')) if f.is_file()}}
metadata['crdt_dependency']=json.loads((root/'third_party/yrs/import.json').read_text())
metadata['binaries']={str(f.relative_to(root)):hashlib.sha256(f.read_bytes()).hexdigest() for f in [root/'demo-app/build/android/drawdo-sokol.apk',root/'demo-app/build/ios/Release-iphoneos/drawdo_sokol.app/drawdo_sokol'] if f.exists()}
(out/(room+'-metadata.json')).write_text(json.dumps(metadata,indent=2)+'\n')
if a.case=='interactive':raise SystemExit(0)
reports={};deadline=time.monotonic()+a.lead+a.duration+60
while len(reports)<2:
 if time.monotonic()>deadline:raise RuntimeError('Timed out collecting both phone reports; unlock phones and accept local-network permission if prompted')
 time.sleep(3)
 if 'android' not in reports:
  r=android('exec-out','run-as',bundle,'cat','files/document-report.json',check=False)
  if r.returncode==0:
   try:
    d=json.loads(r.stdout)
    if d['room']==room:reports['android']=d
   except ValueError:pass
 if 'iphone' not in reports:
  target=work/'iphone-report.json';r=get_ios('document-report.json',target)
  if r.returncode==0:
   d=json.loads(target.read_text())
   if d['room']==room:reports['iphone']=d
for name,d in reports.items():(out/(room+'-'+name+'.json')).write_text(json.dumps(d)+'\n')
def percentiles(xs):
 xs=sorted(xs)
 return {'p50':statistics.median(xs),'p95':xs[min(len(xs)-1,int(len(xs)*.95))],'max':max(xs)} if xs else None
summary={'room':room,'case':a.case,'seed':a.seed,'equal_text':reports['android']['text']==reports['iphone']['text'],'equal_operation_counts':reports['android']['operations']==reports['iphone']['operations'],'devices':{}}
for name,d in reports.items():
 frames=d['frames'];timestamps=[f[5] for f in frames if f[5]>0];start_us=frame_start(frames[0]) if frames else 0
 edits=[e for e in d['edits'] if e[0]>=start_us]
 summary['devices'][name]={'implementation':d.get('implementation','custom-rga'),'state_bytes':d.get('state_bytes'),'ui_facts':d.get('ui_facts'),'edits':len(edits),'error':d['error'],'connected':d['connected'],'settled':d['settled'],
  'edit_ms':percentiles([e[1]/1000 for e in edits]),'edit_components_ms':{label:percentiles([e[i]/1000 for e in edits]) for i,label in enumerate(d.get('edit_component_names',['diff','allocate','commit','merge']),4)} if edits and len(edits[0])>4 else {},'frame_cpu_ms':percentiles([f[2]/1e6 for f in frames]),'frame_work_ms':percentiles([f[4]/1e6 for f in frames]),
  'presentation_coverage':len(timestamps)/len(frames) if frames else 0,'present_fps':(len(timestamps)-1)*1e9/(timestamps[-1]-timestamps[0]) if len(timestamps)>1 else 0,'present_gaps_over_25ms':sum(b-a>25e6 for a,b in zip(timestamps,timestamps[1:])),
  'network':d['network'],'optimistic_sync':d.get('optimistic_sync')}
 if d.get('request_timings'):
  operations={0:'start',1:'reconcile',2:'fetch',3:'apply',4:'subscribe',6:'optimistic_apply',7:'heartbeat',8:'optimistic_subscribe'}
  summary['devices'][name]['requests_by_operation']={}
  for op,label in operations.items():
   samples=[r for r in d['request_timings'] if r[2]==op]
   if samples:summary['devices'][name]['requests_by_operation'][label]={'count':len(samples),'elapsed_ms':None if op==6 else percentiles([r[3]/1000 for r in samples]),'queue_ms':percentiles([r[4]/1000 for r in samples])}
if a.clock_host:
 one_way=one_way_latency(reports)
 (out/(room+'-one-way.json')).write_text(json.dumps(one_way,indent=2)+'\n')
 summary['one_way']={'clock_calibration':one_way['clock_calibration'],'directions':{name:{k:v for k,v in d.items() if k!='samples'} for name,d in one_way['directions'].items()}}
(out/(room+'-summary.json')).write_text(json.dumps(summary,indent=2)+'\n');print(json.dumps(summary,indent=2),flush=True)
if not summary['equal_text'] or not summary['equal_operation_counts'] or any(d['error'] or not d['settled'] for d in reports.values()):raise SystemExit('FAIL: document did not converge cleanly')
if any(d['presentation_coverage']<.99 for d in summary['devices'].values()):raise SystemExit('FAIL: incomplete presentation coverage; raw results retained')
