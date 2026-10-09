# Live mutable query results

The latest app read path is [direct native field access](datafriend-direct-reads.md).
Task/sticker models now use [selective row caching](datafriend-row-cache.md).
The measurements below describe the earlier bulk-reader stage; the live mutable
map and notification design remain unchanged.

Datafriend owns an AELMDB littlebear, mutable AEV/AVE query indexes, and one
`unordered_dense` map (`packed entity/attribute → boxed value`) per distinct
query. Subscriptions share that live map. There are no persistent query trees,
replacement maps, local negentropy, generation counters, or result read locks.
The generic persistent-tree prototype remains only for historical experiments.
The [phone view-layout comparison](datafriend-android-views.md) evaluates ordered
alternatives; those are benchmark adapters, not a new production tree selection.

## Interface and notifications

```dart
final view = database.subscribe(query, onChanged: () {
  // Invalidate a projection or request a Flutter rebuild.
});
final result = view.result(); // retained reference to the live native map

database.edit(add: [Triple(entity, 'done', true)]);
// All affected results and synchronous notifications are complete here.

result.close();
view.close();
```

Edits belong to the namespace, independently of queries. A batch retracts
matching current triples, then adds/replaces entity–attribute pairs. Stale-value
retractions do nothing. The view's set methods are conveniences for direct edits;
they do not reconcile or restrict edits to its current membership.

Native Datafriend returns changed subscription IDs in the blocking edit/import
reply. Dart invokes each subscription's `onChanged` on the calling isolate before
`edit` returns. All native maps are already current when callbacks run. Unchanged
results produce no callback, even when their queries were reevaluated. Duplicate
subscriptions share evaluation but each receives its own callback.
Within the callback, `view.changes` supplies changed result entities/attributes
and a membership flag. Failed edits conservatively notify all subscriptions with
a reset summary before rethrowing; adapters clear their caches in that case.

The app's callbacks evict changed task/sticker models by entity ID and retain
ordering unless membership or sort fields changed. Grouped stroke projections
still rebuild conservatively when notified. The existing optional
`onPublished` batch hook runs after all query callbacks; it lets the controller
issue one Flutter notification after every projection has been invalidated.
This is the same notification delivery, with no polling or revision checks.
`ChangeNotifier` and `ListenableBuilder` schedule the actual widget rebuild.
Callbacks may read results, but must not edit the store or throw.

## Blocking flow and ownership

`result.entities(orderBy: 'order', thenBy: 'part')` sorts ascending by the
requested integer fields, with missing attributes last for each field and entity
ID breaking ties. With no sort fields it sorts by entity ID. A field absent from
every result row contributes only ties. Present non-integer values remain errors.
Sorting reads projected attributes in the result, not unprojected database fields.

1. Normalize explicit triple edits into packed entity/attribute updates.
2. Stage changed facts in an atomic AELMDB transaction.
3. Mutate indexes; use attribute/pair dependencies to select query groups.
4. For entity-local queries, reevaluate and prune only transaction-changed entities
   in the existing flat map. Partial pruning probes projected attribute keys;
   it does not scan unrelated entities. Cross-entity joins and all-facts queries still
   evaluate fully. Reuse unchanged boxes and remove facts no longer emitted.
5. Commit, return changed subscription IDs, and deliver Dart callbacks.

See [incremental query validation and comparison](datafriend-incremental-queries.md).

The UI thread is blocked during native work. The interface requires one caller
to serialize result reads, edits, imports and close. A result handle can outlive
its subscription or engine, retaining its final contents; it never retains old
versions. Iteration copies the current triples, in unspecified order, into Dart.
Individual reads are not safe concurrently with edits. The owner queue's mutex
and condition variable only coordinate dispatch to the native owner thread.

Incoming network changes must be applied at this same UI serialization boundary;
a background thread cannot independently mutate a published map. Disk flushing
and network I/O can run asynchronously without violating that rule.

## Failure recovery

Trust AELMDB's transaction atomicity. If failure occurs after index/result
mutation, abort the transaction, rebuild indexes from committed facts, and
clear/refill every active query's existing map before returning the error. No
change callbacks are delivered for the failed edit. If recovery itself fails,
the store and its result handles reject reads until reopened. Allocation
exhaustion is not promised to be recoverable.

## Storage boundary

One namespace per store; uint32 entity IDs and a generated shared uint32 attribute
enum; immutable shared boxes. AEV/AVE indexes remain dense mutable maps.
[AELMDB v3](datafriend-fact-storage.md) uses eight-byte packed entity/attribute
keys and fingerprint-prefixed values. Count/XOR aggregates and future network
reconciliation now operate on facts. Old disk formats are rejected.

The default now uses [asynchronous disk flushing](datafriend-async-flush.md).
Query publication remains synchronous; the disk worker never accesses live maps.
Legacy `durable:true/false` select synchronous/unflushed diagnostic modes. The
live network driver remains unfinished integration work.

## Measurements for this revision

The AOT app-view workload ran 100 warmups and 1,000 measured edits in each of
three rotated rounds, with 3 stickers and an unchanged 5,000-point drawing.
All 24 processes (including small odd-parity smoke cases) passed result and
notification-count checks. Durability is MDB_NOSYNC; no network traffic.

| Tasks | Direct edit only | Edit + complete Dart projection readback | Whole-document adapter fixture |
| ---: | ---: | ---: | ---: |
| 100 | 75 µs | 412 µs | 578 µs |
| 1,000 | 395 µs | 4,788 µs | 6,938 µs |

These are medians of round medians. Results varied across rounds; this is not a
controlled speedup comparison with older runs. Each mode uses the new backend.
The measurement excludes Flutter widget build/layout/paint and synchronous disk
flush; it does not establish a full-frame latency bound.

[Raw results](benchmarks/datafriend-live-results.csv) and
[commands/source hashes](benchmarks/datafriend-live-results-metadata.json).

Native correctness tests, all 18 Flutter tests (profiling off/on), Flutter analysis,
Android arm64 and iOS simulator debug builds pass. Linux ASan/UBSan/leak checks
pass with the longer debug-test timeout. Native/SQLite benchmark smoke checks
pass for all three backends.

## Rerun after load decreased

Repeated the same three rotated rounds, 100 warmups and 1,000 measured edits
per case. All 24 processes passed correctness and notification checks. A CPU
sample during the run was 64% idle; the long-term load averages were still high.
No implementation changes were made for this rerun.

| Workload | Earlier persistent-view run | Mutable maps, rerun | Less elapsed time |
| --- | ---: | ---: | ---: |
| 100 tasks, edit only | 58.542 µs | 46.542 µs | 20.5% |
| 100 tasks, edit + Dart readback | 277.291 µs | 238.917 µs | 13.8% |
| 1,000 tasks, edit only | 388.750 µs | 258.125 µs | 33.6% |
| 1,000 tasks, edit + Dart readback | 2,635.125 µs | 2,381.417 µs | 9.6% |

The 100-task readback round medians were 238.7–241.3 µs, compared with
334.7–474.1 µs in the loaded run. At 1,000 tasks, edit-only round medians were
255.8–262.7 µs, and readback totals were 2,356–2,387 µs. This supports load having
distorted the preceding measurement. The persistent-view baseline remains a
historical comparison, not a contemporaneous interleaved A/B test.

Median round p99s for the direct edit/readback cases respectively: 78/299 µs
at 100 tasks, and 286/3,225 µs at 1,000. Scope remains Dart AOT operations and
optional readback, excluding Flutter rendering, synchronous disk flush and network.

[Rerun CSV](benchmarks/datafriend-live-results-rerun.csv) and
[metadata](benchmarks/datafriend-live-results-rerun-metadata.json).
