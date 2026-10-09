# Datafriend blocking edit benchmark

Current Dart timings and phase attribution: [Dart AOT profile](datafriend-dart-profile.md).

Historical measurements before query sharing. The [sharing benchmark](datafriend-query-sharing.md)
adds ten different queries and reports the current native results.

Measured on the development Mac, 2026-09-27. Raw results:
[native](benchmarks/datafriend-engine.csv),
[Dart AOT durable](benchmarks/datafriend-dart.csv),
[Dart AOT without synchronous flush](benchmarks/datafriend-dart-nosync.csv),
[environment](benchmarks/datafriend-engine-environment.json).

Times include the native owner-thread handoff, persistent working-view edits,
shared LBR3 reconciliation, AELMDB writes, mutable index updates, affected query
reevaluation, publication and reclamation. The native run excludes Dart. The
Dart runs include FFI encoding/decoding and consuming the updated views; the app
case includes rebuilding its immutable document, reusing unchanged collections.
None includes Flutter widget build/layout/rasterization or network traffic.

| Workload | Durability | Median ms | p95 ms | p99 ms | Max ms |
| --- | --- | ---: | ---: | ---: | ---: |
| Native: 1k entities, 1 subscriber | Durable | 3.00 | 3.95 | 5.08 | 5.18 |
| Native: 10k entities, 1 subscriber | Durable | 3.01 | 4.02 | 7.20 | 10.90 |
| Native: 10k entities, 10 subscribers | Durable | 3.93 | 4.81 | 11.38 | 28.12 |
| Native: 10k entities, 100 subscribers | Durable | 6.02 | 7.11 | 8.84 | 10.31 |
| Native: 100k entities, 10 subscribers | Durable | 7.02 | 9.81 | 14.83 | 19.96 |
| Native: 10k entities, 10 subscribers | MDB_NOSYNC | 0.34 | 0.38 | 0.66 | 0.67 |
| Dart: 10k entities, edit + read 10 views | Durable | 4.10 | 5.22 | 5.59 | 11.76 |
| App: 100 tasks + 5k drawing points | Durable | 4.08 | 5.31 | 7.32 | 25.61 |
| Dart: 10k entities, edit + read 10 views | MDB_NOSYNC | 1.18 | 2.29 | 2.98 | 3.50 |
| App: 100 tasks + 5k drawing points | MDB_NOSYNC | 0.62 | 1.10 | 1.44 | 4.16 |

Native runs measure 200 task-completion toggles after 20 warmups. The store has
three facts per entity; each subscriber joins a 1%-selective group to completion
and title, yielding two facts per matching entity. Fan-out is intentionally
separate subscriptions, without sharing identical query evaluations. The Dart
runs use 50 warmups and 200 samples. All timings come from sequential runs, not
paired operations; OS scheduling and storage variability explain why some
larger samples have lower tails than smaller ones. These are prototype host
measurements, not statistically tight device guarantees.

The implementation meets the 16.7 ms budget for most sampled edits, but **does
not establish an every-frame guarantee**. Durable runs recorded 20–28 ms
outliers before Flutter rendering. At 120 Hz the budget is only 8.3 ms. Physical
Android/iOS device traces, disk pressure, larger batches, cold pages and query
fan-out remain necessary. `MDB_NOSYNC` is an explicit diagnostic option; it is
not the app default and can lose recent writes after a crash. Do not change the
durability contract just to obtain better numbers.

An initial JIT app measurement rebuilt unchanged drawing data and exceeded a
frame at p99. The adapter now keeps native snapshots and reuses decoded task,
sticker and stroke collections when roots are unchanged. The final table uses
AOT, so it is not a controlled numerical comparison with that initial JIT run.

Reproduce after `flutter pub get` and the release CMake build:

```sh
python3 datafriend-benchmarks/benchmarks/run_engine.py --dart /path/to/dart
```

The [native phase profile](datafriend-native-profile.md) breaks down the no-fsync
edit: most time is full reevaluation of affected queries, including repeated
identical subscriptions. It is not a standalone tree-mutation measurement.
