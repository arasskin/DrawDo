# Datafriend and InstantDB todo demo

The runnable comparison is in [`src/todo_demo`](../src/todo_demo/README.md).
One Flutter macOS Release app contains both backends and shares the entire todo
screen. Both support add, complete, rename, delete, active/completed filtering and
local persistence; the selector opens a separate saved list for each backend.

This compares Datafriend with the community Dart package
[`flutter_instantdb` 2.3.0](https://pub.dev/packages/flutter_instantdb/versions/2.3.0),
not InstantDB's official JavaScript SDK or hosted service. No account or network
connection is used. The older `instantdb_flutter` is a different package.

## Experience

Datafriend reuses the existing native database, query subscriptions and selectively
cached task projections from `src/app`. Edits block until query results and Dart
notifications are consistent. Flutter's existing `ChangeNotifier` schedules the
widget rebuild. Building requires the C++ library and bundling/signing its dylib;
the supplied build script handles this. The adapter retains the existing empty
sticker/stroke subscriptions as well as tasks.

InstantDB is easier to add as a Dart dependency with platform plugins. Its
`db.tx['todos'][id].update(...)` API and reactive `db.query(...)` signal are compact.
However, `await db.transact(...)` completes the local write before the subscribed
query reflects it. This demo listens to the query signal and waits for the changed
value before enabling another edit. Both demos use the same interaction policy;
neither adds a separate optimistic UI overlay.

Two package-specific findings affect this experience:

- Version 2.3.0's query engine deliberately debounces store changes for **200 ms**
  before rerunning queries. That delay remains intact in the comparison. It is
  scheduling latency, not 200 ms of query CPU work.
- Its query engine translates ordering into `List<String>`, but the SQLite
  comparator only processes maps inside an ordering list. The initial full run
  exposed shuffled 100-row results. The adapter now requests unordered results
  and sorts its projection by the stored order and ID. That work is timed, and a
  100-row regression test verifies initial and subsequent ordering. No dependency
  code was modified. The failed diagnostic run is preserved separately from the
  successful comparison.

The source is available in the [published package archive](https://pub.dev/api/archives/flutter_instantdb-2.3.0.tar.gz):
`lib/src/query/query_engine.dart` (`_handleStoreChange`, `_queryEntities`),
`lib/src/storage/triple_query_eval.dart` (`compareEntities`), and
`lib/src/storage/triple_store.dart` (`queryEntities`). The latter fetches entity
IDs, then awaits an SQLite query for each entity and reconstructs its maps. This
is materially different from Datafriend's incremental result maintenance.

## Measurement boundaries

The harness runs the actual `sqflite_darwin` plugin inside the same Flutter Release
binary for both stores. It does not substitute an in-memory imitation or the
SQLite FFI implementation used by unit tests.

- 100 and 1,000 tasks; three interleaved rounds; 30 timed toggles after five warmups
  for each workload/profile/backend. Every toggle updates one entity's completion
  field. There is one task query result containing all tasks, not one query per task.
- `call_us`: elapsed time until the store method returns. For InstantDB it returns
  a Future, so this does not mean the write completed.
- `commit_us`: elapsed time until synchronous return/Datafriend or the InstantDB
  transaction Future completes, including its local sync-queue bookkeeping.
- `publish_us`: elapsed time until the listener observes the expected task value.
  These three durations start at the same point and must not be added together.
- `read_us`: subsequent traversal of 15 or all task models plus the completed
  count. Both adapters create Task models lazily. Datafriend retains unchanged
  row models across publications; the InstantDB adapter caches within each newly
  published result. SDK entity-map construction already happened before publication.
- `cached_full`: 1,000 measured reads after 100 warmups with no intervening edit.
  Datafriend's completion count scans the native result; InstantDB computes and
  stores the count at publication. Those costs fall on different sides of the
  publication/read boundary, and both are included in the combined workload.
- Store cases use a static Flutter screen to isolate the data path. A separate
  1,000-task case uses the real todo UI, 20 actions after five warmups and 250 ms
  pauses after completed actions. It records end-of-frame latency and Flutter
  build/raster timings. End-of-frame is not a physical-display presentation timestamp
  or a hardware-input measurement.

Buffered profiles use Datafriend `MDB_NOSYNC` and SQLite `synchronous=OFF`.
Durable profiles retain each backend's defaults and record SQLite's actual
PRAGMAs. These are not a proof of equivalent crash guarantees. Default Datafriend
flushing is currently synchronous; asynchronous flushing remains integration work.
Both network paths are excluded. InstantDB still keeps transaction history and
pending sync bookkeeping, while Datafriend has no equivalent live network outbox.
This is a package-level local app comparison, not a pure query-engine contest.

Each case starts in a fresh process with a fresh database. Setup/seeding and reopen
are recorded separately, and every case validates CRUD, checksums and exact
reopened task contents. The read workload uses the same deterministic UUIDs,
titles, order and booleans. The Flutter platform/UI threads are merged in this
runtime for both backends. No concurrent build or test runs during measurement.

Host: Apple M3 (8 CPU cores), 24 GiB RAM, macOS 26.5.1 arm64; Flutter 3.41.4,
Dart 3.11.1. Results are host macOS measurements, not iOS/Android or physical-device guarantees.
The bundle contains both backends, so its size is not a package-size comparison.

## Results

All **28 Release-app processes** passed CRUD, checksum and persistence checks.
All four tests passed and Dart analysis is clean. Screenshots were inspected for
both backends. Source and binary hashes match the recorded benchmark metadata.
The host's one-minute load average ranged from 3.58 to 8.78; these are interleaved
measurements on a working desktop, not an otherwise idle laboratory machine.

The following milliseconds are pooled across three rounds (90 samples per row),
using the visible-list workload: toggle one task, publish, then read 15 rows and
the completed count. Read time is additional to publication; write-completion and
publication times share the same start and are not additive.

| Tasks | Writes | Backend | Write complete p50 | Published p50 | Published p95 | Subsequent read p50 |
|---:|---|---|---:|---:|---:|---:|
| 100 | Buffered | Datafriend | 0.023 | 0.023 | 0.112 | 0.003 |
| 100 | Buffered | flutter_instantdb | 1.959 | 229.652 | 239.458 | 0.005 |
| 100 | Default durable | Datafriend | 3.600 | 3.595 | 5.889 | 0.023 |
| 100 | Default durable | flutter_instantdb | 2.170 | 229.159 | 237.840 | 0.005 |
| 1,000 | Buffered | Datafriend | 0.038 | 0.038 | 0.077 | 0.014 |
| 1,000 | Buffered | flutter_instantdb | 1.299 | 465.500 | 481.391 | 0.002 |
| 1,000 | Default durable | Datafriend | 3.738 | 3.733 | 4.884 | 0.130 |
| 1,000 | Default durable | flutter_instantdb | 1.685 | 465.035 | 477.268 | 0.002 |

SQLite reported `journal_mode=delete` in both profiles, `synchronous=0` buffered
and `synchronous=2` with defaults. InstantDB's durable write completes sooner in
this workload, but completing the write does not publish the new query result.
Datafriend's callback runs just before the blocking edit returns, hence its
publication measurement can be slightly less than write completion.

For 1,000 tasks with buffered writes:

| Workload | Datafriend p50 / p95 | flutter_instantdb p50 / p95 |
|---|---:|---:|
| Read all models after publication | 0.096 / 0.311 ms | 0.090 / 0.495 ms |
| Edit, publication and full read combined | 0.124 / 0.387 ms | 468.543 / 1,679.185 ms |
| Read all models again without an edit | 0.038 / 0.060 ms | 0.075 / 0.283 ms |

The combined samples are calculated as `max(commit_us, publish_us) + read_us`,
then percentiles are taken over those samples. Cached reads have 3,000 samples
per backend. Full-read publication has substantially worse tails in this client
than the visible-list workload; this run does not isolate the cause of those
outliers. Post-publication read times alone omit the SDK's earlier map construction.

### Rendered UI

With 1,000 tasks and default durability, 20 measured actions after five warmups:

| Measurement | Datafriend p50 / p95 | flutter_instantdb p50 / p95 |
|---|---:|---:|
| Action to updated UI's end-of-frame | 14.362 / 26.148 ms | 481.344 / 515.090 ms |
| Flutter frame build | 0.401 / 5.860 ms | 1.174 / 4.018 ms |
| Flutter frame raster | 1.343 / 1.912 ms | 0.478 / 1.725 ms |

The main difference is waiting for state publication, not drawing the todo list.
There were 266 recorded Datafriend frames and 547 InstantDB frames, including
animations and intermediate busy states; this is not one frame sample per action.
The frame case is one run per backend and only 20 actions, so its tail percentiles
are descriptive, not strong statistical estimates. End-of-frame latency includes
scheduling/vsync wait and is distinct from build/raster execution time.

### Lifecycle

Median of the three 1,000-task store processes, in milliseconds:

| Writes | Backend | Open, seed and initial query | Reopen and read all tasks |
|---|---|---:|---:|
| Buffered | Datafriend | 28.084 | 2.311 |
| Buffered | flutter_instantdb | 1,041.913 | 1,460.643 |
| Default durable | Datafriend | 3,355.456 | 2.253 |
| Default durable | flutter_instantdb | 1,045.231 | 1,373.495 |

Default-durable bulk seeding is a clear weak point for the current Datafriend
path. These setup times include backend initialization and the adapter's seeding
strategy; they are not isolated insert-throughput measurements.

For this local todo workflow, Datafriend's incremental results and retained row
models deliver the responsive interaction we wanted. Its native packaging and
unfinished asynchronous disk-flush path remain extra integration work. The
Flutter InstantDB package has a convenient dependency/transaction API, but its
current query debounce, per-entity SQLite fetching and ordering issue make it a
poor fit for our blocking, query-driven interaction target without changes.
These findings should not be generalized to InstantDB's official client/server.

## Artifacts

- [Run/build instructions](../src/todo_demo/README.md)
- [Per-round metric summaries](benchmarks/datafriend-instantdb.csv)
- [Environment, logs and source/binary hashes](benchmarks/datafriend-instantdb-metadata.json)
- [Raw per-operation samples](benchmarks/datafriend-instantdb-runs/)
- [Initial ordering diagnostic](benchmarks/datafriend-instantdb-ordering-diagnostic-metadata.json)
- [Datafriend screenshot](validation/todo-demo/datafriend.png)
- [InstantDB screenshot](validation/todo-demo/instantdb.png)
