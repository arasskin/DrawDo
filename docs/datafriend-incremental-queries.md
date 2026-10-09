# Incremental entity-local queries

One task edit now reevaluates one task in the task query, instead of all 1,000.
The engine identifies queries in which every clause has the same entity variable
or the same constant entity. It binds that entity to each changed record and
reuses the existing matcher, then prunes only that entity's previous result row.

This preserves value-variable equality, constants, self references, partial and
repeated projections, predicate entry/exit, missing required attributes and
whole-entity deletion. Shared query groups still evaluate once and notify every
subscribed handle only if their result changes. Both triple edits and entity
imports use the path. No-op records are discarded before query maintenance.

Cross-entity joins, disconnected clauses and all-facts queries retain full
evaluation. Initial subscriptions and rollback recovery also evaluate fully.
Classification is intentionally conservative: logically equivalent entity terms
expressed differently do not enter this path.

Within a mixed transaction, an affected local query checks all changed entities,
even if some entities changed only unrelated attributes. The reverse attribute
index still selects affected query groups. Per-entity dependency filtering is
not part of this change.

The native owner-thread handoff, AELMDB transaction, Dart FFI, field decoding and
whole-view Dart cache invalidation are unchanged. In particular, a `done` edit
still rebuilds the ordered ID list on the next read. These remain separate costs.

## Correctness and work

- Differential tests compare 12 maintained queries with fresh full evaluations
  after deterministic edge cases and 120 batches of randomized edits/imports.
  They compare exact triples, counts and notification IDs. Cases include
  unchanged projections, shared subscribers, entity zero/max, joins with remote
  dependencies, shared value variables, duplicate projections and whole deletions.
- Existing rollback tests inject failure after result mutation and verify all
  queries are recovered from committed storage before another edit succeeds.
- An instrumented 1,000-task case verifies exactly **5 candidate visits, 5 emitted
  facts, 5 previous-fact checks and 1 value replacement**, rather than revisiting
  5,000 facts. Two identical subscriptions still share one evaluation.
- Native suites, the instrumented work-count test and all 20 Flutter tests pass.
- Linux AddressSanitizer, UndefinedBehaviorSanitizer and leak checks pass (163 s).

## Comparison

The previous native library is preserved and hash-verified against the original
[Redux benchmark metadata](benchmarks/datafriend-redux-metadata.json). The runner
supports `--baseline-library`, running old/new native libraries through the same
unchanged Dart harness and interleaving them with the Redux implementations.
This isolates the query optimization from Dart read-path changes.

Three interleaved rounds, 2,000 warmups and 3,000 samples per case. All 30 small
validation processes and 90 timed processes pass. Values below are medians of
the three process medians, in **microseconds**:

| Tasks | Operation | Before | Incremental | Toolkit | Toolkit, no freeze | Handwritten Redux |
| ---: | --- | ---: | ---: | ---: | ---: | ---: |
| 100 | Edit | 42.125 | 14.417 | 11.958 | 1.917 | 0.375 |
| 100 | Edit + full read | 84.542 | 56.458 | 14.417 | 3.958 | 2.125 |
| 100 | Edit + 15 rows + count | 48.625 | 24.166 | 29.166 | 18.166 | 16.250 |
| 1,000 | Edit | 252.459 | 14.625 | 101.750 | 2.125 | 0.500 |
| 1,000 | Edit + full read | 632.209 | 397.916 | 115.084 | 15.083 | 8.917 |
| 1,000 | Edit + 15 rows + count | 276.250 | 39.042 | 126.708 | 20.667 | 19.042 |

For 1,000 tasks, the complete edit is **17.3× faster** and visible-row workload
is **7.1× faster** than the previous engine. The visible workload is now **2.1×**
the handwritten Redux time, although edit-only remains **29.3×** and full read
**44.6×**. The remaining work differs: Datafriend still performs the transaction,
thread handoff and typed Dart decoding; Redux updates a known JS task and reuses
existing row objects.

Incremental edit time is nearly unchanged between 100 and 1,000 tasks. Full
readback still scales with the result size, and ordered IDs still get sorted
after invalidation. This patch has not solved that Dart readback cost.

Median of process p99s, 1,000 tasks:

| Operation | Before | Incremental | Handwritten Redux |
| --- | ---: | ---: | ---: |
| Edit | 272.583 | 21.208 | 1.209 |
| Edit + full read | 731.791 | 487.458 | 42.458 |
| Edit + 15 rows + count | 297.208 | 46.167 | 24.084 |

Incremental visible p99 varied from 45.7 to 91.6 µs across processes, so the
summary is not a worst-case guarantee. These are macOS arm64 host timings with
Dart AOT and Node production builds. They exclude Flutter/React rendering,
networking and synchronous disk flush; transactions remain enabled.

[Raw timings](benchmarks/datafriend-redux-incremental.csv) and
[commands, versions and hashes](benchmarks/datafriend-redux-incremental-metadata.json).

```sh
python3 datafriend-benchmarks/benchmarks/run_redux.py --dart /path/to/dart \
  --baseline-library datafriend/build/redux-before-incremental.dylib \
  --output-prefix datafriend-redux-incremental
```

The preserved binary is a local measurement artifact, not a portable dependency;
reproducing the old path on another machine requires a build of the earlier source.
Historical results remain in separate files. The script can also run the current
implementation and Redux without a baseline library.

The subsequent [grouped query planner](datafriend-query-planner.md) adds fused
field gathering and selective execution ordering. The measurements here isolate
the earlier changed-entity optimization; see also the original
[Datalevin research notes](datafriend-datalevin-notes.md).
