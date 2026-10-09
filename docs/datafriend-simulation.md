# Many small simulation views: Moto G7

**2026-10-07 update:** the routing issue described below has been addressed.
A matched before/after phone run measured **92.02 → 2.59 ms** for the 100-edit
case. See [predicate routing and current results](datafriend-predicate-routing.md).
The remaining sections preserve the original measurements and diagnosis.


Flat unordered views remain the production choice. This experiment measures the
actual storage/query/publication pipeline, rather than comparing containers.
The first bottleneck is query invalidation fan-out and the changed-entity loop.

## Workload and method

Moto G7, Android 10, arm64, NDK 28.2, optimized `-O3 -DNDEBUG` C++ engine.
100 or 1,000 distinct views contain 1, 4 or 16 entities each. Every entity stores
`owner`, `x` and `y`; each view projects `x` and `y`. The database therefore holds
up to 16,000 entities / 48,000 facts, with two projected facts per entity.

A tick is one transaction. It edits the first entity in either 1% or 10% of the
views, not every entity in those views. Stable ticks change `x`. Churn ticks
remove that entity's three facts and add a replacement entity in the same group,
keeping each view's size constant. The changed subset is stable across ticks.
There are two query shapes:

- Fixed: two clauses name the entity ID directly; one entity per view.
- Grouped: `owner == groupID`, with a variable entity and projected `x`, `y`.
  Group IDs differ across views, so these queries cannot share one result.

Two shuffled rounds, five warmup ticks and 15 measured ticks per process:
**56 processes, 840 samples**. Results below pool the 30 samples per case.
Every tick validates notification count, all view sizes, and the current `x`
value in every view. Native synchronization integration tests also pass on the
phone before timing starts. Each process uses a fresh database and async flushing.
Preparation, checks and final flush are outside the timing interval. Background
flushes may overlap ticks, as they do in the intended configuration.

The timer wraps `engine.modify`: caller-to-owner dispatch, transaction staging,
indexes, query execution, live-view maintenance and changed-subscription list.
It excludes Dart/FFI, widget builds, layout, rasterization, sockets and server work.
No elapsed ADB or host orchestration time is included. CPU scheduling/scaling is
stock; this is an exploratory workload matrix, not a controlled frequency study.
All captured thermal statuses were 0, battery temperature 34.5–34.6°C.

## Selected results

Milliseconds per tick; medians, with p95 in parentheses.

| Views | Entities/view | Changed views | Query | Membership | Time |
|---:|---:|---:|---|---|---:|
| 1,000 | 1 | 10 | Fixed entity | Stable | **1.87 (2.39)** |
| 1,000 | 1 | 100 | Fixed entity | Stable | **10.96 (13.39)** |
| 1,000 | 4 | 10 | Grouped | Stable | **14.44 (17.66)** |
| 1,000 | 4 | 100 | Grouped | Stable | **90.97 (91.29)** |
| 1,000 | 4 | 10 | Grouped | Churn | **22.24 (22.50)** |
| 1,000 | 4 | 100 | Grouped | Churn | **204.33 (205.08)** |
| 1,000 | 16 | 100 | Grouped | Stable | **92.32 (95.07)** |
| 1,000 | 16 | 100 | Grouped | Churn | **206.66 (207.08)** |

Increasing each grouped view from four to sixteen entities barely changes these
heavy tick times. Replacing the flat result container would not address the
large amount of unnecessary query work.

Fixed-entity queries reconsider exactly the 10 or 100 affected groups and notify
those same groups. Grouped queries reconsider **all 1,000 groups**, then notify
only the 10 or 100 whose results actually changed. Notifications are correct;
the cost occurs before notification.

## Phase check

A separate instrumented build ran four representative cases, 15 samples each.
These numbers include profiling overhead and are not substitutes for the main
unmodified timings. Medians in milliseconds:

| 1,000-view case | Total | DB staging | Index updates + subscriber collection | Query evaluation + view maintenance + notification collection | Commit |
|---|---:|---:|---:|---:|---:|
| Fixed, 100 changed | 13.03 | 1.05 | 0.58 | **9.94** | 0.38 |
| Grouped ×4, 10 changed | 13.50 | 0.07 | 1.17 | **11.47** | 0.12 |
| Grouped ×4, 100 changed | 92.67 | 0.25 | 7.35 | **83.91** | 0.15 |
| Grouped ×4, 100 replaced | 210.27 | 1.15 | 45.36 | **162.61** | 0.14 |

Phase medians need not add to total median. Total also includes owner dispatch,
input normalization and other small phases. The query phase does not separately
isolate result-map maintenance; these results do not claim to measure that alone.

Two concrete sources of wasted work are visible in `engine::impl::commit`:

1. Variable-entity clauses register at attribute scope. Editing `x` collects
   every grouped query that mentions `x`, even if its `owner` constraint excludes
   that entity. Repeated facts also repeatedly insert the same subscribers into
   the affected set; churn amplifies this cost.
2. Each affected entity-local group iterates the entire transaction's
   `entities_changed` set. Fixed queries quickly reject unrelated IDs, but still
   perform that loop. With 100 fixed views affected, this means 10,000 visits
   for 100 actual matching entity updates. With 1,000 grouped views and 100
   changed entities, it means 100,000 evaluations; replacements double the
   changed-ID set to 200.

The next optimization to investigate is routing the relevant changed entities
**to each query**, using fixed IDs and safe constant-value/group constraints.
Predicates must account for old and new values so entering/leaving a group cannot
be missed. General joins retain conservative invalidation. This is an identified
follow-up, not implemented as part of the network integration.

These grouped workloads currently exceed a 16.7 ms frame budget on this phone;
even the 10-change case leaves little/no time for Flutter. The fixed-ID case is
much better, but 100 edits per tick still needs improvement for a simulation UI.

## Reproduction and raw evidence

Configure `datafriend-benchmarks/build/android-network` with the Android NDK toolchain,
`ANDROID_ABI=arm64-v8a`, `ANDROID_PLATFORM=android-28`, `ANDROID_STL=c++_shared`,
`CMAKE_BUILD_TYPE=Release`, `DATAFRIEND_TESTS=OFF` and
`DATAFRIEND_PORTABLE_BENCHMARKS=ON`, then build. API 28 is needed by Littlebear's
server allocator in the portable integration test; the Datafriend library itself
also compiled for API 24.

```sh
python3 datafriend-benchmarks/benchmarks/run_android_simulation.py
# Separate diagnostic build: also set DATAFRIEND_PROFILE=ON.
python3 datafriend-benchmarks/benchmarks/run_android_simulation.py \
  --build datafriend-benchmarks/build/android-simulation-profile --quick \
  --rounds 1 --samples 15 --output-prefix datafriend-android-simulation-profile
```

Raw samples, summaries, device states and binary hashes:

- [Main samples](benchmarks/datafriend-android-simulation.csv)
- [Summary](benchmarks/datafriend-android-simulation-summary.csv)
- [Main metadata](benchmarks/datafriend-android-simulation-metadata.json)
- [Instrumented samples](benchmarks/datafriend-android-simulation-profile.csv)
- [Instrumented metadata](benchmarks/datafriend-android-simulation-profile-metadata.json)
