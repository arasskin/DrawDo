# Datafriend versus a plain Flutter todo list

The baseline is a conventional, deliberately unoptimized single-screen Flutter
implementation: `StatefulWidget` owns a `List<FlutterTask>`, callbacks mutate task
fields inside `setState`, and `build` filters/sorts the list and counts completed
tasks. `ListView.builder` constructs only mounted/cached rows. It uses keys and
the same reorder lookup as the Datafriend screen. It does not eagerly construct
1,000 widgets or artificially copy every task on each edit.

This is one simple baseline, not an official Flutter benchmark or a claim that
all Flutter apps should be structured this way. Flutter's [setState API](https://api.flutter.dev/flutter/widgets/State/setState.html)
describes synchronous mutation followed by scheduling a rebuild. A more carefully
partitioned Flutter UI could also rebuild individual rows; that is outside this
comparison with the requested naive approach.

The baseline is package-free and in-memory: no database, persistence, query engine
or FFI. Datafriend retains its normal AELMDB transactions, query/index maintenance,
blocking publication, lazy field reads and asynchronous disk flushing. Neither
case performs network sync. This compares the resulting todo interaction, not
equal storage or query capabilities.

## Implementation and validation

- [Plain Flutter screen](../src/todo_demo/lib/pure_flutter_screen.dart) implements
  add, checkbox, rename dialog, delete, filters and order changes.
- [Datafriend screen](../src/todo_demo/lib/todo_screen.dart) retains the order-only
  list, per-mounted-row queries and independent counter.
- [Shared harness](../src/todo_demo/lib/widget_benchmark.dart) checks rendered
  checkbox/title/order and completion counts after every edit, captures actual
  edited-frame timings, and verifies rebuild counts.
- The baseline's final task records are checked in memory; Datafriend's final
  records are checked again after reopening its database. The baseline does not
  claim a persistence check.

The layouts, dimensions, controls, theme and task values match. The subtitle
identifies whether data is in memory or stored. Tests cover the baseline's CRUD,
filtered membership, reordering and whole-screen rebuild behavior, alongside the
existing Datafriend tests for selective updates.

## Method

```sh
python3 src/todo_demo/tool/run_widget_benchmarks.py \
  --flutter /Users/alex/developer/flutter/bin/flutter \
  --samples 20 --uis flutter rows --output-prefix datafriend-pure-flutter
```

One macOS Release executable contains both screens. Three rounds alternate UI
order and task-count order. There are 100 and 1,000 tasks, three workloads (title,
completion, order), five warmups and 20 measured edits per fresh process: 36
processes and 720 measured edits. Each action has a 250 ms post-frame pause.
Completion calls the mounted checkbox callback; title/order call each system's
edit API for the first entity. Order alternates 0/2, moving it past the second
task with ID as the tie-breaker. Source/binary hashes and host load are recorded.
No builds or other tests run alongside measurement.

`data_ready_us` measures the blocking callback/edit through return and publication.
For plain Flutter that means model mutation and `setState`; filtering, sorting
and counts are deferred to `build`. For Datafriend it also includes committing
records, updating queries and invoking callbacks. Comparing this number alone
would omit work from the Flutter baseline.

Edited-frame build and raster times come from Flutter's engine, matched by frame
number. End-of-frame elapsed time additionally includes frame scheduling wait;
the fixed post-frame pause affects input phase. Frame duration is not display
presentation latency. Timings do not establish mobile performance. The coarse
initial-frame field is retained for diagnostics but not compared: setup/seeding
boundaries differ. Engine frame timings overlap the separately recorded framework
interval, so those two intervals must not be added together.

## Results — 2026-09-27

All 36 Release cases passed: 720 measured edits plus 180 warmups. Seven demo
tests pass and analysis is clean. A separate audit verifies source/binary hashes,
all per-action rebuild counts, and the sum of the disjoint timing intervals.
No blockers. Host: Apple M3, macOS 26.5.1 arm64; one-minute load at process start
ranged from 3.30 to 5.09. No builds or other tests ran during measurement.

At 1,000 tasks, milliseconds, median / p95; 60 samples per cell across three
fresh processes:

| Edit | Measurement | Plain Flutter | Datafriend |
| --- | --- | ---: | ---: |
| Completion | Callback/state ready | 0.019 / 0.048 | 0.395 / 2.432 |
| | Edited-frame build | 6.128 / 9.593 | 1.868 / 2.374 |
| | End-of-frame | 14.656 / 19.308 | 14.485 / 16.249 |
| Title | Callback/state ready | 0.025 / 0.064 | 0.452 / 3.961 |
| | Edited-frame build | 6.232 / 9.222 | 1.284 / 1.555 |
| | End-of-frame | 14.619 / 19.015 | 14.041 / 15.399 |
| Order | Callback/state ready | 0.026 / 0.051 | 0.345 / 1.506 |
| | Edited-frame build | 5.803 / 8.812 | 5.385 / 6.970 |
| | End-of-frame | 13.919 / 20.613 | 14.229 / 18.141 |

Plain Flutter performs the mutation cheaply, then recomputes derived state and
rebuilds the screen. Datafriend pays approximately 0.3–0.4 ms more median callback
time, while reducing median frame build work by 4.26 ms for completion (70%) and
4.95 ms for title (79%). Reordering rebuilds both lists and is much closer.

For a combined view without frame scheduling wait, compute
`data_ready_us + framework_frame_us` for each raw sample, then take its median.
These are disjoint wall-clock intervals, not CPU time; they exclude raster work
and must not be confused with end-of-frame latency:

| Edit | Plain Flutter | Datafriend |
| --- | ---: | ---: |
| Completion | 6.139 ms | 2.269 ms |
| Title | 6.349 ms | 1.783 ms |
| Order | 5.877 ms | 5.985 ms |

Thus the selective-query UI saves substantial observed work for title/completion,
while order changes are approximately even. End-of-frame medians remain around
14–15 ms because scheduling wait absorbs much of the difference. End-of-frame p95
is lower for Datafriend in all three 1,000-task cases. Raster medians are similar
for title/order; completion raster is higher for Datafriend (1.601 versus 1.204 ms).

The 100-task runs show the same pattern: median build times were completion
6.366 → 1.831 ms, title 6.198 → 1.344 ms and order 5.206 → 4.594 ms. Plain Flutter
callbacks were 0.024–0.026 ms; Datafriend callbacks were 0.353–0.493 ms. Changing
task count does not change the number of mounted rows in this viewport.

Exact application-builder counts per measured edit, at both task counts:

| UI/edit | Screen | List | Counter | Rows |
| --- | ---: | ---: | ---: | ---: |
| Plain Flutter: any edit | 1 | 1 | 1 | 12 |
| Datafriend: title | 0 | 0 | 0 | 1 |
| Datafriend: completion | 0 | 0 | 1 | 1 |
| Datafriend: order | 0 | 1 | 0 | 12 |

These count application builders, not every internal Flutter widget/layout/paint.
Both UIs initially built twelve mounted/cached rows, never all 1,000 task widgets.

There are outliers: Datafriend's maximum synchronous edit at 1,000 tasks was
4.942 ms; at 100 tasks one title edit took 6.024 ms and one frame build took
7.278 ms. Datafriend's callback tails are substantially higher than pure Flutter's.
These are desktop observations under the recorded load, not a worst-case or
mobile frame-budget guarantee. A Flutter implementation with more selective
state ownership could reduce its frame work without adopting Datafriend; this
comparison establishes the benefit over this simple screen-level baseline.

Artifacts:

- [Pooled results](benchmarks/datafriend-pure-flutter.csv).
- [Raw per-edit runs](benchmarks/datafriend-pure-flutter-runs/).
- [Environment, process logs and source/binary hashes](benchmarks/datafriend-pure-flutter-metadata.json).
