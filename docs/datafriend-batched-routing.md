# Batched routing and separate subscription maintenance

Implemented only in the `datafriend-benchmarks` worktree, behind the default-off
`DATAFRIEND_BATCH_ROUTING` CMake option. It works with either the current predicate
router or the experimental two-table router (`DATAFRIEND_DIRECT_SUBSCRIPTIONS`).
Production is unchanged.

## What changed

Routing now finishes before data-index mutation. The normalized, no-op-pruned
edit map supplies pending values and deletions; unchanged values come from the
old indexes. This describes both complete states without exposing intermediate
per-fact states. Queries still execute against the fully updated indexes.

For the current predicate router:

- Iterate edits grouped by entity (the existing packed-key map is ordered).
- Cache each required predicate's old value and pending value/hash for that
  entity, reusing them across changed attributes.
- Visit the old subscriber bucket once per dependency, and the new bucket only
  when that predicate actually changes to a present value. Unchanged predicates
  no longer get looked up again after index updates.

For the two-table router:

- Read old concrete EA subscriptions and new predicate matches in one pass.
- Collect affected query/entity pairs and append candidate link removals/additions
  to a flat vector. Pending values are hashed without allocating value boxes.
- After updating the data indexes, apply the planned link operations directly.
  There is no second subscriber-discovery pass or nested refresh map.

A candidate group has one chosen constant predicate. Because staging removes
no-op predicates, a changed predicate's old and new matching groups are disjoint;
the same group/entity cannot receive conflicting link operations. Dependencies
still include missing/nonprojected attributes and partial matches. Fixed-entity
queries and general joins retain their existing routing rules.

## Measurement boundaries

- **Routing** (`routing_us`): subscriber lookups, deduplication of affected
  query/entity pairs, predicate cache and subscription update planning. For the
  unbatched versions it is the sum of old/new discovery times per sample.
- **Data-index updates** (`index_update_us`): the existing AEV/AVE mutation loop.
- **Subscription maintenance** (`routing_maintenance_us`): update concrete EA
  subscriber sets and reverse candidate memberships. The unbatched version also
  reads final predicate values here; the batched version already planned these
  decisions while routing. Ordinary predicate routing has no per-edit subscriber
  structure changes, so this field is zero.
- **Indexes/routing region** (`indexes_us`): the surrounding region, including
  setup and overhead not inside those individual timers.

Staging, query/view execution, commit and full blocking-edit measurements remain
separate. Disk flushing is asynchronous; network and UI execution are excluded.
No per-record clocks are enabled. Main throughput comparisons disable all native
phase clocks; a separate build measures the coarse components.

## Blocking edit comparison

Moto G7, Android 10, arm64 Release, CPU 0 affinity, asynchronous durability.
Five shapes × four variants × four rounds × 75 timed edits: **6,000 samples across
80 fresh processes**, after five warmups per process. Internal phase timers are
disabled. Medians in milliseconds; all grouped rows have 1,000 four-entity views:

| Workload | Current | Current batched | Two tables | Two tables batched |
|---|---:|---:|---:|---:|
| Edit 10 | 0.433 | 0.425 | 0.426 | 0.426 |
| Edit 100 | 2.267 | 2.188 | 2.186 | 2.168 |
| Replace 10 | 0.747 | 0.737 | 0.838 | 0.808 |
| Replace 100 | 5.695 | 5.502 | 6.546 | 6.340 |
| Fixed-entity control, edit 100 | 1.893 | 1.890 | 1.914 | 1.879 |

For the current router, batching reduces 100 ordinary edits by **3.5%** and 100
replacements by **3.4%**. The median of per-round paired ratios agrees (0.965 and
0.966). For two tables, the corresponding pooled-median changes are **−0.8%** and
**−3.1%**; paired-round ratios are 0.995 and 0.966. The small ordinary-edit change
is not a convincing additional win. Two-table replacement remains slower than
the current router, even with batching.

Replacement p95 goes **6.287 → 6.018 ms** for the current router and **7.192 →
6.886 ms** for two tables. The fixed-entity control uses no predicate discovery
or concrete-link maintenance; its small differences are a control for overhead
and run variation, not evidence of a predicate-routing improvement.

Battery temperature stayed 33.1°C and thermal status 0. CPU frequency was not
fixed. These are within-run comparisons; do not compare their absolute values
with previous phone runs as if device conditions were identical.

## Routing versus subscription maintenance

A separate coarse-profile run uses three shapes × four variants × four rounds ×
50 samples: **2,400 edits across 48 processes**. Battery temperature stayed 33.2°C,
thermal status 0. These are phase medians in ms, not subdivisions of the exact
uninstrumented median above; medians do not sum exactly.

For 100 entity replacements (300 removed facts and 300 inserted facts):

| Router | Routing lookups/collection | Subscription maintenance | AEV/AVE updates | Entire index/routing region |
|---|---:|---:|---:|---:|
| Current | 0.887 | 0 | 0.697 | 1.615 |
| Current batched | 0.706 | 0 | 0.708 | 1.440 |
| Two tables | 0.892 | 0.707 | 0.756 | 2.403 |
| Two tables batched | 0.558 | 0.812 | 0.744 | 2.181 |

The batched two-table path reduces lookup/collection time by about 0.334 ms, but
its measured maintenance phase increases by about 0.105 ms. It applies a flat
edit-ordered link plan rather than the previous query-grouped refresh sets; this
changes update order and memory access. The current measurements do not isolate
why maintenance is slower, so they do not establish an allocator or cache cause.
The combined region still improves by about 0.222 ms. Link maintenance remains
larger than subscriber lookup in the batched two-table replacement case.

For 100 ordinary x edits:

| Router | Routing lookups/collection | Subscription maintenance | AEV/AVE updates | Entire index/routing region |
|---|---:|---:|---:|---:|
| Current | 0.403 | 0 | 0.172 | 0.575 |
| Current batched | 0.306 | 0 | 0.166 | 0.475 |
| Two tables | 0.257 | <0.001 | 0.167 | 0.426 |
| Two tables batched | 0.259 | <0.001 | 0.170 | 0.431 |

There are no links to change in this workload; the tiny nonzero maintenance
reading is the empty-scope timer overhead. Two-table ordinary-edit routing is
effectively unchanged by batching. Current-router routing improves because the
unchanged predicate is no longer resolved twice.

The fixed-entity control's whole index/routing region remains 0.354–0.362 ms
across the four variants. Staging, queries and commit remain separate and have
not been optimized here. Full profile medians are available in the summary CSV.

Both experiments remain default-off in the benchmark worktree. Batching is a
modest improvement for the current router on the tested shapes; it does not make
the two-table replacement workload competitive with the batched current router.

## Validation

Both batched configurations pass all six native suites and the UBSan engine
suite. Both unbatched baselines also pass the updated engine suite. New regression
coverage includes simultaneous owner/projection changes, missing predicates,
different selected predicates, arrivals/departures and no-op predicate removal/
reinsertion. Existing randomized oracle, rollback and subscription lifetime tests
remain enabled.
All four phone variants pass native network integration in both timing runs.
All **8,400 timed edits** pass view content/size checks and notification-count
checks; recorded recomputation counts match the expected affected view count.

## Reproduction

Build four arm64 Release variants from this worktree, using the same Android NDK:

| Variant | DIRECT_SUBSCRIPTIONS | BATCH_ROUTING |
|---|---|---|
| predicate | OFF | OFF |
| predicate_batch | OFF | ON |
| direct | ON | OFF |
| direct_batch | ON | ON |

All option names have the `DATAFRIEND_` prefix. Use `DATAFRIEND_TESTS=OFF` and
`DATAFRIEND_PORTABLE_BENCHMARKS=ON`. Profile builds additionally set
`DATAFRIEND_PROFILE=ON`; leave `DATAFRIEND_PROFILE_INDEXES=OFF`.

```sh
python3 datafriend-benchmarks/benchmarks/run_android_simulation.py \
  --variant predicate datafriend-benchmarks/build/routing-batch/predicate \
  --variant predicate_batch datafriend-benchmarks/build/routing-batch/predicate_batch \
  --variant direct datafriend-benchmarks/build/routing-batch/direct \
  --variant direct_batch datafriend-benchmarks/build/routing-batch/direct_batch \
  --case 1000 4 1 stable group --case 1000 4 10 stable group \
  --case 1000 4 1 churn group --case 1000 4 10 churn group \
  --case 1000 1 10 stable fixed \
  --rounds 4 --samples 75 --cpu 0 --output-prefix datafriend-android-batched-routing
python3 datafriend-benchmarks/benchmarks/summarize_routing.py \
  docs/benchmarks/datafriend-android-batched-routing.csv
```

The runner rotates variant position across four rounds and shuffles case order.
CPU affinity restricts only the benchmark process and its threads; device settings
and CPU frequency are unchanged. Metadata records build options, source/binary
hashes, temperatures, frequency snapshots and network test results.

## Evidence

- [Uninstrumented samples](benchmarks/datafriend-android-batched-routing.csv),
  [summary](benchmarks/datafriend-android-batched-routing-summary.csv),
  [build options and device conditions](benchmarks/datafriend-android-batched-routing-metadata.json).
- [Coarse profile samples](benchmarks/datafriend-android-batched-routing-profile.csv),
  [summary](benchmarks/datafriend-android-batched-routing-profile-summary.csv),
  [build options and device conditions](benchmarks/datafriend-android-batched-routing-profile-metadata.json).
- [Validation record](validation/datafriend-batched-routing.json).
