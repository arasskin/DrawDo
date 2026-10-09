# Shared query subscriptions

Identical query descriptions now share one evaluated persistent view within a
Datafriend engine. Each subscriber still receives an independent handle. The
registry compares a length-prefixed encoding of the complete description:
all-facts flag, ordered clauses, variables/constants, and ordered projection.
Equality uses the complete bytes, not a potentially colliding hash. Different
constants, projections or variable IDs create separate groups; this does not
attempt semantic equivalence or variable renaming.

The first subscriber evaluates the query. Subsequent identical subscriptions
join its group without evaluating again or constructing another root. Attribute
dependencies reference groups, so each affected group evaluates once per commit.
The commit stages one result per group and swaps its root after storage commit;
all handles immediately see that same root. Local edits still use private
persistent working values, leaving the shared published baseline unchanged
until commit. Multi-view edits can include several handles in the same group.

Closing a handle removes only that owner. Closing the last handle removes the
group and its dependency registrations. Independently retained native snapshots
remain valid. A later subscription reevaluates against current storage.
`last_recomputed()` now counts distinct evaluations, not subscriber handles.

## Benchmark

Both before and after runs used the same native workload: 10,000 entities, three
facts per entity, one completion toggle per edit, 100 warmups and 1,000 measured
edits. Disk flushes were disabled (`MDB_NOSYNC`); networking and Dart were absent.
These are total blocking-call times from the uninstrumented Release build.

| Subscriptions | Evaluations before → after | Median before → after | p99 before → after |
| --- | ---: | ---: | ---: |
| 10 identical | 10 → 1 | 312.3 → **47.9 µs** | 1,484.8 → 71.3 µs |
| 10 identical + 10 different | 20 → 11 | 606.3 → **346.8 µs** | 676.9 → 488.1 µs |

The additional queries select groups 2 through 11, while the repeated query
selects group 1. Every query matches 100 entities and projects 200 facts. All
reference the edited completion attribute, so all 11 distinct queries are
invalidated. Only group 1's result actually changes; the other ten queries still
rerun completely and preserve their unchanged roots. Changed-entity incremental
evaluation is not part of this change.

An instrumented run confirms the work reduction:

| Work per edit | 10 identical before → after | Mixed before → after |
| --- | ---: | ---: |
| Clause-candidate visits | 3,000 → 300 | 6,000 → 3,300 |
| Projected fact checks | 2,000 → 200 | 4,000 → 2,200 |
| Old-result membership checks | 2,000 → 200 | 4,000 → 2,200 |
| Result-tree replacements | 10 → 1 | 10 → 1 |
| Mean query evaluation time | 305.3 → 33.0 µs | 618.5 → 340.5 µs |

The runs are sequential host measurements, not paired latency guarantees. The
before run had more scheduling noise; avoid attributing the entire p99 decrease
to sharing. The mixed after run also had a 6.5 ms maximum outlier. Raw timings:
[before](benchmarks/datafriend-sharing-before.csv),
[after](benchmarks/datafriend-sharing-after.csv),
[profile before](benchmarks/datafriend-sharing-profile-before.csv),
[profile after](benchmarks/datafriend-sharing-profile-after.csv).

Reproduce after building `build/native` in Release and `build/profile` in Release
with `DATAFRIEND_PROFILE=ON`:

```sh
python3 datafriend-benchmarks/benchmarks/run_sharing.py
```

Direct arguments are `engine_benchmark ENTITIES IDENTICAL DURABLE ADDITIONAL`
and `engine_profile ENTITIES IDENTICAL ADDITIONAL`. The profile uses no fsync.
The regular benchmark runner also includes the new mixed workload.

Validation covers separate handles with shared roots, constants and projections
that must not share, edits through aliases, atomic multi-alias batches, failed
edits, final-owner cleanup, resubscription, wildcard groups, incoming entity
changes, and snapshots retained after unsubscribe/engine closure.

Production and instrumented native suites, all 14 Flutter tests, Dart analysis,
and the Linux ASan/UBSan engine suite pass. No new integration blocker was found.
