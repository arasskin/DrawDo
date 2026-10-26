"""Local-producer comparison. Preserve all per-update matches and unmatched IDs."""
import argparse, csv, json, math, pathlib, statistics as st
p=argparse.ArgumentParser(); p.add_argument('directory',type=pathlib.Path); p.add_argument('--flutter',action='store_true'); a=p.parse_args(); root=a.directory

def read(name):
    with (root/name).open() as f: return [{k:int(v) for k,v in r.items()} for r in csv.DictReader(x for x in f if not x.startswith('#'))]
def stats(v):
    v=sorted(v)
    return dict(n=len(v),median=st.median(v),p95=v[math.ceil(.95*len(v))-1],min=v[0],max=v[-1]) if v else None
surface=read('android-surface.csv'); captures=read('ios/frames.csv'); labels={r['frame']:r['tag'] for r in read('ios-tags.csv')}
summary={}; details=[]
for platform,filename in [('android','android-events.csv'),('ios','ios/events.csv')]:
    events=read(filename); samples={}; missing=[]
    clock={e['stage']:e['ns'] for e in events if e['tag']==999997}
    offset=clock[41]-(clock[40]+clock[42])/2 if clock else 0
    def add(name,v): samples.setdefault(name,[]).append(v/1e6)
    for tag in range(300001,300061):
        ev=[e for e in events if e['tag']==tag]
        stages={s:min(e['ns'] for e in ev if e['stage']==s) for s in {e['stage'] for e in ev}}
        if 9 not in stages: missing.append(tag); continue
        origin=stages[9]
        row=dict(platform=platform,tag=tag,stages=stages)
        for stage,name in [(3,'ready_to_request'),(4,'ready_to_handler'),(5,'ready_to_assignment'),(6,'ready_to_draw'),(7,'ready_to_commit')]:
            if stage in stages: add(name,stages[stage]-origin)
        for x,y,name in [(3,4,'request_to_handler'),(4,5,'assignment_block'),(31,32,'editable_replace'),(5,13,'assignment_to_predraw'),(18,6,'draw_block')]:
            if x in stages and y in stages: add(name,stages[y]-stages[x])
        if 6 in stages and a.flutter:
            # FrameTiming is in the engine clock. The native clock sandwich
            # below detects epoch mismatch; mapping uses a nearby, unique build-finish.
            frameids={e['tag'] for e in events if e['stage']==21}
            frame_matches=[]
            for fid in frameids:
                f={e['stage']:e['ns']-offset for e in events if e['tag']==fid}
                if 21 in f and 22 in f and abs(stages[6]-f[22]) < 2000000:
                    frame_matches.append(f)
            row['flutter_frame_candidates']=len(frame_matches)
            if len(frame_matches)==1:
                f=frame_matches[0]
                row['flutter_frame']=f
                for x,y,name in [(21,22,'flutter_build'),(23,24,'flutter_raster')]:
                    if x in f and y in f: add(name,f[y]-f[x])
                if 24 in f: add('ready_to_raster_finish',f[24]-origin)
                add('assignment_to_build_start',f[21]-stages[5])
            else: print('Flutter frame mismatch',platform,tag,len(frame_matches))
        if platform=='android' and 6 in stages:
            draw=stages[6]
            # Native queueBuffer submission is after onDraw. Flutter can set
            # desired presentation separately; use buffer readiness for matching.
            if a.flutter:
                candidates=[s for s in surface if draw<=s['ready_ns']<draw+50000000 and s['present_ns']>=draw]
            else:
                candidates=[s for s in surface if draw<=s['desired_ns']<=draw+16666667 and s['ready_ns']>=draw]
            row['surface_candidates']=len(candidates)
            if len(candidates)==1:
                row['surface']=candidates[0]
                add('ready_to_presentation',candidates[0]['present_ns']-origin)
                add('assignment_to_presentation',candidates[0]['present_ns']-stages[5])
            else: print('Surface mismatch',platform,tag,len(candidates))
            vsync=[e['ns'] for e in events if e['stage']==11 and e['ns']<=stages[4]]
            if vsync: add('handler_phase_since_vsync',stages[4]-max(vsync))
        if platform=='ios':
            matches=[f for f in captures if labels.get(f['frame'])==tag and f['callback_ns']>=origin]
            if matches:
                row['capture']=min(matches,key=lambda f:f['callback_ns']); add('ready_to_capture',row['capture']['callback_ns']-origin)
            else: print('Capture missing',tag)
        details.append(row)
    summary[platform]={k:stats(v) for k,v in samples.items()}
    summary[platform]['missing_ready']=missing
    if a.flutter:
        clock={e['stage']:e['ns'] for e in events if e['tag']==999997}
        summary[platform]['clock_check']=clock
(root/'summary.json').write_text(json.dumps(summary,indent=2)+'\n')
(root/'samples.json').write_text(json.dumps(details,indent=2)+'\n')
print(json.dumps(summary,indent=2))
