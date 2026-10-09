# Attribute-level AELMDB storage

The production littlebear in AELMDB now stores one record per fact:

```text
key   = big-endian uint64((uint64(entity) << 32) | attribute)
value = big-endian XXH64(key bytes || encoded value) + encoded value
```

Entity and attribute IDs are namespace-local uint32 and shared-schema uint32,
respectively. The eight-byte fingerprint prefix supplies AELMDB's existing XOR
aggregate at offset zero. Aggregate counts now count facts. The remainder is the
existing typed value encoding; there is no entity envelope or attribute name in
the physical record. A record is at most 65,535 bytes, so the encoded application
value is at most 65,527 bytes. Empty value bytes are supported; deletion means
absence of the key, not an empty or null-valued record.

## Shared attributes

[`schema/attributes.json`](../datafriend/schema/attributes.json) is the single
source of explicit attribute IDs. The generator writes a C++ `attribute` enum and
a Dart `Attribute` enum with `code` and `wireName`. IDs 1–25 cover current app/query
fields; IDs 1001 onward are declared test/benchmark fixtures. There is no dynamic
ID allocation or per-store dictionary. Undeclared writes/queries/import IDs fail.

```sh
python3 datafriend/tool/generate_attributes.py
python3 datafriend/tool/generate_attributes.py --check
```

The app may use enums directly in `Triple` and `QueryClause`:

```dart
database.edit(add: [Triple(entity, Attribute.done, true)]);
final query = DatafriendQuery([
  (QueryClause(entity, Attribute.title, const QueryVariable(0)), true),
]);
```

Existing string spellings are aliases for those fixed definitions, not dynamically
interned attributes. Field reads resolve generated IDs directly in Dart. At open,
Dart checks the native schema fingerprint. Disk metadata also contains the codec
and schema fingerprint. Mismatched schemas and the old disk format are rejected;
there is no migration or older-client compatibility layer. The app and demo now
use fresh `datafriend-v3` directories, preserving old files in their old locations.

## Transaction and publication

Each local edit normalizes its retractions/additions to a batch of packed keys.
Retractions still compare the current value, all retractions precede additions,
and the last addition for a pair wins. The existing AEV index supplies current
values; no entity read, whole-entity decode or whole-entity encoding is required.
No-op facts are removed from the batch.

One AELMDB transaction writes/removes changed facts and advances the entity
allocator if needed. Only those facts update the AEV/AVE indexes. The same
attribute/pair dependency routing and incremental queries update live results;
the transaction commits, then the blocking call delivers Dart notifications.
Unchanged value boxes remain shared. Atomic rollback and index/result recovery
retain their existing behavior.

Disk flushing remains on the existing asynchronous worker. `flush` and checked
close remain explicit durability barriers. No network request is awaited by an
edit. This change prepares the attribute-level network boundary; it does not add
network I/O or stage an entire reconciliation exchange for coherent publication.

`exportFacts` / `importFacts` transfer packed keys and the exact fingerprint-prefixed
value records. One import is one atomic batch, with normal query notifications.
The export-all helper is diagnostic, not yet a range-based reconciliation driver.
Whole-entity export/replacement/deletion remain convenience operations: they
group or prefix-scan facts only when explicitly called. They are not the physical
storage or network unit and are not on ordinary triple-edit paths.

## Verification and benchmarks

Native tests check physical key/value layout and AELMDB count/XOR, independent
stores' IDs, fact imports/removals, max/zero entities, empty values, invalid IDs and
fingerprints, atomic rollback, schema/format rejection and reopen. A 32-field
entity larger than 64 KiB verifies there is no whole-entity transport-size limit
on local edits. Existing query differential, sharing, recovery, map-full and async
flush tests remain active. Dart tests cover enum aliases, fact publication,
partial removal, corrupted imports and whole-entity deletion.

The native comparison alternates preserved entity-record and fact-record libraries
in one C++ driver. Each process seeds 1,000 entities, 12 row queries, an order list
and a completion counter. Shapes: five ordinary task fields, 32 small fields, or
five fields plus an unchanged 32 KiB blob. Workloads toggle completion alone or
change title and completion together. Both retain async flushing; initial seed
flush completes before timing. Three rounds use 500 warmups and 2,000 measured
edits per process, with exact notification IDs and final contents checked.

The UI comparison alternates the preserved/current Release apps using the same
per-row UI workloads: title, completion and order at 100/1,000 tasks, three rounds,
five warmups and 20 measured edits. Rebuild counts, rendered values and exact
reopened contents are checked. Its timing scope and frame-wait limitations are
the same as the [per-row benchmark](datafriend-row-queries.md).

### Native results

All 36 processes / 72,000 measured edits passed. Times below are the median of
three per-process medians, in microseconds; they include the blocking native
edit and owner-thread dispatch, storage, index and query work.

| Entity shape | Edit | Entity records | Fact records | Reduction |
| --- | --- | ---: | ---: | ---: |
| Five task fields | Completion | 20.42 | 17.13 | 16% |
| Five task fields | Title + completion | 18.96 | 18.08 | 5% |
| 32 small fields | Completion | 29.92 | 17.29 | 42% |
| 32 small fields | Title + completion | 32.04 | 18.13 | 43% |
| Task + unchanged 32 KiB blob | Completion | 42.75 | 17.25 | 60% |
| Task + unchanged 32 KiB blob | Title + completion | 43.42 | 18.04 | 58% |

The small-task gain is modest. Avoiding encoding/copying unchanged fields makes
the larger-entity gain much clearer. Updating two fields requires two fact writes,
which offsets more of the saving for small entities. Per-process task completion
medians ranged 18.96–30.50 µs before and 16.08–17.42 µs after; the exact percentage
is not a stable universal speedup. One-minute system load ranged 4.94–5.58.

Storage size is not uniformly better. Seeded AELMDB file extents were 304 → 288 KiB
for ordinary tasks, 1,344 → 1,552 KiB for 32-field entities, and 47.09 → 47.17 MiB
for blob entities. These are allocated file extents, not memory or compacted sizes;
fact-level records introduce per-attribute tree overhead.

Reproduce with the preserved baseline dylib and the current Release dylib:

```sh
python3 datafriend-benchmarks/benchmarks/run_fact_edits.py \
  --baseline-library /path/to/old/libdatafriend.dylib \
  --current-library src/todo_demo/build/macos/Build/Products/Release/datafriend_todo_lab.app/Contents/Frameworks/libdatafriend.dylib
```

[Native results](benchmarks/datafriend-fact-edits-native.csv) and
[source/binary hashes and metadata](benchmarks/datafriend-fact-edits-native-metadata.json).

### Flutter results and load limitation

All 36 Release-app cases / 720 measured edits passed rendered-state and exact
reopen checks. Every title edit rebuilt only one row, completion rebuilt one row
and the counter, and order edits rebuilt the list and its twelve mounted/cached
rows. Neither storage version rebuilt the screen.

The machine became heavily loaded during this comparison: recorded one-minute
load averages ranged **5.95–348.88**, and individual callback stalls reached
44 ms. These mixed timings do **not** establish a Flutter speedup or regression.
They are retained for transparency; the native run above completed beforehand
under much lower load.

| Tasks | Edit | Old callback median (µs) | New callback median (µs) |
| ---: | --- | ---: | ---: |
| 100 | Completion | 221.0 | 248.5 |
| 100 | Title | 219.0 | 225.5 |
| 100 | Order | 273.0 | 260.0 |
| 1,000 | Completion | 263.5 | 279.5 |
| 1,000 | Title | 262.0 | 222.5 |
| 1,000 | Order | 253.0 | 221.0 |

These are pooled medians of 60 samples per case and include Dart edit encoding,
FFI/native work, notifications and immediate callback work; rendering is measured
separately. End-of-frame medians remain 14.25–14.60 ms, dominated by frame
scheduling. A quiet-machine rerun is needed to quantify the full UI effect of a
native saving this small.

```sh
python3 src/todo_demo/tool/run_widget_benchmarks.py \
  --skip-build --samples 20 --uis rows --output-prefix datafriend-fact-storage \
  --baseline-app /path/to/preserved/datafriend_todo_lab.app \
  --baseline-engine-source /path/to/preserved/engine.cpp
```

[UI summary](benchmarks/datafriend-fact-storage.csv),
[raw runs](benchmarks/datafriend-fact-storage-runs), and
[metadata and hashes](benchmarks/datafriend-fact-storage-metadata.json).

Native release suites and UBSan pass, as do 33 app tests and seven demo tests;
app/demo analysis and generated-schema checks are clean. ASan could not run:
the macOS runtime stalled before `main` in reentrant allocator/shadow-memory
initialization. UBSan was run separately and completed without errors. This is a
validation limitation, not a functional-test failure.
