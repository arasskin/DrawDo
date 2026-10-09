# Asynchronous Datafriend disk flushing

The default now commits edits and publishes query results without waiting for a
disk flush. One native worker batches pending commits on a 100 ms cadence. Writes
do not postpone an existing deadline, so continuous editing cannot starve flushing.
The main app and Datafriend todo demo both use this default. The database format
is unchanged.

```dart
final db = Datafriend.open(path); // asynchronous flushing
// Edits still return with all query results and callbacks consistent.
db.edit(add: [Triple(entity, 'done', true)]);
// Optional blocking checkpoint, outside the normal edit path:
db.flush();
db.close(); // drains pending writes and reports flush failures
```

`DatafriendDurability.synchronous` keeps the old per-commit flush behavior.
`DatafriendDurability.buffered` disables automatic flushing for diagnostic
comparisons. Legacy `durable:true/false` retain those two meanings; omitting both
options selects asynchronous flushing. The C ABI uses mode 0/1/2 respectively for
buffered/synchronous/asynchronous. Rebuild the native library with the Dart binding.

## Ownership and errors

The flush worker never touches queries, indexes, published maps or Dart callbacks.
It holds the stable database file descriptor returned by AELMDB and uses
`F_FULLFSYNC` on Apple platforms and `fsync` on the other supported POSIX platforms.
These synchronize the non-WRITEMAP database file without reading AELMDB's mutable
metadata from another thread. The engine joins the worker before closing the
file/environment. No result read locks were added.

Each successful transaction marks pending work, including schema dictionary,
entity-ID allocation and whole-entity imports. A flush acknowledges only the
commits captured before its syscall; writes arriving during a flush remain
pending. The worker releases its coordination mutex throughout disk I/O. Normal
edits never wait for the flush barrier, although OS/filesystem contention can
still affect their elapsed time.

A failed flush retains pending work and retries after 100 ms. New writes report
the error until a flush succeeds; existing views remain readable. `flush()` makes
an immediate attempt and reports failure. Checked Dart/C close releases the
handle even on failure and reports the error; no-throw destruction/finalization
logs a final failure. Shutdown drains newer writes even when an earlier flush
was already in flight.

The 100 ms interval is a batching target, not a hard persistence deadline: slow
I/O or suspension can extend it. Pending writes are not yet durable. This retains
AELMDB's `MDB_NOSYNC` crash semantics: without filesystem write-order guarantees,
a system/power failure before synchronization can corrupt the database, not merely
lose recent transactions. No new crash-recovery/WAL protocol is claimed. A
successful explicit barrier/clean close is the durability checkpoint. Networking
is unchanged and remains separate integration work.

## Validation

Native tests cover coalescing, progress during continuous writes, writes during
an intentionally blocked sync, draining those writes at shutdown, error reporting
and retry, default async engine behavior, imports/ID persistence and the C barrier/
checked-close API. Dart tests verify synchronous notifications, no additional
notifications from flushing, and persistence after close.

The native Release suite, all 24 main-app tests, all four demo tests and Dart
analysis pass. Linux ASan/UBSan/leak checks pass for the engine and flush worker;
Linux ThreadSanitizer passes the worker's concurrency tests. The Mac TSAN runtime
could not start even a trivial program; Linux was used instead.
Android arm64 debug and iOS simulator debug builds pass, including the CocoaPod's
new private worker header. The macOS Release demo also builds successfully.

## Benchmark method

Run the Datafriend-only comparison:

```sh
python3 src/todo_demo/tool/run_flush_benchmarks.py --flutter /path/to/flutter
```

The three profiles run in a rotated order, in fresh processes/databases, at 100
and 1,000 tasks. Three rounds use 30 measured edits after five warmups for visible
rows and full reads. A third workload pauses 16 ms between edits, allowing the
background worker to run during sustained activity. Cached full reads use 1,000
samples after 100 warmups. An explicit flush, CRUD and exact reopen checks run
outside the edit timers. No InstantDB runs and no network are included.

Each profile also runs three rendered-UI cases at 1,000 tasks, with 20 measured
actions after five warmups and 250 ms pauses after each completed frame. For each
action the harness records:

- Data ready: edit, query publication and the controller's completion.
- Wait for frame: data ready to Flutter's next frame-start callback.
- Framework frame: the frame-start callback to the post-frame callback.
- Post-frame resume: the post-frame callback to resumption of the awaiting Dart
  benchmark. This separates any engine/microtask handoff delay from frame work.
- Updated frame build/raster: the engine FrameTiming matched by its frame number,
  excluding unrelated animation frames from these particular metrics. The ID is
  captured in the post-frame callback because this Flutter version updates its
  frame data after the frame-start callback.

The first four durations partition the measured action-to-end-of-frame-Future interval.
Their separately computed medians need not sum to the median total. End-of-frame
is not a physical-display presentation timestamp. The actions are programmatic,
not hardware input events. Results are macOS Release on an Apple M3, 24 GiB RAM,
Flutter 3.41.4/Dart 3.11.1, and do not establish phone performance.

## Results

All 27 final processes passed CRUD, exact reopen and checksum checks. Recorded
source and binary hashes match the final implementation. One-minute host load
averaged 3.59–5.94 across process starts; no builds, tests or sanitizer runs ran
alongside these measurements. Small differences should be read in the context of
normal desktop scheduling variance.

Pooled medians / p95, in milliseconds, over 90 edits per workload/profile:

| Tasks | Workload | Synchronous flush | Async flush | Buffered, no auto flush |
|---:|---|---:|---:|---:|
| 100 | Edit to publication | 3.384 / 4.547 | 0.034 / 0.077 | 0.052 / 0.218 |
| 100 | Edit + 15 rows/count | 3.446 / 4.852 | 0.039 / 0.100 | 0.061 / 0.537 |
| 1,000 | Edit to publication | 3.811 / 6.249 | 0.092 / 0.287 | 0.066 / 0.312 |
| 1,000 | Edit + 15 rows/count | 3.921 / 6.456 | 0.126 / 0.323 | 0.092 / 0.359 |
| 1,000 | Edit + full read/count | 4.168 / 7.431 | 0.252 / 1.194 | 0.200 / 0.675 |
| 1,000 | Paced edit to publication | 3.732 / 6.522 | 0.294 / 0.602 | 0.127 / 0.405 |
| 1,000 | Paced edit + 15 rows/count | 3.853 / 6.652 | 0.438 / 0.796 | 0.195 / 0.606 |

Combined durations are calculated per sample as
`max(commit_us, publish_us) + read_us` before taking percentiles. The 16 ms pause
is outside the paced edit timer. The async worker remains enabled throughout,
including those pauses. Async has coordination/background-I/O overhead relative
to the unflushed diagnostic mode, but removes the multi-millisecond flush wait.
Per-round async 1,000-task publication medians ranged from 0.040 to 0.136 ms; the
pooled value should not be interpreted as a precise hardware-independent constant.

For 1,000 tasks, median lifecycle measurements across the three store processes:

| Measurement | Synchronous | Async | Buffered |
|---|---:|---:|---:|
| Open, seed and initial queries | 3,343.898 ms | 71.657 ms | 28.523 ms |
| Explicit final flush barrier | 3.187 ms | 3.300 ms | 4.986 ms |
| Reopen and read all tasks | 2.492 ms | 2.590 ms | 2.453 ms |

The flush still costs time; it has moved off ordinary edits and onto the worker.
The explicit barrier intentionally waits for it. Reopen checks verify contents,
not physical power-loss behavior.

### Actual edited frames

Pooled medians / p95 in milliseconds, over 60 actions per profile. Build/raster
figures below are matched to the updated frame, not averaged over animation frames.

| Measurement | Synchronous | Async | Buffered |
|---|---:|---:|---:|
| State ready, including controller | 3.904 / 5.208 | 0.391 / 0.946 | 0.379 / 0.874 |
| Wait for frame-start callback | 4.327 / 8.299 | 7.298 / 11.947 | 7.628 / 11.117 |
| Framework start → post-frame callback | 6.636 / 8.573 | 6.883 / 8.292 | 6.476 / 8.625 |
| Post-frame callback → await resumes | 0.037 / 0.092 | 0.035 / 0.100 | 0.038 / 0.077 |
| Total action → await resumes | 15.166 / 19.535 | 14.498 / 19.566 | 14.070 / 19.608 |
| Engine build duration, updated frame | 6.694 / 8.607 | 6.920 / 8.299 | 6.513 / 8.648 |
| Engine raster duration, updated frame | 1.269 / 1.792 | 1.163 / 3.475 | 1.218 / 1.841 |

The four callback-based phases sum exactly to each recorded total. Build/raster
measure overlapping engine intervals and must not be added to that total.

The edit finishes much earlier with async flushing, but the next frame still
follows display scheduling. The benchmark's 250 ms pause after each frame also
biases where actions land within that cadence; these are not randomly phased
hardware taps. Removing a 3–4 ms disk wait therefore does not imply an equal
reduction in this end-of-frame latency. The remaining substantial application
work is the roughly 7 ms Flutter rebuild, with roughly 7 ms of pre-frame waiting
in the async case. The old all-frame median near 0.4 ms understated the cost of
an edited frame because it included cheaper animation-only frames.

The query/storage path now fits comfortably within the observed desktop frame
budget. This run does not prove zero missed deadlines or establish mobile-device
performance; the remaining UI build cost is visible and independent of flushing.

## Artifacts

- [Final per-round summaries](benchmarks/datafriend-async-flush.csv)
- [Logs, load and source/binary hashes](benchmarks/datafriend-async-flush-metadata.json)
- [Raw samples](benchmarks/datafriend-async-flush-runs/)
- [Runner](../src/todo_demo/tool/run_flush_benchmarks.py)
- [Flush worker](../datafriend/src/async_flush.h)

Earlier `*-initial`, `*-frame-id-diagnostic` and smoke outputs retain development
measurements. Their frame correlation was provisional; use the final unsuffixed
artifacts above for the reported comparison.
