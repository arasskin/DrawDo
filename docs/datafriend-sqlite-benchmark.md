# Datafriend and SQLite: shared-view edit workload

Measured 2026-09-27 on macOS 26.5.1 arm64, Release C++20 builds. SQLite is the
system library, reporting version 3.51.0; its exact source ID and configuration
are recorded in the [run metadata](benchmarks/datafriend-sqlite-metadata.json).

Datafriend is in the expected performance range for this workload. Its median
edit takes about 20% less time than SQLite's conventional task table with eleven
queries, and 53% less than SQLite's indexed triple table. This compares complete
native edit calls, including persistent result views, rather than SQL execution
alone. No production storage or query-engine code was changed for this comparison.

## Results

Three sequential rounds, rotating backend order and alternating workload order.
Each process uses a fresh database, 100 warmup edits and 1,000 measured edits.
The median column is the median of the three run medians, not a pooled percentile.
p99 ranges show the three individual runs' tail variation.

| Workload | Backend | Median edit | Run p99 range |
| --- | --- | ---: | ---: |
| 10 identical handles, 1 evaluated query | Datafriend | **48.2 µs** | 60.5–137.7 µs |
| Same | SQLite indexed triples | 87.3 µs | 106.2–123.0 µs |
| Same | SQLite task rows | 55.3 µs | 64.2–171.3 µs |
| 20 handles, 11 evaluated queries | Datafriend | **346.5 µs** | 369.4–391.2 µs |
| Same | SQLite indexed triples | 738.7 µs | 808.0–1,095.8 µs |
| Same | SQLite task rows | 434.5 µs | 476.1–666.6 µs |

For the mixed workload, observed maxima across all three runs were 0.591 ms,
8.630 ms and 2.325 ms respectively. These host measurements include scheduler
and filesystem noise; they do not establish a mobile frame-time guarantee.

A separate instrumented run isolates query execution **and persistent view
maintenance**, including old-result membership checks. Median total time in this
phase, across all evaluated queries per edit:

| Distinct queries | Datafriend | SQLite indexed triples | SQLite task rows |
| --- | ---: | ---: | ---: |
| 1 | 32.2 µs | 59.4 µs | 38.4 µs |
| 11 | 336.0 µs | 713.7 µs | 419.8 µs |

The two SQL layouts execute identical logical queries. The triple layout needs
two indexed joins, whereas the task layout retrieves all projected fields from
one row. This is a plausible explanation for the difference, not a separately
isolated measurement of join overhead. Datafriend uses its existing in-memory
hash indexes. Query/result maintenance dominates the mixed workload in all three.

## Follow-up: SQL reads versus view maintenance

The subsequent [Datafriend phase breakdown](datafriend-query-breakdown.md)
measures the native evaluator too, using an owned-triple boundary in every backend.
The historical SQL row-buffer measurements below remain unchanged.

The original query timer combined SQL and view work. The follow-up adds a
`buffered` diagnostic mode: first execute each prepared query and copy all rows
into owned `(entity, done, title)` values, then feed those values through the same
view materializer. This places timers around whole loops instead of every row.
The original `stream` mode still interleaves SQL reads and view updates.

Three rounds of 1,000 measured edits per mode/backend/workload, after 100 warmups,
produced these phase medians (median of the three run medians):

| Evaluated queries | SQLite layout | SQL execution + owned rows | View maintenance | Full blocking edit |
| --- | --- | ---: | ---: | ---: |
| 1 | Task rows | 19.7 µs | 17.9 µs | 56.5 µs |
| 1 | Indexed triples | 42.0 µs | 17.9 µs | 90.7 µs |
| 11 | Task rows | **208.3 µs** | **204.0 µs** | 435.3 µs |
| 11 | Indexed triples | 505.6 µs | 206.3 µs | 746.3 µs |

All figures in the mixed rows are totals for eleven queries, each returning 100
rows. The task-table SQL/read phase therefore takes about **19 µs per query**.
It includes statement reset/binding, all `sqlite3_step` calls, column decoding,
string copies and allocation of a row buffer. It is not a measurement of SQLite
VM execution alone. Row-buffer destruction is timed separately.

The view phase includes constructing projected facts, hashing pair identities,
tracking seen keys, checking values against the persistent tree, replacing changed
facts, scanning the previous result for removals, and releasing temporary tracking
storage. In the mixed workload it checks 2,200 facts and 2,200 old entries while
only one published fact changes. This benchmark does not further isolate tree
writes from hash/look-up/membership costs.

For an additive breakdown, means across the three equal-sized buffered runs are:

| Mixed workload phase | SQLite task rows | SQLite indexed triples |
| --- | ---: | ---: |
| SQL execution and owned-row reads | 209.3 µs | 517.7 µs |
| View maintenance | 204.9 µs | 207.6 µs |
| Row-buffer release and query orchestration | 1.8 µs | 1.8 µs |
| Remaining edit work, including transaction/reconciliation/handoff | 21.6 µs | 36.4 µs |
| **Total** | **437.6 µs** | **763.5 µs** |

Means are affected by scheduling outliers; phase medians need not add up to the
median total. View maintenance accounts for about **47%** of the task-table edit's
mean time. The original full-workflow result thus substantially exceeds the SQL
read cost. The indexed triple query's joins make SQL reads a larger fraction.

Buffering changes execution order, cache behavior and temporary allocations, so
the phase split is a diagnostic approximation. Uninstrumented controls quantify
its effect on median full-edit time:

| Evaluated queries | SQLite layout | Original streaming | Buffered | Change |
| --- | --- | ---: | ---: | ---: |
| 1 | Task rows | 54.8 µs | 54.4 µs | −0.8% |
| 1 | Indexed triples | 87.1 µs | 86.8 µs | −0.4% |
| 11 | Task rows | 435.5 µs | 427.7 µs | −1.8% |
| 11 | Indexed triples | 732.9 µs | 733.1 µs | +0.03% |

The Datafriend control remained at 47.1 µs for one evaluated query and 345.0 µs
for eleven. Its recursive query evaluator remains unchanged; this follow-up does
not separately measure Datafriend's query traversal and view-maintenance costs.
No production engine or persistent-tree implementation changed.

All 67 follow-up processes passed the existing fact/root/snapshot checks. Buffered
profiles also verify the number of rows consumed after every edit, including
small runs with uneven group sizes and an odd final toggle. Run after rebuilding
both targets with the commands below:

```sh
python3 datafriend-benchmarks/benchmarks/run_sqlite.py --breakdown
```

This preserves the original measurements and writes separate
[streaming/buffered controls](benchmarks/datafriend-sqlite-breakdown.csv),
[phase timings](benchmarks/datafriend-sqlite-breakdown-profile.csv), and
[run metadata](benchmarks/datafriend-sqlite-breakdown-metadata.json).

## Matched work

- 10,000 entities, each with `group = id % 100`, `done = "false"` and
  `title = "task"`. EAV storage contains 30,000 facts; task storage 10,000 rows.
- Ten subscriptions share the query for group 1. The mixed workload adds ten
  distinct queries for groups 2 through 11. Each query returns 100 entities and
  projects the 200 actual `done`/`title` facts. Thus each mixed edit checks 2,200
  projected facts in total, not 2,200 per query.
- Every edit toggles entity 1's `done`, with no no-op edits. All distinct queries
  reevaluate because they depend on `done`; only group 1's published content
  changes. Both SQLite adapters deliberately follow the same invalidation policy.
- One private owner thread per backend. The timed blocking call includes the
  packaged-task/future roundtrip, private working view, local LBR3 reconciliation,
  transaction and index updates, full query execution, result-tree maintenance,
  commit, publication and temporary-root reclamation.
- SQLite uses the same Datafriend persistent tree, pair keys, triple hashes,
  unchanged-fact checks, old-result membership scan and shared-handle roots.
  Every result row is consumed. These features belong to the benchmark adapter,
  not SQLite itself. No Dart, UI rendering or network activity is measured.
- Database creation, seeding, subscription, statement preparation, `ANALYZE`,
  initial SQLite checkpoint and correctness checks happen outside timing.

This is a fixed-workload adapter, not an alternative general-purpose Datafriend
implementation. SQL supports only this benchmark's completion replacement and
query shapes. Unlike Datafriend, it does not encode whole entity records or
maintain database-wide count/XOR aggregates for Littlebear reconciliation.
The task schema also indexes fewer attribute combinations than the generic
triple schema. Neither comparison measures cold-cache behavior or memory usage.

## SQLite setup and query plans

Both SQL databases are file-backed, with WAL, `synchronous=OFF`, a 64 MiB cache
budget, 4 KiB pages and automatic checkpointing at 1,000 pages. Automatic
checkpoints during timed edits remain included. Datafriend uses `MDB_NOSYNC`.
These settings exclude synchronous disk flushes, while retaining database writes
to the OS. This does not benchmark a completed background-durability mechanism.
See SQLite's [synchronous setting](https://www.sqlite.org/pragma.html#pragma_synchronous)
and [WAL checkpoint behavior](https://www.sqlite.org/wal.html#ckpt).

Indexed EAV schema and query:

```sql
CREATE TABLE triples (
  entity INTEGER NOT NULL,
  attribute TEXT NOT NULL,
  value TEXT NOT NULL,
  PRIMARY KEY (entity, attribute)
) WITHOUT ROWID;
CREATE INDEX ave ON triples(attribute, value, entity);

SELECT d.entity, d.value, t.value
FROM triples g
JOIN triples d ON d.entity = g.entity AND d.attribute = 'done'
JOIN triples t ON t.entity = g.entity AND t.attribute = 'title'
WHERE g.attribute = 'group' AND g.value = ?;
```

Observed plan: covering `ave` lookup for the group, then primary-key lookups
for `done` and `title`. No full-table scan or forced query-plan hint.

Conventional task schema and query:

```sql
CREATE TABLE tasks (
  entity INTEGER PRIMARY KEY,
  group_id TEXT NOT NULL,
  done TEXT NOT NULL,
  title TEXT NOT NULL
);
CREATE INDEX task_group ON tasks(group_id);
SELECT entity, done, title FROM tasks WHERE group_id = ?;
```

Observed plan: `task_group` index lookup. Statements are prepared once, reused
with bound parameters, and run on a single-owner `SQLITE_OPEN_NOMUTEX` connection.
SQL updates use a single `UPDATE` inside `BEGIN IMMEDIATE`/`COMMIT`; the reconciled
remove/add pair is one scalar replacement.

## Validation and reproduction

All 27 benchmark processes passed expected-fact, count, fingerprint and shared-root
checks. Each verifies both toggle directions, retained old snapshots and unchanged
roots for unaffected groups before timing, then checks final results afterward.
SQLite additionally runs `integrity_check` and reconstructs fresh results from SQL
to compare with the published roots. Three small runs use 237 entities and three
measured toggles to cover unequal group sizes and an odd final toggle. Result
checks are explicit exceptions and remain enabled in Release builds.

The optional target adds no SQLite dependency to the production library or mobile
builds. Reproduce from the repository root:

```sh
cmake -S datafriend-benchmarks -B datafriend-benchmarks/build/native -DCMAKE_BUILD_TYPE=Release -DDATAFRIEND_PROFILE=OFF -DDATAFRIEND_SQLITE_BENCHMARK=ON
cmake --build datafriend-benchmarks/build/native --target sqlite_comparison -j 4
cmake -S datafriend-benchmarks -B datafriend-benchmarks/build/profile -DCMAKE_BUILD_TYPE=Release -DDATAFRIEND_PROFILE=ON -DDATAFRIEND_SQLITE_BENCHMARK=ON
cmake --build datafriend-benchmarks/build/profile --target sqlite_comparison -j 4
python3 datafriend-benchmarks/benchmarks/run_sqlite.py
```

The native AELMDB calls require execution outside the macOS tool sandbox.
Direct arguments: `sqlite_comparison datafriend|sqlite_eav|sqlite_tasks EXTRA
[ENTITIES=10000] [SAMPLES=1000] [stream|buffered|facts]`. Datafriend `buffered`
requires its separate diagnostic build; SQLite `facts` uses the same owned-triple
phase boundary, while SQLite `buffered` retains the earlier owned-row boundary.
The runner records sequential runs rather than
running competing benchmarks in parallel.

Source: [comparison harness](../datafriend-benchmarks/benchmarks/sqlite.cpp),
[runner](../datafriend-benchmarks/benchmarks/run_sqlite.py).
Raw results: [uninstrumented rounds](benchmarks/datafriend-sqlite.csv),
[instrumented phases](benchmarks/datafriend-sqlite-profile.csv),
[configuration, plans, checks and source hashes](benchmarks/datafriend-sqlite-metadata.json).
