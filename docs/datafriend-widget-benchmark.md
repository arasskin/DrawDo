# Inline-query UI benchmark

These are historical measurements of the first inline-query screen. The current
[per-row comparison](datafriend-row-queries.md) separates list membership/order
from each mounted row's title/completion query.

This benchmark compares the previous controller-backed todo UI with the new
`DatafriendBuilder` screen in the same macOS Release executable. Both use
Datafriend with asynchronous disk flushing. No InstantDB cases are run.

```sh
python3 src/todo_demo/tool/run_widget_benchmarks.py \
  --flutter /Users/alex/developer/flutter/bin/flutter \
  --uis legacy bulk --workloads toggle --output-prefix datafriend-inline-widgets-reproduction
```

The runner alternates legacy/inline order over three rounds at 100 and 1,000
tasks. Each case uses a fresh process and database, five warmup toggles and 30
measured toggles. It waits 250 ms after each frame. The inline case has only the
screen's own two subscriptions; the legacy case retains its original
AppViews/controller subscriptions. No benchmark query is added during timing.

The driver locates the first mounted checkbox outside the timed region and
invokes its real `onChanged` callback. These are programmatic actions, not
hardware taps. It measures publication, callback completion, state readiness,
frame scheduling wait and the end of Flutter's framework frame. Build/raster
durations are matched to that edited frame's engine frame number. Each raw
sample's four phases must sum to its end-of-frame latency.

Every action verifies that the rendered checkbox has the requested value.
After measurement, the driver unmounts the UI, checks task/completion counts,
closes the database and verifies exact record contents on reopening. Seeding,
verification and close/reopen are outside timers. Both screens use their actual
layouts and query shapes, so this is an end-to-end refactor comparison, not an
isolated wrapper microbenchmark. The regular display cadence affects frame wait;
the timed end-of-frame does not measure physical screen presentation.

Raw samples, pooled percentile summaries, process output, host load and
source/binary hashes are retained in
[`datafriend-inline-widgets-runs`](benchmarks/datafriend-inline-widgets-runs/),
[`datafriend-inline-widgets.csv`](benchmarks/datafriend-inline-widgets.csv), and
[`metadata`](benchmarks/datafriend-inline-widgets-metadata.json).

## Results — 2026-09-27

All 12 processes passed rendered-toggle, count, exact-reopen and phase-sum
checks: 90 measured edits per UI per task count, 360 measured edits overall.
Apple M3/macOS 26.5.1 arm64, Flutter macOS Release. One-minute host load at process
start ranged from 4.18 to 7.21. No builds or tests ran alongside measurements.

At 1,000 tasks, milliseconds, pooled median / p95:

| Measurement | Previous controller UI | Inline-query UI |
| --- | ---: | ---: |
| All query notifications published | 0.357 / 0.757 | 0.322 / 0.880 |
| Callback flow complete, state ready | 0.432 / 0.875 | 0.324 / 0.883 |
| Wait for next frame start | 6.797 / 11.256 | 8.094 / 11.347 |
| Actual edited-frame build | 6.940 / 8.410 | 5.860 / 7.783 |
| Actual edited-frame raster | 1.222 / 3.558 | 1.184 / 1.799 |
| Action to end-of-frame await resumption | 14.059 / 19.219 | 14.446 / 17.866 |

Inline-query UI scaling, milliseconds, pooled median / p95:

| Measurement | 100 tasks | 1,000 tasks |
| --- | ---: | ---: |
| State ready | 0.314 / 0.773 | 0.324 / 0.883 |
| Edited-frame build | 5.810 / 7.714 | 5.860 / 7.783 |
| End-of-frame await resumption | 14.569 / 19.039 | 14.446 / 17.866 |

No correctness failure or large scaling regression appeared. At 1,000 tasks the
new flow lowers median state-ready latency by about 25% and edited-frame build
time by about 16%; state-ready p95 is essentially unchanged. The new publication
p95 is somewhat higher, so this is not an across-the-board latency improvement.
The maximum inline state-ready sample was 1.967 ms (legacy 1.767 ms).

End-of-frame medians remain near 14 ms; the new case's longer frame wait offsets
its shorter build. The 250 ms pause after each frame influences input phase.
Engine build/raster intervals overlap framework timing and must not be added to
the end-of-frame total. Per-phase percentiles also do not sum to total percentiles.
These desktop measurements do not establish physical-phone performance or a
no-jank guarantee. The prior store benchmarks measure a different, less UI-heavy
workload and should not be compared directly with this callback timing.
