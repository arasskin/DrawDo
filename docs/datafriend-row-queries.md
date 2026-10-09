# Per-row task queries

The subsequent [fixed-entity routing comparison](datafriend-fixed-routing.md)
measures the same UI before and after narrowing native subscription dependencies.
The results below describe the original per-row refactor, before that optimization.

The todo list now projects only each task's `order` attribute. Its `kind` clause
selects tasks without projecting kind; Active/Completed filters add a `done`
condition without returning it. Each mounted `TaskRow` subscribes to its entity's
`title` and `done` attributes. The completion counter keeps its independent query.

`ValueKey(entityId)` and `findChildIndexCallback` preserve row elements and their
subscriptions when the list changes order. A row whose entity disappears returns
an empty widget until the parent removes it. Subscriptions follow mounted rows,
including Flutter's cached rows, rather than every entity in the database.

## Rebuild verification

Optional `TodoBuildProbe` callbacks count application screen, list, counter and
row builders. These are application rebuild boundaries, not every internal
Flutter widget, layout or paint operation. Instrumentation is absent from normal
interactive use. Regression tests verify:

- A visible title edit rebuilds only that row.
- In All mode, completion rebuilds one row and the counter, not the list.
- Two title edits in one transaction rebuild the two affected rows.
- An unmounted row's title edit causes no application widget rebuild; scrolling
  it into view later reads the updated title.
- Order changes rebuild the list and retain row subscription state by entity ID.
  The parent may rebuild visible rows while repositioning them.
- Insert/delete rebuild the list and counter; deletion removes the row.
- In Active/Completed mode, a completion edit can change list membership, so the
  list also rebuilds. Omitting `done` from the projection does not omit its predicate.

## Benchmark methodology

```sh
python3 src/todo_demo/tool/run_widget_benchmarks.py \
  --flutter /Users/alex/developer/flutter/bin/flutter --samples 20
```

The previous full-list query is retained in `bulk_query_screen.dart` as a matched
baseline. Both cases use identical visual layouts, error helpers, async disk
flushing and completion-counter queries. The new case adds per-mounted-row
subscriptions; the baseline returns all task fields from one list query.

Three rounds alternate baseline/new UI at 100 and 1,000 tasks, with fresh
processes/databases per workload: completion toggle, title change, and order
change. Each case has five warmups and 20 measured edits, separated by 250 ms
after frame completion. Toggle invokes the mounted checkbox's actual callback;
title and order use direct triple edits. Order alternates the first task between
orders 0 and 2, moving it past the second task while keeping it on screen.

Each action checks rendered values and completion counts. The new case asserts
exact row/list/counter rebuild scope per action; all cases record builder counts.
Build/raster timings match the actual edited engine frame. Each raw sample's
state-ready, frame-wait, framework-frame and post-frame-resume intervals must sum
to total latency. Validation subscriptions are created only after measurement;
the final task/completion counts and exact reopened records are checked.

The runner records source/binary hashes, host load and process output. The
benchmark drives callbacks, not physical input; end-of-frame timing does not
measure display presentation. Engine durations overlap framework intervals.
Initial mounting has a separate coarse frame timer and is not part of edit timing.

## Results — 2026-09-27

All 36 final cases passed: 720 measured edits, plus 180 warmups. All measured
application-builder count tuples matched the expected scope. Rendered checkbox,
title, order, completion counts and exact reopened records passed. All six demo
tests passed and analysis was clean; the macOS Release bundle was rebuilt.

Apple M3, macOS 26.5.1 arm64, asynchronous flushing. One-minute host load at case
start ranged from 3.16 to 4.67. No builds or tests ran during measurement. Each
UI/workload/task-count cell pools 60 measured samples across three rounds.

At 1,000 tasks, milliseconds, median / p95:

| Change | Measurement | Full-list query | Per-row queries |
| --- | --- | ---: | ---: |
| Completion | State ready | 0.314 / 0.941 | 0.455 / 0.759 |
| | Edited-frame build | 5.413 / 6.508 | 1.929 / 2.170 |
| | End-of-frame | 14.216 / 18.169 | 14.187 / 15.211 |
| Title | State ready | 0.326 / 1.200 | 0.450 / 1.297 |
| | Edited-frame build | 4.449 / 5.687 | 1.305 / 1.495 |
| | End-of-frame | 14.040 / 16.960 | 13.904 / 15.198 |
| Order | State ready | 0.365 / 0.887 | 0.318 / 0.573 |
| | Edited-frame build | 6.638 / 8.242 | 4.892 / 5.876 |
| | End-of-frame | 13.813 / 19.516 | 13.988 / 16.468 |

At 100 tasks, median edited-frame builds were 5.045 → 1.724 ms for completion,
4.475 → 1.319 ms for title and 5.687 → 4.338 ms for order. Per-row median
state-ready times were 0.446, 0.470 and 0.347 ms respectively, similar to 1,000 tasks.

Exact measured rebuild counts per edit at both task counts:

| Change | Full-list list / row / counter | Per-row list / row / counter |
| --- | ---: | ---: |
| Title | 1 / 12 / 0 | 0 / 1 / 0 |
| Completion (All filter) | 1 / 12 / 1 | 0 / 1 / 1 |
| Order | 1 / 12 / 0 | 1 / 12 / 0 |

Neither case rebuilt the screen root. The viewport built 12 rows including the
scroll cache in every process. Thus the new case maintained 14 screen-owned
subscriptions (list, counter and 12 rows), versus two in the baseline; it did not
subscribe separately to all 1,000 entities. Reorder still rebuilds visible row
builders through their parent, while the separate widget test verifies row
subscription state survives that reorder.

The measured tradeoff is about 0.12–0.14 ms more median synchronous work for
title/completion, saving 3.14–3.48 ms of median Flutter build time. Order also
improves despite rebuilding the visible rows. State-ready tail latency does not
universally improve: title p95 rose from 1.200 to 1.297 ms and its maximum sample
was 3.147 ms (baseline 2.101 ms).

End-of-frame medians remain near 14 ms because frame wait grows as build work
shrinks; the fixed post-frame pause affects input phase. End-of-frame p95 improves
for all three workloads at 1,000 tasks. Raster medians are similar (roughly
1.2–1.5 ms). The coarse initial-frame timer showed no obvious startup regression:
1,000-task medians across nine cases per UI were 23.106 ms baseline and 22.620 ms
per-row, but this timer includes cold mounting and is not an isolated query metric.

These results establish rebuild isolation and lower Flutter build work on this
desktop workload; they are not physical-phone measurements or a no-jank guarantee.

Artifacts:

- [Pooled results](benchmarks/datafriend-row-queries.csv)
- [Raw runs](benchmarks/datafriend-row-queries-runs/)
- [Metadata, source/binary hashes and process logs](benchmarks/datafriend-row-queries-metadata.json)

The separately suffixed smoke artifacts are validation runs, not these results.
