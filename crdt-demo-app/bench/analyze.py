import csv, pathlib, statistics as st, math, json
import sys
p=pathlib.Path(sys.argv[1])
def read(name):
 with open(p/name) as f: return [{k:int(v) for k,v in r.items()} for r in csv.DictReader(l for l in f if not l.startswith('#'))]
def stats(values):
 values=sorted(values)
 return dict(n=len(values),median=st.median(values),p95=values[math.ceil(.95*len(values))-1],min=min(values),max=max(values)) if values else None
surfaces=read('android-surface.csv')
frames=read('ios/frames.csv')
labels={r['frame']:r['tag'] for r in read('ios-tags.csv')}
report={}; details=[]
for platform,filename,lo,hi in [('android','android-events.csv',200001,200060),('ios','ios/events.csv',100001,100060)]:
 ev=read(filename); result={}; samples={}
 for tag in range(lo,hi+1):
  received=[e['ns'] for e in ev if e['tag']==tag and e['stage']==1]
  if not received: print('MISSING RECEIVE',platform,tag); continue
  rx=max(received)
  stages={1:rx}
  for stage in range(2,9):
   times=[e['ns'] for e in ev if e['tag']==tag and e['stage']==stage and e['ns']>=rx]
   if times: stages[stage]=min(times)
  row={'platform':platform,'tag':tag,'receive_ns':rx,**{'stage'+str(k)+'_ms':(v-rx)/1e6 for k,v in stages.items()}}
  for stage,label in [(2,'merge'),(3,'request'),(4,'native_begin'),(5,'native_set'),(6,'draw_or_layout'),(7,'commit')]:
   if stage in stages: samples.setdefault(label,[]).append((stages[stage]-rx)/1e6)
  if platform=='android' and 6 in stages:
   draw=stages[6]
   candidates=[s for s in surfaces if draw<=s['desired_ns']<=draw+16666667 and s['ready_ns']>=draw]
   row['surface_candidates']=len(candidates)
   if len(candidates)==1:
    s=candidates[0]
    row.update(s)
    row['present_ms']=(s['present_ns']-rx)/1e6
    samples.setdefault('os_present',[]).append(row['present_ms'])
   else: print('SURFACE MISMATCH',tag,candidates)
  if platform=='ios':
   candidates=[f for f in frames if labels.get(f['frame'])==tag and f['callback_ns']>=rx]
   if candidates:
    f=min(candidates,key=lambda f:f['callback_ns']); row.update(f)
    row['capture_ms']=(f['callback_ns']-rx)/1e6
    samples.setdefault('capture_callback',[]).append(row['capture_ms'])
   else: print('CAPTURE MISSING',tag)
  details.append(row)
 for label,v in samples.items(): result[label]=stats(v)
 report[platform]=result
print(json.dumps(report,indent=2))
(p/'summary.json').write_text(json.dumps(report,indent=2)+'\n')
(p/'samples.json').write_text(json.dumps(details,indent=2)+'\n')
