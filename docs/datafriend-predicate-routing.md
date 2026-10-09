# Query routing: removing the ~90 ms simulation stall

The original 1,000-view, 100-edit grouped workload now takes **2.59 ms median**
on the Moto G7, versus **92.02 ms** for the preserved old binary in the same
interleaved run. Both binaries validate all live results and notification counts
after every tick. This measures native edit/publication, not a complete UI frame.

## What changed

Previously, an `x` edit collected every query mentioning `x`. A query such as
`owner = 17 AND x = ?x AND y = ?y` was therefore reconsidered even when only
entities belonging to other owners changed. Every affected query then visited
every changed entity: 1,000 queries × 100 entities = 100,000 evaluations.

Entity-local queries with a required constant predicate now register under:

```
changed attribute → predicate attribute → predicate value → query groups
```

For an `x` edit, routing reads the entity's `owner` and finds the matching query
bucket directly. The implementation chooses the first literal-value clause as
this necessary predicate; the evaluator still checks every query condition.
It reads both the complete old and complete new index state, so moves, predicate
removal, whole-entity deletion and simultaneous edits update departing and
arriving views correctly.

Each affected query also receives only its relevant changed entity IDs. This
removes the Cartesian product even for fixed-entity queries. The 100-edit grouped
case evaluates 100 query/entity pairs; replacing 100 entities evaluates 200.
General joins retain conservative invalidation and full evaluation. Queries
without a useful constant predicate can still have broad attribute fanout.

## Matched phone comparison

Moto G7, Android 10, arm64, Release builds, async durability. Two shuffled rounds,
alternating before/after order within each case, fresh database/process each time,
five warmup ticks and 15 timed ticks: **112 processes, 1,680 timed samples**.
The matrix covers 100/1,000 views, 1/4/16 entities per view, 1%/10% changed views,
stable membership and entity replacement. Phone thermal status stayed 0;
battery temperature was 28.0–28.9°C. CPU frequency/scheduling was not pinned.

All rows below use 1,000 views. Times are milliseconds, median (nearest-rank p95).

| Query / entities per view | Changed views | Before | After |
|---|---:|---:|---:|
| Grouped / 4 | 10 | 10.89 (14.82) | **0.82 (1.15)** |
| Grouped / 4 | 100 | 92.02 (114.98) | **2.59 (4.50)** |
| Grouped / 4, replacement | 100 | 204.69 (210.05) | **4.72 (7.11)** |
| Grouped / 16 | 100 | 93.26 (94.89) | **1.97 (3.17)** |
| Grouped / 16, replacement | 100 | 210.70 (229.28) | **5.55 (10.00)** |
| Fixed entity / 1 | 100 | 3.56 (5.02) | **2.19 (2.59)** |
| Fixed entity / 1 | 10 | **0.54 (0.85)** | 0.87 (4.07) |

Grouped recomputation drops from 1,000 groups to exactly 10 or 100; notification
counts remain exactly 10 or 100. Fixed-query recomputation counts are unchanged.
Historical September timings differ from the preserved-binary run; the paired
values above are the comparison for this change.

The small fixed-query regression warranted a separate control: four alternating
rounds with 100 timed ticks per binary per round. The median was **0.629 → 0.688 ms**, with p95 **2.887 → 3.246 ms**
(400 samples per binary). This suggests a small cost for an already narrowly
routed case, alongside substantial scheduling variance. It does not negate the
grouped-query improvement, but this change does not claim a universal speedup.

## Remaining phases

A separate instrumented run uses 30 samples per case. Medians in milliseconds:

| Workload | Total | Stage DB writes | Indexes + routing | Queries + view updates | Commit |
|---|---:|---:|---:|---:|---:|
| Grouped / 4, 10 changed | 0.62 | 0.06 | 0.06 | 0.12 | 0.08 |
| Grouped / 4, 100 changed | 2.04 | 0.31 | 0.51 | 0.66 | 0.16 |
| Grouped / 4, 100 replaced | 3.83 | 1.17 | 1.05 | 0.66 | 0.12 |
| Fixed, 100 changed | 1.49 | 0.21 | 0.29 | 0.57 | 0.11 |

Phase medians do not sum to total median; owner dispatch and other small phases
also contribute. The pathological query work is removed from this workload.
Heavy replacement now spends substantial time staging facts and updating indexes.
Full rendering, CRDT application, network latency and sustained frame tails remain
separate measurements. The widest replacement case still consumes a significant
portion of a 16.7 ms frame at p95.

## Correctness and limits

- All six native suites and 34 Flutter app tests pass.
- New tests cover old/new predicate transitions, absent predicates, extra
  constraints, whole-entity imports/deletes, shared subscriptions, final
  unsubscribe/re-register, and commit-failure recovery.
- Existing randomized tests compare incrementally maintained views against both
  full query execution and an independent reference evaluator.
- The engine suite passes UBSan. Linux ASan was unavailable because Docker was
  stopped; no ASan success is claimed for this change.
- Both phone binaries pass native AELMDB/Littlebear integration before timing.
- A single selected predicate is only a necessary condition. A low-selectivity
  predicate or general join can still require substantial query work.

## Reproduction and evidence

Preserve the old Android build before compiling the change. Then:

```sh
python3 datafriend-benchmarks/benchmarks/run_android_simulation.py \
  --compare-build datafriend/build/android-routing-before \
  --rounds 2 --samples 15 --output-prefix datafriend-android-predicate-routing
```

The runner also accepts repeated `--case VIEWS WIDTH PERCENT MEMBERSHIP ROUTING`
arguments for targeted controls. Android directories include `libdatafriend.so`,
`simulation_benchmark`, and `network_test`; the script supplies the NDK runtime.

- [All samples](benchmarks/datafriend-android-predicate-routing.csv)
- [Summary](benchmarks/datafriend-android-predicate-routing-summary.csv)
- [Device states and binary/source hashes](benchmarks/datafriend-android-predicate-routing-metadata.json)
- [Instrumented phases](benchmarks/datafriend-android-predicate-routing-profile.csv)
- [Small fixed-query control](benchmarks/datafriend-android-predicate-routing-fixed-control.csv)
