"""Shared summaries for rendered Android and iOS task profiles."""
import math, statistics
names=["idle","toggle","rename","filter","reorder","scroll","add_remove","cache_walk"]
def stats(values):
 if not values:return {}
 v=sorted(values)
 return {'p50':statistics.median(v),'p95':v[math.ceil(len(v)*.95)-1],'max':v[-1],'mean':statistics.mean(v)}

def summarize(d):
 rows=d['rows'];actions=[r for r in rows if r['action']]
 selected=actions or rows
 s={'count':d['count'],'case':names[d['case']],'frames':len(rows),'actions':len(actions)}
 for scope,group in [('all',rows),('active',selected)]:
  for field in ['action','prepare','submit','total','cpu','encode','engine','publish','subscribe','unsubscribe','text','raster','upload','geometry_upload','shape_submit','label_upload','label_submit','commit','diagnostics','jni_attach','jni_detach','java_raster','bitmap_copy','pass_begin','shape_draw']:
   for key,value in stats([r[field+'_ns']/1e6 for r in group]).items():s[f'{scope}_{field}_ms_{key}']=value
  for key,value in stats([r['text_misses'] for r in group]).items():s[f'{scope}_text_misses_{key}']=value
 timestamps=[r['present_ns'] for r in rows if r['present_ns']>0]
 s['presentation_coverage']=len(timestamps)/len(rows)
 intervals=[(b-a)/1e6 for a,b in zip(timestamps,timestamps[1:])]
 for key,value in stats(intervals).items():s['present_interval_ms_'+key]=value
 s['present_fps']=1000/statistics.mean(intervals) if intervals else 0
 s['present_over_25ms']=sum(x>25 for x in intervals)
 s['present_intervals']=len(intervals)
 s['callback_over_25ms']=sum(r['interval_ns']>25000000 for r in rows)
 for key,value in stats([(r['present_ns']-r['start_ns'])/1e6 for r in selected if r['present_ns']>0]).items():s['dispatch_to_present_ms_'+key]=value
 s['texture_bytes_max']=max(r['texture_bytes'] for r in rows)
 s['texture_entries_max']=max(r['texture_entries'] for r in rows)
 s['subscriptions']=sum(r['subscriptions'] for r in rows)
 s['unsubscriptions']=sum(r['unsubscriptions'] for r in rows)
 return s

