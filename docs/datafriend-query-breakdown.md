# Datafriend query matching versus persistent-view maintenance

Measured 2026-09-27 on macOS 26.5.1 arm64, Release C++20. This extends the
[SQLite phase comparison](datafriend-sqlite-benchmark.md) to the real Datafriend
evaluator. The workload remains 10,000 entities, ten identical subscriptions and
optionally ten additional distinct queries, with synchronous disk flushing off.

Datafriend's advantage is mainly query execution. Persistent-view maintenance
costs approximately the same in all three backends and takes over half of
Datafriend's measured edit time.

The subsequent [detailed view profile](datafriend-view-maintenance.md) separates
key creation, lookups, membership tracking and actual persistent-tree writes.

## Comparable phase boundaries

The normal Datafriend evaluator streams matched facts into its persistent view.
A separate diagnostic build (`DATAFRIEND_BUFFER_QUERIES=ON`) instead collects the
actual projected triples in an owned vector, then performs the existing view
update. Timers surround whole phases, with no per-fact clock calls. The flag
defaults off; the normal build still streams directly and allocates no fact buffer.

For this comparison, SQLite also collects owned triples in its new `facts` mode.
This puts projection/fact construction in the query phase for every backend.
The earlier SQLite `buffered` mode collected `(entity, done, title)` rows and
constructed facts during view maintenance; that mode remains available. Its
historical measurements are preserved.

The query phase includes index lookups/joins, matching, projection, buffer
allocation and owned-fact collection. SQLite additionally reads its columns into
owned values. The view phase includes key hashing, seen-key tracking, persistent
lookups/value comparisons, actual replacements, old-result membership scans and
seen-set destruction. Fact-buffer destruction is separately timed. Tiny remaining
query overhead and all other edit work are recorded as residual phases.

## Results

Three rotated, sequential rounds per backend/mode/workload, each with 100 warmup
edits and 1,000 measured edits. The following are instrumented phase medians
(median of the three run medians), **total across all eleven evaluated queries**:

| Phase | Datafriend | SQLite task rows | SQLite indexed triples |
| --- | ---: | ---: | ---: |
| Query execution and owned projected facts | **136.8 µs** | 210.9 µs | 507.6 µs |
| Persistent-view maintenance | 202.7 µs | 200.0 µs | 202.2 µs |
| Complete blocking edit | **360.2 µs** | 434.1 µs | 744.2 µs |

Each query matches 100 entities and projects 200 facts. Query/fact-collection time
averages about 12.4 µs per query for Datafriend versus 19.2 µs for task-table
SQLite, a 35% reduction. View maintenance rechecks 2,200 projected facts and 2,200
old entries while only one published fact changes. The measurement identifies
the aggregate cost of that process; it does not isolate individual hash lookups
or the single tree replacement.

With only the one shared query, Datafriend's medians were 14.3 µs for query/fact
collection, 18.4 µs for view maintenance and 49.8 µs for the complete call.
SQLite task rows measured 19.8 / 17.7 / 55.8 µs respectively.

Phase medians do not necessarily sum to the median total. An additive breakdown
using means across the three equal-sized mixed-workload runs is:

| Phase | Datafriend | SQLite task rows | SQLite indexed triples |
| --- | ---: | ---: | ---: |
| Query execution and fact collection | 136.6 µs | 215.3 µs | 514.6 µs |
| View maintenance | 204.1 µs | 203.9 µs | 204.4 µs |
| Fact-buffer release and remaining query overhead | 2.2 µs | 2.3 µs | 2.4 µs |
| Remaining edit work, transaction, reconciliation and handoff | 20.4 µs | 24.8 µs | 37.1 µs |
| **Total** | **363.2 µs** | **446.4 µs** | **758.4 µs** |

Rounding can affect the last decimal. On these means, view maintenance represents
56% of Datafriend's complete edit. Reducing unnecessary full-result checks is a
larger remaining opportunity than the SQL comparison alone suggested.

## Measurement controls and validation

Buffering changes allocation and cache behavior, so these are diagnostic phase
boundaries rather than an exact decomposition of the streaming implementation.
Uninstrumented median full-edit controls were:

| Workload | Backend | Streaming | Owned-fact buffer | Change |
| --- | --- | ---: | ---: | ---: |
| 1 evaluated query | Datafriend | 47.1 µs | 47.2 µs | +0.2% |
| 1 evaluated query | SQLite task rows | 54.8 µs | 54.0 µs | −1.4% |
| 1 evaluated query | SQLite indexed triples | 87.0 µs | 86.9 µs | −0.1% |
| 11 evaluated queries | Datafriend | 349.4 µs | 348.8 µs | −0.2% |
| 11 evaluated queries | SQLite task rows | 434.3 µs | 428.9 µs | −1.2% |
| 11 evaluated queries | SQLite indexed triples | 732.8 µs | 731.8 µs | −0.1% |

All differences are under 2%. Results still include host scheduling noise;
instrumented timings include timers/counters and should not replace the ordinary
streaming totals when quoting production edit latency. No Dart, rendering or
network activity is included, and these are not physical-device frame guarantees.

All 81 benchmark processes passed exact fact/count/fingerprint checks, shared-root
checks, retained-snapshot checks and final-result checks. Buffered profiles also
verify every expected projected fact/SQL row was consumed after each edit. Small
runs cover uneven group sizes and odd toggle counts. SQLite integrity checks pass.
The native engine test suite passes in all four build configurations: ordinary,
profiled, buffered and buffered/profiled. These tests exercise generic query
behavior, not only this fixed benchmark shape.

## Reproduce

Build the ordinary `native` and `profile` targets as in the SQLite comparison
with `DATAFRIEND_BUFFER_QUERIES=OFF`. Then build both diagnostic variants:

```sh
cmake -S datafriend-benchmarks -B datafriend-benchmarks/build/native-buffered -DCMAKE_BUILD_TYPE=Release -DDATAFRIEND_SQLITE_BENCHMARK=ON -DDATAFRIEND_BUFFER_QUERIES=ON -DDATAFRIEND_PROFILE=OFF
cmake --build datafriend-benchmarks/build/native-buffered -j 4
cmake -S datafriend-benchmarks -B datafriend-benchmarks/build/profile-buffered -DCMAKE_BUILD_TYPE=Release -DDATAFRIEND_SQLITE_BENCHMARK=ON -DDATAFRIEND_BUFFER_QUERIES=ON -DDATAFRIEND_PROFILE=ON
cmake --build datafriend-benchmarks/build/profile-buffered -j 4
python3 datafriend-benchmarks/benchmarks/run_sqlite.py --native-breakdown
```

As before, AELMDB needs execution outside the macOS tool sandbox. The runner
selects the appropriate binary for each mode and preserves earlier result files.

Raw [uninstrumented controls](benchmarks/datafriend-native-breakdown.csv),
[phase profiles](benchmarks/datafriend-native-breakdown-profile.csv), and
[configuration, query plans and source hashes](benchmarks/datafriend-native-breakdown-metadata.json).
