# Where persistent-view maintenance spends time

Historical profile before packed EA keys and boxed values. See the
[implemented optimization and new comparison](datafriend-packed-benchmark.md).

Measured 2026-09-27 on macOS arm64, Release C++20, extending the
[query/view split](datafriend-query-breakdown.md). The workload is unchanged:
10,000 entities, eleven distinct queries, 100 matched entities and 200 projected
facts per query, one completion toggle per edit, no synchronous disk flush.

The main costs are rebuilding stable identity keys and validating unchanged
facts. The actual replacement of one published fact is inexpensive. This does
not show that persistence or path copying is the principal problem.

## Detailed diagnostic

A separate `DATAFRIEND_DETAIL_VIEWS=ON` build, requiring buffered queries, splits
view maintenance into whole passes. It avoids per-fact timers, which would be
intrusive at these operation sizes. The passes compute keys, build the seen set,
collect changed facts, apply changes, scan old membership, and release scratch
storage. Pending writes recheck the current result to preserve duplicate-projection
and collision behavior. The flag defaults off.

Three rotated rounds, each with 100 warmup edits and 1,000 measured edits, yield
these median phase times **across all eleven views** (median of run medians):

| Stage | Work per edit | Time |
| --- | --- | ---: |
| Encode and hash `(entity, attribute)` keys | 2,200 identities | **86.0 µs** |
| Tree lookups and equality/collision checks | 2,200 facts | **50.0 µs** |
| Build seen-key sets | 2,200 inserts | **22.9 µs** |
| Scan previous results for removals | 2,200 membership checks | **9.4 µs** |
| Apply actual tree replacements | 1 changed fact | **0.4 µs** |
| Release temporary sets/vectors | 11 views | **0.6 µs** |
| **Entire diagnostic view phase** | | **170.2 µs** |

The total includes timer/orchestration overhead. Independent medians do not
necessarily add up to the median total. The replacement phase includes changed
payload hashing, payload allocation, path copying and its pending-write recheck;
the destruction of old published roots after publication is outside this phase.
There are no view deletions in this particular completion-toggle workload.

Identity-key creation calls `pair_key` for every emitted fact. It constructs the
canonical byte representation with the codec writer and hashes it with XXH64.
This timer includes serialization, hashing and collection of the resulting keys;
it does not isolate the hash primitive. These stable identity keys differ from
the triple-content fingerprints, which are calculated only for actual writes in
the view-maintenance path.

Each current tree `get()` returns an owning entry copy. For these views, its value
is a `shared_ptr<const triple>`, so lookup/comparison includes ownership bookkeeping
as well as tree traversal and equality checks. That cost has not been separated
into its individual components. The seen sets are newly allocated/grown for each
evaluation, then used to detect facts absent from the new result.

## Measurement changes performance

The detailed pass ordering is itself faster on this workload. It is therefore
an approximate attribution guide, **not an exact partition of the earlier
203 µs view-maintenance figure**:

| Eleven-query workload | Normal streaming | Buffered, interleaved maintenance | Detailed passes |
| --- | ---: | ---: | ---: |
| Uninstrumented median complete edit | 346.3 µs | 349.1 µs | **315.9 µs** |
| Instrumented median view maintenance | not separately timed | 201.3 µs | **170.2 µs** |

Detailed passes reduce the uninstrumented complete edit by about **9%** versus
streaming. The precise mechanism (instruction/cache behavior versus other effects
of regrouping work) has not been isolated. This is a diagnostic implementation,
not a production optimization already enabled. All runs are sequential host
measurements; tail noise and mobile-device performance remain separate concerns.

## Optimization candidates

1. **Carry cached identity keys from the indexes.** `(entity, attribute)` identity
   is stable across value edits. Computing its key once and retaining it alongside
   the indexed fact can eliminate repeated serialization/hash work during refresh.
   The tradeoff is additional cached metadata; the hash format need not change.
2. **Use borrowed lookups during an evaluation.** While its root is retained,
   the engine can inspect an entry without constructing a new owning `shared_ptr`.
   Keep ownership at the snapshot boundary, and do not allow borrowed entries to
   escape that lifetime. Avoid repeating identity comparisons already performed
   by the collision check. Measure these changes within the combined lookup stage.
3. **Reserve or reuse the seen-set storage.** The prior result size provides a
   useful initial capacity; the single owner thread can also reuse scratch storage.
   This reduces allocation/growth without changing persistence semantics.
4. **Consider an ordered merge for larger savings.** Sorting projected keys and
   merging against the existing view could replace separate lookups, seen-set
   construction and the removal scan. Sorting is an added cost and must be measured;
   it is not an established improvement yet.

The current measurements favor improving the surrounding validation work before
redesigning the persistent tree or its allocator. More selective query invalidation
could avoid entire refreshes, but requires additional dependency logic and is a
separate design change.

## Validation and reproduction

All 41 benchmark processes passed result, root, fingerprint and snapshot checks.
Profiled edits also assert exactly one published tree replacement and unchanged
result membership. The engine suite passes in normal-profile, buffered-profile,
detailed and detailed-profile builds, including a new repeated-projection test
covering insertion, replacement, removal of the last join match and retained roots.

After building the ordinary and buffered configurations from the earlier report:

```sh
cmake -S datafriend-benchmarks -B datafriend-benchmarks/build/native-detailed -DCMAKE_BUILD_TYPE=Release -DDATAFRIEND_SQLITE_BENCHMARK=ON -DDATAFRIEND_BUFFER_QUERIES=ON -DDATAFRIEND_DETAIL_VIEWS=ON -DDATAFRIEND_PROFILE=OFF
cmake --build datafriend-benchmarks/build/native-detailed -j 4
cmake -S datafriend-benchmarks -B datafriend-benchmarks/build/profile-detailed -DCMAKE_BUILD_TYPE=Release -DDATAFRIEND_SQLITE_BENCHMARK=ON -DDATAFRIEND_BUFFER_QUERIES=ON -DDATAFRIEND_DETAIL_VIEWS=ON -DDATAFRIEND_PROFILE=ON
cmake --build datafriend-benchmarks/build/profile-detailed -j 4
python3 datafriend-benchmarks/benchmarks/run_sqlite.py --view-breakdown
```

AELMDB requires execution outside the macOS tool sandbox. Raw
[uninstrumented controls](benchmarks/datafriend-view-breakdown.csv),
[phase profiles](benchmarks/datafriend-view-breakdown-profile.csv), and
[run metadata/source hashes](benchmarks/datafriend-view-breakdown-metadata.json)
are preserved separately from prior runs.
