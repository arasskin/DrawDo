#!/usr/bin/env python3
"""Compare repeated subscriptions and ten additional distinct queries, no fsync.

Build native (Release) and profile (Release + DATAFRIEND_PROFILE=ON) first.
The checked-in *before* files were captured using the same workloads before sharing.
"""
import subprocess
from pathlib import Path

root = Path(__file__).resolve().parents[2]
out = root / 'docs/benchmarks'
rows = ['entities,identical_subscribers,additional_queries,query_evaluations,durable,p50_us,p95_us,p99_us,max_us']
phases, counts = [], []
for extra in [0, 10]:
    result = subprocess.run([str(root / 'datafriend-benchmarks/build/native/engine_benchmark'),
                             '10000', '10', '0', str(extra)], capture_output=True, text=True, check=True)
    rows.append(result.stdout.strip())
    print(result.stdout, end='', flush=True)
    result = subprocess.run([str(root / 'datafriend-benchmarks/build/profile/engine_profile'),
                             '10000', '10', str(extra)], capture_output=True, text=True, check=True)
    lines = result.stdout.splitlines()
    phases.extend(lines if not phases else lines[1:])
    counts.append(result.stderr.strip())
    print(result.stderr, end='', flush=True)
(out / 'datafriend-sharing-after.csv').write_text('\n'.join(rows) + '\n')
(out / 'datafriend-sharing-profile-after.csv').write_text('\n'.join(phases) + '\n')
(out / 'datafriend-sharing-counts-after.txt').write_text('\n'.join(counts) + '\n')
