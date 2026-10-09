# Direct reads from shared query results

The app now reads the existing native result map through FFI. It does not serialize
all facts, recreate `Triple` objects, rebuild Dart entity maps, or rebuild a public-ID
lookup on each notification. Triples remain the edit representation; explicit bulk
exports and the historical adapter remain available for diagnostics/benchmarks.

```dart
final view = database.subscribe(query, onChanged: invalidateUi);
final result = view.result();
final title = result.value(entity, 'title');
final completed = result.countWhere('done', true);
final ids = result.entities(orderBy: 'order');
// ids[index] reads a native-owned uint32 array; fields stay in the shared map.
```

## Ownership and read behavior

`value` resolves attribute names to numeric IDs once and performs a direct lookup
in the shared native nested map. C returns a borrowed pointer to the existing
immutable value bytes. Dart immediately decodes the requested scalar or string;
byte values are copied into Dart-owned memory. Borrowed memory never escapes a
read call, so later edits, rollback recovery and close cannot invalidate a returned
Dart value. `containsField` distinguishes a missing field from a stored null.
`findEntity` and `countWhere` scan native rows without exporting facts.

The existing single-caller contract remains: reads and blocking edits cannot
overlap. No result locks, generation counters, or new notification mechanisms.

`entities` builds a native-owned uint32 ID index. Optional order/secondary fields
are canonical signed int64 values; ties use entity ID. Dart indexes that buffer
without copying it. The index owns its allocation independently of the live map,
with explicit close and a native finalizer. It is only a membership/order index,
not a snapshot of field values. A query notification invalidates the app's index.

The app creates task/sticker objects only for list rows that are actually requested,
then reuses them within that projection. A new notification creates a fresh lazy
projection. Already-read UI objects are ordinary immutable Dart values; retaining an
old lazy list does not provide historical database semantics for unread rows.
Drawing chunk IDs are grouped as metadata; points decode when a stroke is requested.
The task header counts completed rows natively instead of constructing every task.

`AppViews.document` is an explicit full export/debug operation. It still constructs
all requested UI objects. Widgets use the lazy collections; `TaskList` uses its
builder to request visible rows. Reading 15 tasks in a 1,000-task view is tested to
create 15 task objects, decode 45 fields, and reconstruct **zero triples**.

## Controlled reader comparison

The old AOT benchmark executable was preserved before compiling the new reader.
Its SHA-256 matches the executable recorded in the preceding low-load rerun.
Both executables ran against the same current native library, whose existing ABI
remains compatible. The runner interleaved old/new reader cases and reversed order
in the second round. No builds, tests or sanitizer jobs ran during measurement.

Three rounds, 100 warmups and 1,000 measured edits per case; 3 stickers and an
unchanged 5,000-point drawing. All **48 processes** passed correctness and
notification checks, including small odd-parity smoke cases. Numbers are medians
of round medians. Durability is MDB_NOSYNC; network and Flutter build/layout/paint
are excluded.

| Workload | Previous bulk reader | Direct native reader | Reduction |
| --- | ---: | ---: | ---: |
| 100 tasks, edit only | 46.334 µs | 42.708 µs | 8% |
| 100 tasks, edit + full readback | 240.541 µs | 86.667 µs | 64% |
| 1,000 tasks, edit only | 260.042 µs | 253.333 µs | 3% |
| 1,000 tasks, edit + full readback | 2,383.167 µs | 650.458 µs | 73% |

A separate new workload reads 15 visible tasks plus the native completed count:
**49 µs** total at 100 tasks, **282 µs** at 1,000 tasks. Median round p99s are
90/311 µs respectively. Full readback at 1,000 tasks has a median round p99 of
775 µs, versus 3,200 µs with the old reader. Visible-row timing is not presented as
a replacement for the full-read comparison: it deliberately consumes fewer rows.

[Raw measurements](benchmarks/datafriend-direct-fields.csv) and
[commands and source/binary hashes](benchmarks/datafriend-direct-fields-metadata.json).

## Validation

Native tests, all 20 Flutter tests (profiling on/off), Dart analysis, Android arm64
and iOS simulator debug builds pass. Linux engine ASan/UBSan/leak checks pass,
including direct borrowed-buffer identity, ordering/error handling and ownership.
No new blocker; async durability and live network integration remain separate work.
