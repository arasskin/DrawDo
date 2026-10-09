# Native particle client versus Dart

This experiment moves the particle client to C++ and uses Datafriend's C++ API
directly. It does not change the production engine and does not implement Sokol
rendering. It measures whether a native client is worth pursuing independently
of renderer choice.

## Matched experiment

Both implementations run headlessly, sequentially in the same Flutter profile
APK on the Moto G7. They link the **same production `libdatafriend.so`**, compiled
with `-O3 -DNDEBUG`. The benchmark C++ library also uses `-O3 -DNDEBUG`, with
floating-point contraction disabled to match Dart arithmetic. Dart is AOT.

The application thread is pinned to CPU 0 before constructing either store;
the engine's owner and asynchronous flush threads inherit this affinity.
Neither device clock settings nor the production threading model are changed.
Each tick runs immediately after the previous one: no vsync pacing, painting,
layout, animation or network work is included. Consequently these times are not
an FPS comparison with the earlier unpinned, rendered demo.

- Identical starting positions **and drift velocities**, generated once in Dart
  and supplied to C++ as doubles. Same fixed 640×960 world, 96 cells, radius 24.
- Same 96 queries, same x/y projections and region constraints.
- Same neighbor enumeration, local patch movement, wrap rules and fixed timestep.
- Same one-transaction edit batch, async disk flushing, changed-entity cache
  updates, membership handling and float point-buffer maintenance.
- Both keep an insertion-ordered cell cache. C++ uses vectors plus a dense ID
  index; Dart uses its ordinary map. Native direct calls avoid serialized
  requests, serialized change summaries and per-field FFI reads.
- C++ still prepares the existing engine's canonical boxed-value bytes and passes
  attribute names to its public API. This is not a new packed numeric engine API.
- Fresh stores per implementation; 60 warm-up and 240 measured ticks per case.
  Two rounds, shuffled cases, with client order varied across cases and rounds.
- A second APK enables existing coarse Dart boundary timers; control results use
  the uninstrumented build. Profile counters/timers are collected outside the
  tick where possible, and no per-scalar timer is added.

This compares the **whole client implementation**, not just the machine-level
FFI transition. Physics, allocation, object representation, encoding, decoding,
notification handling and cache data structures can all contribute to the gain.
The database, indexes and query engine remain the same.

## Results

Pooled uninstrumented medians and p95s from two rounds, 480 measured ticks per
implementation per row. Times are milliseconds. These are headless tick times,
not rendered frame times or an FPS prediction.

| Particles | Activity | Dart median / p95 | C++ median / p95 | Median time reduction |
|---:|---|---:|---:|---:|
| 100 | local | 0.51 / 0.87 | 0.27 / 0.51 | 47.7% |
| 100 | all | 5.06 / 6.13 | 2.87 / 3.31 | 43.4% |
| 300 | local | 1.18 / 1.55 | 0.63 / 1.05 | 46.0% |
| 300 | all | 13.20 / 16.72 | 8.11 / 8.96 | 38.6% |
| 1,000 | local | 3.76 / 4.70 | 2.66 / 3.03 | 29.1% |
| 1,000 | all | 45.00 / 49.63 | 27.72 / 29.21 | 38.4% |
| 3,000 | local | 13.09 / 15.13 | 9.00 / 10.65 | 31.2% |
| 3,000 | all | 162.87 / 172.27 | 98.63 / 101.45 | 39.4% |

At 1,000–3,000 particles, the native client removes **29–39% of tick time** in
this experiment. The gains include faster client physics and cheaper caches, as
well as bypassing the Dart/native serialization boundary. They must not be
reported as the cost of the FFI call instruction alone.

Median components for 1,000 particles, all moving:

| Component | Dart | C++ |
|---|---:|---:|
| Physics | 5.86 | 2.54 |
| Prepare triples | 2.32 | 0.34 |
| Edit and dispatch | 33.02 | 23.54 |
| Readback/cache refresh | 3.27 | 1.27 |

Independent component medians need not sum to the median tick. Dart edit includes
serialized change-summary decoding in callbacks; native cache refresh directly
uses the native summaries, so not every subphase boundary is identical. The full
tick comparison includes both in either implementation.

The native client still spends most of its tick in `engine::modify`. Sokol could
remove Flutter's rendering path, but it would not remove this storage/query work.
No claim about a Sokol rendering speedup follows from these measurements.

[Per-run metrics](benchmarks/datafriend-particle-native.csv),
[pooled metrics](benchmarks/datafriend-particle-native-aggregate.json),
[raw samples and final coordinates](benchmarks/datafriend-particle-native-runs),
[device conditions and source/APK hashes](benchmarks/datafriend-particle-native-metadata.json).

## Boundary profile

A separate diagnostic APK profiles 1,000 particles, with 480 measured ticks per
activity mode. Values below are **means**, so the edit phases partition its mean
correctly. The main results above remain the uninstrumented controls.

| Phase (ms) | Local activity | All moving |
|---|---:|---:|
| Whole tick | 3.954 | 47.493 |
| Prepare/encode individual triples | 0.189 | 2.377 |
| Encode the edit request | 0.261 | 6.234 |
| Main native call, including C++ request parsing and engine work | 2.365 | 25.080 |
| Transport allocation/copy/free, outside the main call | 0.026 | 0.075 |
| Copy native response to Dart | 0.006 | 0.007 |
| Other edit wrapper work and notification dispatch | 0.318 | 3.956 |
| Readback and cache/point-buffer refresh | 0.288 | 3.661 |

In the all-moving case, the edit averages **35.35 ms**: **25.08 ms** is inside the
main native call and **10.27 ms** outside it. Request serialization alone is
**6.23 ms**; another **3.96 ms** belongs to request finalization, notification
handling, and other wrapper work. That residual includes native change-summary
helper calls, so it is not purely Dart CPU time. The 10.27 ms is about **22% of
the full profiled tick**, before considering preparation and readback. Local
activity spends **0.61 ms** outside the main call during its 2.98 ms edit.

Transport allocation/copy/free plus response copying is only **0.081 ms** for
all-moving. This does not measure the bare FFI transition separately: that is
inside the native-call timer. It does show that bulk memory copying is not the
large boundary cost here. Repeated representations, serialization and callbacks
are more substantial. C++ bypasses those but retains canonical engine values,
query maintenance and database transactions.

Profiling and run variation are visible: the all-moving Dart profiled median is
46.90 ms versus 45.00 ms in the control. Treat the phase profile as diagnostic;
use controls for the speedup. The native implementation is also run in the
profile APK to check for order/device variation. Battery temperatures stayed
29.2–29.4 °C in controls and 29.3–29.4 °C in the diagnostic runs.

This identifies an option short of replacing Flutter: typed numeric edit batches
and bulk result access could remove some of the same conversion work. A Sokol
port would additionally change rendering and the broader UI implementation;
those gains and costs are outside this measurement.

[Profile runs](benchmarks/datafriend-particle-native-profile.csv),
[mean phase breakdown](benchmarks/datafriend-particle-native-profile-means-ms.json),
[raw profile samples](benchmarks/datafriend-particle-native-profile-runs),
[profile conditions and hashes](benchmarks/datafriend-particle-native-profile-metadata.json).

## Validation

Each implementation validates cached values and cell membership against the
underlying database, checks uniqueness/counts and result sizes, then exports its
final state outside timing. The harness requires final coordinates to agree
within 1e-6 and compares **every measured tick's** moved count, crossings, changed
views, read entities, candidate pairs and actual interactions exactly. All 20 paired control/profile cases passed: **4,800 measured tick comparisons**
matched exactly, and maximum final coordinate discrepancy was **zero**. Errors
abort the run and preserve the failure report. Native uses explicit checks even
in its optimized build; validation does not depend on C++ assertions.

## Reproduction

From `src/app`:

```sh
DATAFRIEND_PARTICLE_DEMO=1 DATAFRIEND_PARTICLE_COMPARE=1 \
  flutter build apk --profile --target lib/particle_main.dart \
  --target-platform android-arm64
```

From the repository root:

```sh
python3 src/app/tool/run_particle_comparison.py
```

For the diagnostic build, add `--dart-define=DATAFRIEND_PROFILE=true` to the
Flutter build, then run with `--counts 1000 --output-prefix datafriend-particle-native-profile`. The regular app build does not include the
benchmark native library; the comparison flag selects a small CMake wrapper
which links the production engine unchanged.

[Native client](../src/app/benchmarks/particles.cpp),
[Dart harness](../src/app/lib/particles/comparison.dart),
[runner](../src/app/tool/run_particle_comparison.py),
[actual compile commands](benchmarks/datafriend-particle-native-flags.json).
