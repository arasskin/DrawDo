import subprocess as s, time, pathlib, json, argparse
parser = argparse.ArgumentParser(description="Collect paired native text latency after installing and pairing --timing builds")
parser.add_argument("--output", type=pathlib.Path, required=True)
parser.add_argument("--driver", type=pathlib.Path, required=True, help="Directory containing the built CRDTTimingTest project and build/ products")
args = parser.parse_args()
out=args.output; out.mkdir(parents=True, exist_ok=False)
adb=['/Users/alex/Library/Android/sdk/platform-tools/adb','-s','ZY227364TP']
def run(cmd,**kw): return s.run(cmd,check=True,**kw)
role = out/'role.txt'
role.write_text('1\n')
run(['xcrun','devicectl','--timeout','30','device','copy','to','--device','Phone','--domain-type','appDataContainer','--domain-identifier','dev.drawdo.crdt','--source',str(role),'--destination','Library/Application Support/timing/run.txt'])
run(adb+['shell','run-as','dev.drawdo.crdt','sh','-c',"'cat > files/timing/run.txt'"],input=b'0\n')
log=out/'ios-driver.log'
f=log.open('w')
p=s.Popen(['xcodebuild','test-without-building','-project',str(args.driver/'CRDTTimingTest.xcodeproj'),'-scheme','CRDTTimingTest','-destination','id=00008110-000A7D681490401E','-derivedDataPath',str(args.driver/'build'),'DEVELOPMENT_TEAM=466U33S75F'],stdout=f,stderr=s.STDOUT)
deadline=time.monotonic()+120
while 'CRDT_TIMING_STARTED' not in log.read_text():
 if p.poll() is not None or time.monotonic()>deadline: raise RuntimeError('iOS test failed to start; inspect '+str(log))
 time.sleep(.5)
run(adb+['shell','am','force-stop','dev.drawdo.crdt'])
run(adb+['shell','am','start','-W','-n','dev.drawdo.crdt/.MainActivity'])
time.sleep(2)
start=time.monotonic(); frames=set(); layers=[]
while time.monotonic()-start<110:
 names=run(adb+['shell','dumpsys','SurfaceFlinger','--list'],capture_output=True,text=True).stdout.splitlines()
 candidates=[n for n in names if n.startswith('PopupWindow:')]
 if len(candidates)!=1: raise RuntimeError(str(candidates))
 layer=candidates[0]
 if layer not in layers: layers.append(layer)
 data=run(adb+['shell','dumpsys','SurfaceFlinger','--latency',layer],capture_output=True,text=True).stdout
 for line in data.splitlines()[1:]:
  cols=line.split()
  if len(cols)==3:
   row=tuple(map(int,cols))
   if all(0<v<2**63-1 for v in row): frames.add(row)
 print(f'{time.monotonic()-start:.1f}s: {len(frames)} compositor frames',flush=True)
 time.sleep(5)
(out/'android-surface.csv').write_text('desired_ns,present_ns,ready_ns\n'+''.join(','.join(map(str,r))+'\n' for r in sorted(frames)))
(out/'layers.json').write_text(json.dumps(layers))
with (out/'android-events.csv').open('wb') as dst:
 run(adb+['exec-out','run-as','dev.drawdo.crdt','cat','files/timing/events.csv'],stdout=dst)
run(['xcrun','devicectl','--timeout','30','device','copy','from','--device','Phone','--domain-type','appDataContainer','--domain-identifier','dev.drawdo.crdt','--source','Library/Application Support/timing','--destination',str(out/'ios')])
print('Saved',out,flush=True)
