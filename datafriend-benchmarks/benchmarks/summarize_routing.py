#!/usr/bin/env python3
"""Summarize paired routing runs without treating setup repeats as new samples."""
import argparse
import csv
import math
from pathlib import Path
import statistics as stats

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('samples', type=Path)
args = parser.parse_args()
with args.samples.open() as f:
    rows = list(csv.DictReader(f))
keys = ['views', 'entities_per_view', 'percent', 'membership', 'routing']
summary = []
for case in sorted({tuple(r[k] for k in keys) for r in rows}):
    result = dict(zip(keys, case))
    process_medians = {}
    for variant in sorted({r['variant'] for r in rows}):
        selected = [r for r in rows if tuple(r[k] for k in keys) == case and r['variant'] == variant]
        if not selected:
            continue
        times = sorted(float(r['edit_us']) for r in selected)
        result[variant + '_samples'] = len(times)
        result[variant + '_median_us'] = stats.median(times)
        result[variant + '_p95_us'] = times[math.ceil(len(times)*.95)-1]
        processes = {r['round']: r for r in selected}
        process_medians[variant] = {rnd: stats.median(float(r['edit_us']) for r in selected if r['round'] == rnd)
                                    for rnd in processes}
        result[variant + '_round_median_min_us'] = min(process_medians[variant].values())
        result[variant + '_round_median_max_us'] = max(process_medians[variant].values())
        if 'subscription_us' in selected[0]:
            result[variant + '_subscription_us'] = stats.median(float(r['subscription_us']) for r in processes.values())
            result[variant + '_subscription_peak_delta_kib'] = stats.median(
                float(r['rss_subscribed_kib'])-float(r['rss_before_kib']) for r in processes.values())
        for field in ['routing_old_us', 'index_update_us', 'routing_new_us', 'routing_us', 'routing_maintenance_us', 'indexes_us', 'queries_us', 'stage_us', 'commit_us']:
            if selected[0].get(field):
                result[variant + '_' + field] = stats.median(float(r[field]) for r in selected)
    if set(process_medians) == {'before', 'after'}:
        rounds = process_medians['before'].keys() & process_medians['after'].keys()
        result['paired_round_ratio'] = stats.median(process_medians['after'][r]/process_medians['before'][r] for r in rounds)
        result['median_change_percent'] = (result['after_median_us']/result['before_median_us']-1)*100
    summary.append(result)
path = args.samples.with_name(args.samples.stem + '-summary.csv')
with path.open('w') as f:
    fields = list(dict.fromkeys(k for r in summary for k in r))
    writer = csv.DictWriter(f, fieldnames=fields)
    writer.writeheader()
    writer.writerows(summary)
print(path)
