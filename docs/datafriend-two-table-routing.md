# Two-table subscription routing experiment

The experiment lives in the `datafriend-benchmarks` linked worktree. Production
is unchanged. `DATAFRIEND_DIRECT_SUBSCRIPTIONS=OFF` builds the current predicate
router; `ON` builds concrete dependencies plus predicate match subscriptions.

## Implementation

Eligible entity-local queries still choose one required constant predicate. The
two subscription tables are:

1. Packed entity/attribute key → set of query groups.
2. Predicate attribute → value → set of query groups.

An entity satisfying the selected predicate has concrete links for **all** the
query's clause attributes, even absent attributes and attributes not projected.
It remains a candidate when another condition fails. This catches a missing
attribute being added and a secondary predicate becoming true. Full query
execution still checks all conditions, variable equalities and projections.

Subscription creation finds initial candidates through the existing AVE index.
Each group also keeps a reverse set of candidate entity IDs for link cleanup.
Identical queries share both results and routing registrations.

An edit first routes through concrete links in the old state. After normal query
index updates, the match table discovers new candidates when a predicate changes.
Departing and arriving candidates remove/add their concrete links. These updates
are inside the measured blocking edit, before query execution and publication.
An ordinary x/y edit does not rebuild its candidate links.

Fixed-entity routing and conservative attribute routing for other queries are
retained. Rollback rebuilds the concrete links from committed data. Final
unsubscribe removes both kinds of subscription, including incomplete candidates.

## Measurement

All builds use the same source and Android NDK, arm64 Release, asynchronous disk
flushing. The main comparison has no internal timing instrumentation. Five warmup
ticks precede each process's timed samples; before/after order alternates within
cases and case order is shuffled reproducibly. Every tick verifies view sizes,
live values and notification counts. Network integration runs before timing.

The baseline and candidate perform the same database writes and query evaluation.
100 replacements mean 300 fact deletions and 300 fact insertions in one transaction.
100 stable edits mean updating x on one entity in each of 100 affected views.

Additional columns record initial view setup time and process peak RSS before
and after subscribing. Setup includes query evaluation, registration, obtaining
result handles and size checks. RSS is a process high-water mark, **not** exact
live subscriber allocation; mapped database pages, allocator reuse and runtime
memory also contribute. Setup values repeat in each tick's CSV row but summaries
count them once per fresh process.

With 1,000 disjoint four-entity views and three dependent attributes, the candidate
logically maintains 12,000 concrete links, 4,000 reverse candidate memberships and
1,000 match registrations. The baseline has 3,000 predicate registrations.
Concrete dependencies can therefore be comparable in count to the facts themselves;
small views alone do not guarantee a small subscriber index. Overlapping views and
low-selectivity predicates can increase this further and are not covered by this
disjoint-view matrix.

## Results on the Moto G7

The materialized subscriber index gives a small ordinary-edit gain, but does not
justify replacing the current router in this implementation. Entity replacement
is slower, and the subscriber sets increase startup cost and memory.

The ordinary scheduler matrix has 12 shapes × two builds × four rounds × 50
samples: **4,800 timed edits across 96 processes**. Scheduling variation obscures
small differences: for example, the unchanged 10-edit fixed-query control varies
by 4.7% in aggregate median, and pooled medians sometimes disagree with the median
of paired process ratios. Raw results are retained below.

A follow-up keeps both builds on CPU 0, with six shapes × two builds × four
rounds × 100 samples: **4,800 edits across 48 processes**. No phone settings or CPU
frequencies were changed. All rows have 1,000 views. Times are milliseconds,
median (nearest-rank p95), with no internal phase instrumentation:

| Workload | Current router | Two tables | Median change |
|---|---:|---:|---:|
| 4 entities/view, edit 10 | 0.429 (0.536) | 0.427 (0.565) | −0.5% |
| 4 entities/view, edit 100 | 2.267 (2.554) | 2.180 (2.446) | −3.9% |
| 4 entities/view, replace 10 | 0.745 (0.883) | 0.832 (0.963) | +11.7% |
| 4 entities/view, replace 100 | 5.652 (6.251) | 6.564 (7.159) | +16.1% |
| 16 entities/view, edit 100 | 2.693 (3.087) | 2.617 (2.911) | −2.8% |
| Fixed-entity control, edit 100 | 1.891 (2.131) | 1.896 (2.179) | +0.2% |

The paired-round median ratios agree: 0.962 for 100 ordinary edits, 1.163 for
100 replacements, and 1.004 for the fixed-entity control. The 10-edit ordinary
case is effectively unchanged; its paired-round ratio is 1.000.

For the 1,000 four-entity-view/100-edit shape, initial view setup is **96.4 →
121.3 ms**. The median increase in process peak RSS during setup is **1,138 →
5,148 KiB**, about **3.92 MiB additional high-water growth**. For 16 entities/view,
setup is **131.1 → 220.4 ms** and candidate peak growth is 19,290 KiB versus zero
additional peak growth for the baseline. Zero here means setup stayed below the
previous process high-water mark, not that it allocated no memory.

The ordinary scheduler run recorded 33.0–33.1°C battery temperature; the CPU-0
control recorded 33.1–33.2°C. Thermal status stayed 0. Frequency snapshots still
vary, so CPU affinity is a scheduling control, not a fixed-frequency guarantee.
These are paired comparisons, not claims of improvement over historical timings.

## Where the difference goes

A separate CPU-0 run adds only coarse phase timers: 24 processes, 1,200 edits,
200 samples per workload/build. It confirms the expected tradeoff. Medians in ms:

| Workload / phase | Current router | Two tables |
|---|---:|---:|
| 100 ordinary edits: old-state routing | 0.303 | 0.253 |
| 100 ordinary edits: AEV/AVE index updates | 0.168 | 0.167 |
| 100 ordinary edits: new-state routing | 0.099 | 0.006 |
| 100 ordinary edits: entire index/routing region | 0.576 | 0.426 |
| 100 replacements: old-state routing | 0.540 | 0.605 |
| 100 replacements: AEV/AVE index updates | 0.702 | 0.761 |
| 100 replacements: new-state routing and link maintenance | 0.336 | 0.976 |
| 100 replacements: entire index/routing region | 1.611 | 2.394 |

Ordinary x edits avoid the old predicate lookup chain and almost eliminate the
new-state pass. That saves about 0.15 ms in the whole region, but it is only part
of the blocking edit. Query/view work measures 0.795 → 0.855 ms in this profile;
the measurements do not isolate the cause of that difference.

Replacements remove 300 concrete links, add 300 new links and update reverse
candidate sets. The new-state pass includes this work and grows by about 0.64 ms.
The ordinary data indexes use the same algorithm, although their measured cost
also differs somewhat. Phase medians do not sum exactly to the median total.

Staging is similar in this paired profile: 0.386 → 0.382 ms for ordinary edits,
1.825 → 1.805 ms for replacements. Fixed-query index/routing time is also similar:
0.362 → 0.361 ms. Battery temperature was 33.3°C and thermal status stayed 0.

The experiment is retained behind its default-off option. On these workloads,
the modest steady-membership benefit does not offset replacement and memory costs.

## Validation

- All six native candidate suites pass.
- Both implementations pass the engine suite including the new incomplete-
  candidate, alias, repeated-clause, unsubscribe and rollback regressions.
- Existing randomized checks compare incremental results and notifications with
  full evaluation and an independent reference evaluator.
- Candidate engine tests pass UBSan with halt-on-error enabled.
- Both phone variants pass AELMDB/Littlebear integration in all three comparison
  runs. All 10,800 timed ticks pass result and notification checks.

## Reproduction

Configure separate Android build directories with the same options, changing
only `-DDATAFRIEND_DIRECT_SUBSCRIPTIONS=OFF` / `ON`. Common options:

```sh
cmake -S datafriend-benchmarks -B datafriend-benchmarks/build/two-table-android \
  -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE="$ANDROID_NDK/build/cmake/android.toolchain.cmake" \
  -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-28 \
  -DDATAFRIEND_TESTS=OFF -DDATAFRIEND_PORTABLE_BENCHMARKS=ON \
  -DDATAFRIEND_DIRECT_SUBSCRIPTIONS=ON
cmake --build datafriend-benchmarks/build/two-table-android -j 4
```

Use `--case` repeatedly to select shapes. `--cpu 0` restricts only the benchmark
process and its threads to one CPU, without modifying phone settings; it does
not fix CPU frequency. Omit it for the ordinary scheduler comparison.

```sh
python3 datafriend-benchmarks/benchmarks/run_android_simulation.py \
  --compare-build datafriend-benchmarks/build/predicate-table-android \
  --build datafriend-benchmarks/build/two-table-android \
  --case 1000 4 10 stable group --case 1000 4 10 churn group \
  --rounds 4 --samples 100 --cpu 0 --output-prefix datafriend-android-two-table-control
python3 datafriend-benchmarks/benchmarks/summarize_routing.py \
  docs/benchmarks/datafriend-android-two-table-control.csv
```

Coarse phase builds add `DATAFRIEND_PROFILE=ON`, leaving
`DATAFRIEND_PROFILE_INDEXES=OFF`. Candidate new-state routing includes link
maintenance; the ordinary AEV/AVE update loop remains the index-update phase.

## Evidence

- [Ordinary scheduler samples](benchmarks/datafriend-android-two-table-routing.csv),
  [summary](benchmarks/datafriend-android-two-table-routing-summary.csv),
  [conditions and binary hashes](benchmarks/datafriend-android-two-table-routing-metadata.json).
- [CPU-0 control samples](benchmarks/datafriend-android-two-table-control.csv),
  [summary](benchmarks/datafriend-android-two-table-control-summary.csv),
  [conditions and binary hashes](benchmarks/datafriend-android-two-table-control-metadata.json).
- [Coarse profile samples](benchmarks/datafriend-android-two-table-profile.csv),
  [summary](benchmarks/datafriend-android-two-table-profile-summary.csv),
  [conditions and binary hashes](benchmarks/datafriend-android-two-table-profile-metadata.json).
- [Validation record](validation/datafriend-two-table-routing.json).
