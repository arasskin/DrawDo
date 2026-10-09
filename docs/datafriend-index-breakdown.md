# Index maintenance and query routing

Instrumentation only; database staging and indexing algorithms are unchanged.
All timers compile out of ordinary builds. `DATAFRIEND_PROFILE` enables the
three main phases; `DATAFRIEND_PROFILE_INDEXES` additionally brackets individual
map operations and is diagnostic only.

## Workload and reliable phase timings

Moto G7, Android 10, arm64 Release, asynchronous durability. There are 1,000
grouped views with four entities each. Every entity has `owner`, `x` and `y`;
queries filter by owner and project x/y. Each tick changes 100 views:

- Stable: change x on one existing entity per affected view (100 facts).
- Replacement: remove one entity and add a new entity with the same owner per
  affected view (300 deleted facts and 300 inserted facts).

Four alternating rounds, 50 timed ticks per case/build per round, five warmups
per process: 200 samples per cell below. Medians in milliseconds, from the
coarse-only build in the counter comparison:

| Phase | 100 existing entities edited | 100 entities replaced |
|---|---:|---:|
| Route using old index state | 0.624 | 0.925 |
| Maintain query indexes | 0.436 | 1.234 |
| Route using new index state | 0.272 | 0.603 |
| Entire indexes/routing region | 1.356 | 2.812 |

Medians need not add up. The outer region also includes affected-map setup and
the nested timers. Routing is approximately half the replacement cost.

Old-state routing finds queries that may lose an entity, plus ordinary attribute
and fixed-entity subscribers. New-state routing finds queries that may gain one;
it checks only predicate subscriptions. Both collect entity IDs in a per-query
set, deduplicating repeated discoveries. These phases do not execute the queries
or update their result maps.

Index maintenance updates two nested dense maps:

- Attribute → entity → boxed value (AEV), for looking up an entity's attribute.
- Attribute → value → entity set (AVE), for finding entities matching a value.

The replacement tick removes 300 facts from both indexes and inserts 300 facts
into both, creating 300 boxed values. The boxes are shared between indexes.
Removal includes cleaning up empty value buckets and entity sets. Allocation,
hashing, copying new value bytes and releasing old values are included here.

Staging opens one database transaction, prunes no-op edits, encodes keys/values
and fingerprints, calls `mdb_del`/`mdb_put` per fact, and updates the entity-ID
counter if needed. It is outside the timings above. Commit and query execution
are also separate; disk flushing is asynchronous, and network/UI work is absent.

## Comparison limits

This run is slower across all major phases than the earlier profile. Its full
replacement edit is 10.792 ms median (staging 3.547 ms, queries 2.032 ms), versus
3.826 ms in the earlier run. These numbers therefore do **not** subdivide that
earlier 1.052 ms indexes/routing measurement or prove a code regression.
CPU frequency and scheduling were not pinned. Recorded thermal status stayed 0
and battery temperature was 32.3–32.4°C; those readings do not establish constant
CPU performance. Use the interleaved comparison to assess timer overhead.

## Why individual map timings are not reported as real costs

Both diagnostic comparisons used 16 fresh processes / 800 timed samples,
alternating coarse-only and detailed builds of the same algorithm. The CSV
labels `before` and `after` mean **coarse** and **detailed**, not old/new algorithms.

| Clock in per-operation timer | Coarse index loop, replacement | Detailed index loop, replacement |
|---|---:|---:|
| steady_clock | 1.356 ms | 3.587 ms |
| ARM generic counter | 1.234 ms | 3.123 ms |

Detailed fields split old-value lookup, AVE removal, AEV removal, value boxing,
AEV insertion and AVE insertion. However, thousands of timer reads, barriers and
accumulations change the cost of this tiny-operation workload substantially.
The ARM counter reads cost about 200 ns versus 247 ns for steady_clock on this
phone. A three-process clock probe agreed with steady_clock over 50 ms to within
0.002%, but lower read overhead was insufficient to avoid perturbation.

Do not treat the detailed CSV columns as uninstrumented allocation/map costs,
or subtract an average timer cost to claim precise corrected numbers. The
coarse three-phase split is the useful result. A finer allocation-versus-hashing
attribution remains unresolved by these measurements.

The ARM implementation follows the architected counter usage in
[Android's arm64 vDSO](https://android.googlesource.com/kernel/common/%2B/7bbf48947605d6ccef21a896c4b44dc356dc8726/arch/arm64/kernel/vdso/gettimeofday.S).

## Validation and evidence

Host engine tests pass with detailed profiling enabled. Both phone builds pass
native AELMDB/Littlebear integration; every benchmark tick checks query results
and notification counts. No indexing or staging optimization was made.

- [Counter comparison samples](benchmarks/datafriend-android-index-counter-breakdown.csv)
- [Counter comparison metadata and binary hashes](benchmarks/datafriend-android-index-counter-breakdown-metadata.json)
- [Standard-clock comparison samples](benchmarks/datafriend-android-index-breakdown.csv)
- [Standard-clock comparison metadata](benchmarks/datafriend-android-index-breakdown-metadata.json)
- [Clock validation](benchmarks/datafriend-index-clock-probe.json)

With coarse and detailed Android build directories containing the library,
simulation benchmark and network test, reproduce using:

```sh
python3 datafriend-benchmarks/benchmarks/run_android_simulation.py \
  --compare-build datafriend-benchmarks/build/android-routing-profile \
  --build datafriend-benchmarks/build/android-index-profile \
  --case 1000 4 10 stable group --case 1000 4 10 churn group \
  --rounds 4 --samples 50 --output-prefix datafriend-android-index-counter-breakdown
```
