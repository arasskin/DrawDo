# Dart boundary and app adapter profile

Historical whole-document path. Production now uses
[direct view edits and Flutter publication](datafriend-widget-publication.md).

Measured 2026-09-27 on macOS arm64 using Dart AOT and the packed-key, boxed-value
native engine. Queries remain single-threaded. **Repeated materialization and
app-wide diffing dominate; invoking the edit through FFI is inexpensive.**

## What the totals measure

These are complete action times with `durable:false` (AELMDB `MDB_NOSYNC`). They
include native owner-thread waits, transactions and publication where applicable,
plus Dart encoding, reading and adaptation. They exclude synchronous disk flush,
networking and Flutter build/layout/rasterization. The native library is an
uninstrumented Release build; Dart profiling is separately compiled on or off.

Each workload runs in a fresh process: 100 warmup actions, 1,000 measured actions,
three rounds, rotated workloads and alternating control/profile order. Times below
are **medians of three uninstrumented run medians**, in microseconds.

| Workload | Median | Median run p99 |
| --- | ---: | ---: |
| 10 identical subscriptions: `add` + read every live view | 609 µs | 831 µs |
| Same subscriptions: `edit` + read every live view | 563 µs | 823 µs |
| 20 subscriptions / 11 queries: `add` + read every live view | 1,301 µs | 1,929 µs |
| Same subscriptions: `edit` + read every live view | 1,259 µs | 1,811 µs |
| Same subscriptions: **edit only** | **193 µs** | 218 µs |
| Same subscriptions: edit + reuse unchanged snapshots | 702 µs | 919 µs |
| **Actual app adapter: toggle one of 100 tasks** | **412 µs** | 474 µs |
| App adapter stress case: toggle one of 1,000 tasks | 3,932 µs | 5,414 µs |
| App adapter: unchanged 100-task document | 67 µs | 103 µs |

The synthetic view workloads use 10,000 entities, 100 groups and two projected
facts per matching entity. Ten handles share group 1; mixed workloads add groups
2–11. All eleven queries depend on the edited `done` attribute. The app workload
instead uses its real five-attribute task query, three stickers and a retained
5,000-point drawing. **The 1.3 ms synthetic read-all result is not the app's
412 µs result.** They perform different work.

The snapshot-reuse case caches each owner's decoded list, compares retained
native roots and decodes only changed roots. It does not deduplicate equal roots
across owners. This is an experiment in the benchmark, not a new live-view cache.
The actual app adapter already skips unchanged sticker/stroke roots.

## Where the app's time goes

For one toggle among 100 tasks, the profiled total is 418 µs, versus 412 µs in
the control. Exclusive phase medians below exclude any nested phases. Independent
medians need not add to the median total.

| Phase | 100 tasks | 1,000 tasks |
| --- | ---: | ---: |
| Encode candidate rows/triples and compare against baseline | **97 µs** | **1,006 µs** |
| Decode values, sort rows and construct app model objects | **74 µs** | **811 µs** |
| Decode returned bytes into Dart triples | **68 µs** | **683 µs** |
| Native blocking edit, including queries/publication | 55 µs | 405 µs |
| Rebuild app-ID → entity-ID lookup | 33 µs | 288 µs |
| Native snapshot serialization | 32 µs | 288 µs |
| Group returned triples into Dart entity/attribute maps | 22 µs | 235 µs |
| Obtain the three snapshot roots, native calls | 14 µs | 16 µs |
| Scan baseline for removed rows | 14 µs | 110 µs |
| Copy response buffers into Dart | **0.7 µs** | **5.7 µs** |
| Encode the actual edit request | **0.4 µs** | **0.5 µs** |

One boolean change causes:

- **100 candidate rows / 500 candidate triples** to be encoded and compared.
- One edit request; only the changed attribute is sent to native storage.
- Three snapshot acquisitions, one changed root, one bulk snapshot export.
- **500 triples / 13,884 response bytes** decoded for the refreshed task view.
- All 100 task models to be reconstructed; sticker and stroke objects are reused.

At 1,000 tasks this becomes 5,000 candidate triples and 5,000 returned triples
(140,784 bytes). This explains the near-linear scaling. Native edit time also
increases because the app's query returns every task, but it is not the largest
component. Approximately 76% of the 100-task profiled mean and 82% of the
1,000-task mean lie outside the timed native operation bodies. The residual
includes Dart orchestration and small allocation/release FFI calls; it is not
an isolated Dart CPU or garbage-collection measurement.

The unchanged-document case still rebuilds the identity lookup (~37 µs), scans
for removals (~12 µs) and fetches roots, despite encoding and decoding no triples.

## Why reading live views costs so much

For the 20-handle `edit` + live-read case, the profiled median is 1,286 µs:

| Phase, summed across owners per edit | Median |
| --- | ---: |
| Decode returned triples in Dart | **584 µs** |
| Native live reads: owner-thread waits and serialization | **369 µs** |
| Native edit | 202 µs |
| Native snapshot acquisition for live-set lengths | 72 µs |
| Remaining wrappers, copies and list work | roughly 60 µs |

Counters show **20 live reads, 20 snapshot acquisitions, 4,000 decoded triples
and 104,080 response bytes**. `SetBase.toList` asks for the live set's length,
which acquires a snapshot, and then its iterator reads the full view separately.
Native reads include C++ serialization and waiting, not just FFI transition cost.

Two additional effects matter:

- `DatafriendView.add` implements its return value with `contains`, which reads
  and decodes the entire existing view. It adds one 200-triple read here. Using
  the existing `edit(add: ...)` operation when no membership result is needed
  saves about **43–46 µs** in these controls.
- Native query sharing does not share decoded Dart lists. Ten subscribers to the
  same 200-fact root each serialize/decode it. Snapshot reuse skips the ten
  unchanged different-query roots, but the ten identical changed roots are still
  decoded independently. It reduces 4,000 decoded triples to 2,000, taking the
  synthetic read-all action from 1,259 to 702 µs.

The edit-only profile spends ~191 µs inside the native call and about 1–2 µs
around it. Therefore the extra millisecond in read-all execution does not come
from encoding the edit or the basic Dart-to-native invocation.

## Design correction after reviewing original iCare

The whole-document adapter is not the intended callback flow. Original iCare
callbacks directly transact the requested attribute changes, then invoke a
separate render step. The agreed new flow edits a subscribed view directly and
blocks until Datafriend has reconciled, updated storage/indexes, recomputed
queries and published all affected roots. Callback execution resumes at that
point. Widget invalidation and reading the new state are separate concerns.

Consequently, these app results characterize the transitional adapter. Replacing
that mutation path takes precedence over optimizing its document-wide diff or
reconstruction. The edit-only benchmark already exercises the intended blocking
boundary. Full result decoding is a read/render concern, not mandatory work on
every mutation's return path.

## Optimization targets identified in the measured adapter

1. Avoid recreating every task's encoded triples to discover a one-field change.
   Preserve explicit edits or skip unchanged task objects while still accounting
   for additions, deletions and order changes.
2. Refresh changed facts/entities in the Dart cache instead of exporting and
   reconstructing the complete changed query result. The native retained roots
   and reconciliation machinery are suitable inputs; the refresh API still needs
   design and measurement.
3. Share decoded results across handles with identical roots. This chiefly helps
   the synthetic multi-owner case; the app's three schema views are distinct.
4. Avoid the live-set length/read double handoff and full-view `contains` reads.
   A native membership lookup or mutation result could preserve the set interface.

Raw buffer copying is too small to justify prioritizing shared byte buffers over
these changes. Native query parallelism remains deferred. No functional cache,
query, codec or app-adapter optimization was enabled in this profiling change.

## Instrumentation, validation and reproduction

Compile-time `DATAFRIEND_PROFILE` guards coarse timers and counters. Normal AOT
builds eliminate those call sites. Nested timers report inclusive and exclusive
time; every measured action asserts that exclusive ticks sum exactly to its
instrumented root. No timer runs per fact or scalar value. Profile bookkeeping
and garbage collection are not independently subtracted from the phases.

Profiling changes median totals by 0–2.5% for edit/read workloads, 1.5–1.8% for
the two app toggle workloads and 5.9% for the much smaller no-op workload. These
controls bound the diagnostic perturbation in this experiment, not on every
platform. Host scheduling causes tail noise: the worst uninstrumented 100-task
app sample was 4.39 ms and the 1,000-task sample was 14.0 ms. Physical-device and
complete Flutter frame profiling remain necessary.

All **72 processes** passed result checks, including 18 short runs using uneven
group sizes and odd final toggle parity. Checks cover every returned entity's
values, cached results, retained snapshots and reuse of unchanged app objects.
All 15 Flutter tests pass with profiling both enabled and disabled; Flutter
analysis is clean.

```sh
cmake --build datafriend-benchmarks/build/native -j 4
python3 datafriend-benchmarks/benchmarks/run_dart_phases.py --dart /path/to/dart
```

The runner compiles separate AOT control/profile executables and uses the normal
native library. `--skip-build` reuses those binaries. Profiling can also be tested
with `flutter test --dart-define=DATAFRIEND_PROFILE=true`.

[Raw totals, phase timings and counters](benchmarks/datafriend-dart-phases.csv) ·
[Run commands, environment and source/binary hashes](benchmarks/datafriend-dart-phases-metadata.json).
