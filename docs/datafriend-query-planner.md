# Grouped query execution and selective access

Implemented the three findings from the [Datalevin review](datafriend-datalevin-notes.md):
group clauses by entity, start from selective index buckets, and avoid unused
value bindings. This is a purpose-built implementation for Datafriend's
single-valued entity/attribute model; no upstream code was copied.

## Execution

At subscription time, compilation groups clauses with the same entity variable
or constant entity, records which facts are projected, and counts variable uses.
Each group tests constant predicates and shared-variable constraints before
fetching fields used only for projection. A value that occurs once needs no
binding slot: results contain the actual matched facts, so projecting that fact
does not require carrying a separate variable binding. Required-field existence
tests remain, even when neither the fact nor its value is projected.

At execution time, attribute-map pointers are resolved once for that evaluation.
For each unprocessed entity group, the planner considers:

- A bound entity: one direct field gather.
- A bound attribute value: the exact size of its AVE entity bucket.
- An unbound attribute: the size of its AEV entity map.

The smallest available candidate set starts the next group. Newly bound values
and references inform the next choice; counts come from current indexes rather
than a cached statistical estimate. Empty buckets short-circuit the branch.
The selected entity's fields are gathered in a loop, and only joins between
groups recurse. Original clause indices still control projection, so reordering
does not change which facts are returned. Duplicate projections remain sets.

This is a small greedy planner, not Datalevin's full cost-based planner. It does
not estimate combined predicate correlations, enumerate complete join trees,
add hash joins, or introduce ordered indexes. A grouped read still performs the
needed hash lookups; it is not a single EAV cursor scan like Datalevin's storage.

The prior [incremental entity-local path](datafriend-incremental-queries.md)
remains: local edits evaluate only changed entities. General joins still rerun
fully, now through grouped execution and selective ordering. Atomic transactions,
failure recovery, callback semantics, C/Dart ABI and Dart read caches are unchanged.

## Correctness

- Existing 12-query randomized edit/import tests now compare with a separate
  brute-force evaluator over plain entity records, in addition to fresh queries.
- Six additional query shapes each run under 20 clause permutations with remapped
  projections. Cases include reference chains, shared values, self references,
  cycles, missing fields and variable 63.
- A 32-group case exercises the group bit mask and required unprojected fields.
- Instrumented tests verify 5 fact visits for one task edit, and 201 visits for
  a selective reference join that formerly walks 1,000 tasks through three clauses.
  Removing its last selective anchor causes zero candidate visits and clears the
  old result.
- Native, instrumented native and all 20 Flutter tests pass.
- Linux address/undefined-behavior/leak sanitizers pass (233 s).

## Broader benchmark

The native matrix runs **25 workloads at 100, 1,000 and 10,000 tasks**. Every
fixture also has 100 referenced owner entities. Tasks have 16 fields; the UI
Redux benchmark remains a separate five-field task workload.
The native matrix uses short raw byte values; the app benchmark uses the actual
Dart value codec. Their absolute timings should not be treated as phase breakdowns
of the same operation.

| Category | Workloads |
| --- | --- |
| Initial subscriptions | Wide 16-field projection; 1% filter first/last; 50% filter; forward/reverse reference joins; two-hop chain; value equality join; same-entity equality; disconnected existence condition; empty required attribute |
| Random task edits | Wide, selective, forward/reverse reference, chain and value-join results |
| Other mutations | Predicate entry/exit; referenced-owner change; insert/delete; 32-entity batch; 32 shared subscriptions; 32 distinct subscriptions; unrelated attribute; no-op |

`query_matrix.cpp` is one executable that loads the preserved old or new native
library through the C ABI. The baseline already includes incremental entity-local
queries. Identical fixtures, requests and seeded edit traces are used for both.
The independent oracle scans ordinary entity records and checks exact result
facts and notification IDs at initial, post-warmup and final checkpoints.
Every operation contributes to a notification-count digest, also compared across
old/new runs. Small odd/even traces cover all workloads before measurement.

Each timed case uses 20 warmups and 100 measured calls, in three processes with
different deterministic edit seeds. Workload and old/new order rotate across
rounds. Subscription timing includes decoding, compilation and initial evaluation;
unsubscribe and result checks are outside timing. Edit timing includes request
decoding, owner handoff, the database transaction and query maintenance. Request
encoding, result validation and oracle updates are outside timing. Database
creation and fixture loading are also excluded. No tests or builds run alongside
timed measurements.

Results are host wall-clock timings with `MDB_NOSYNC`: no synchronous disk flush,
network or UI rendering. They are not a complete frame-time or mobile guarantee.

All **100 small validation processes and 450 timed processes** pass. Median of
three process medians, **10,000 tasks plus 100 owners**, in microseconds:

| Workload | Incremental baseline | Grouped planner | Speedup |
| --- | ---: | ---: | ---: |
| Initial wide 16-field result | 11,423.1 | 6,294.5 | 1.81× |
| Initial 1% filter, written first | 31.5 | 22.5 | 1.40× |
| Initial 1% filter, written last | 1,169.8 | 22.2 | 52.67× |
| Initial 50% filter | 1,806.1 | 698.5 | 2.59× |
| Initial forward-reference join | 419.0 | 15.1 | 27.70× |
| Initial two-hop chain | 703.7 | 15.9 | 44.33× |
| Initial value join | 4,891.7 | 94.5 | 51.76× |
| Edit requiring reference join | 429.8 | 26.8 | 16.04× |
| Edit requiring two-hop chain | 719.0 | 26.5 | 27.17× |
| Edit requiring value join | 4,803.8 | 70.8 | 67.90× |
| Changed referenced owner | 722.0 | 25.7 | 28.13× |
| 32-entity edit batch | 263.2 | 240.5 | 1.09× |
| Local wide-result edit | 26.0 | 26.8 | 0.97× |
| Unrelated attribute edit | 19.9 | 19.8 | 1.00× |
| No-op edit | 8.8 | 9.0 | 0.97× |

The big gains come from reducing candidate enumeration, especially in queries
whose original clause order started with a broad scan. Literal-first/last forms
now have nearly identical costs. Grouped gathering also improves wide initial
results, even when all entities must be visited.

Already-incremental local edits mostly stay flat. Across all 75 cases, the
largest median regression is **3.7%** (100-task, 32 shared subscriptions:
16.958 → 17.583 µs). Some other small cases regress by 1–3.5%; these are not
universal speedups, and three host runs do not distinguish every sub-microsecond
change from scheduling noise. The results do not imply similar gains for arbitrary
joins or correlated distributions: this planner uses exact individual bucket
sizes, not complete join cardinality estimates.

The 16-field, 10,000-task initial result still takes **6.3 ms natively** and
contains 160,000 projected facts. This is substantially larger than the normal
UI workload, and result materialization remains significant when everything is
requested.

[Complete 75-case table including p99](datafriend-query-planner-matrix.md),
[raw timings](benchmarks/datafriend-query-planner.csv), and
[run metadata](benchmarks/datafriend-query-planner-metadata.json).

## App/Redux comparison

The existing Dart AOT harness is also rerun with old/new libraries and all three
Redux configurations. It retains its original 2,000 warmups, 3,000 samples and
three interleaved rounds, so a small query-kernel gain is not confused with a
change in Dart object construction or read-cache behavior.

All **30 validation and 90 timed app processes** pass. At 1,000 tasks, medians
in microseconds:

| Operation | Incremental baseline | Grouped planner | Toolkit | Toolkit, no freeze | Handwritten Redux |
| --- | ---: | ---: | ---: | ---: | ---: |
| Edit | 14.625 | 14.208 | 99.667 | 2.166 | 0.500 |
| Edit + all rows | 397.458 | 391.541 | 114.833 | 14.917 | 8.833 |
| Edit + 15 rows + count | 39.375 | 38.792 | 125.917 | 20.958 | 18.750 |

The simple app workload improves only slightly: roughly 0.4 µs for edit-only
and 0.6 µs for the visible-row operation. Its query already checks just one
entity, so better full-query planning cannot reproduce the previous 17× gain.
The visible operation remains about twice handwritten Redux; edit-only remains
about 28×, and full Dart readback about 44×. Further improvements there require
addressing the unchanged transaction/dispatch and Dart read costs, not just
query planning.

Grouped-planner p99s for these three operations are 20.292 / 493.291 / 46.083 µs
(median of process p99s). These summaries are not worst-case bounds.

[App/Redux raw timings](benchmarks/datafriend-redux-planner.csv) and
[run metadata](benchmarks/datafriend-redux-planner-metadata.json).

## Reproduce

```sh
python3 datafriend-benchmarks/benchmarks/run_query_matrix.py \
  --baseline-library datafriend-benchmarks/build/query-before-planner.dylib
python3 datafriend-benchmarks/benchmarks/run_redux.py --dart /path/to/dart \
  --baseline-library datafriend-benchmarks/build/query-before-planner.dylib \
  --output-prefix datafriend-redux-planner
```

The preserved baseline is an ignored local artifact, hash-verified against the
previous incremental-query benchmark. Other hosts need a corresponding old-source
build. The matrix runner supports other entity counts, sample counts and round
counts; `--validate-only` exercises the small correctness traces without timing.
Sources, binary hashes, commands, compiler/configuration and host load averages
are recorded in each run's metadata.
