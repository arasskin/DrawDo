# Packed EA keys and boxed values

Implemented and measured on 2026-09-27, macOS arm64, Release C++20.

## Change

Views now belong to one namespace and use the exact key
`(uint64_t(entity_id) << 32) | attribute_id`. Both components are uint32;
attribute IDs and the non-reusing entity counter are persisted. One store directory
owns one namespace. The app and native disk format start afresh at v2.

Values are immutable boxes shared by numeric query indexes and persistent view
leaves. Unchanged facts retain their boxes. Queries compile attribute names once,
borrow indexed values during evaluation, and use borrowed tree lookups, with
pointer equality followed by byte equality. The Dart facade still accepts
attribute names and scalar values; it does not expose native pointers.

The tree retains its variable-width design. Sixteen is expected fanout, not a
capacity limit. Rank mixes the complete packed key with the SplitMix/Stafford
finalizer, then takes groups of four trailing zero bits from its low 32 bits.
Equal ranks share nodes; no secondary tie-break rank was introduced. Subtree
counts and XOR summaries remain, with content fingerprints separate from keys.

See the [native API](datafriend-native.md) for metadata, lifetime and wire details.

## Same-session comparison

The workload is unchanged: 10,000 entities, 100 groups, one completion toggle per
edit. Ten identical subscriptions share one query returning 200 facts. Adding
ten different queries produces 20 handles, 11 query evaluations and 2,200 result
facts. Only one published fact changes, but all eleven queries are invalidated
by the completion attribute.

Each run performs 100 warmups and 1,000 timed edits. Three rounds rotate backend
and before/after order. The saved pre-change executables run with their matching
saved libraries, overriding their original build rpath. The table reports the
median of the three run medians, in **microseconds per complete blocking edit**.
Times include owner-thread dispatch, reconciliation, local transaction/index
work, query execution and view publication. They exclude Dart and rendering.

Both stores are file-backed without synchronous flush: AELMDB `MDB_NOSYNC` and
SQLite WAL with `synchronous=OFF`. OS writes still occur. SQLite retains its
verified indexed EAV and task-table query plans, and receives the **same updated
packed-key/boxed persistent view implementation** as Datafriend.

| Workload | Backend | Before | After | Time reduction |
| --- | --- | ---: | ---: | ---: |
| 1 query / 10 handles | Datafriend | 47.3 µs | **34.1 µs** | **28%** |
| 1 query / 10 handles | SQLite task rows | 55.6 µs | 45.0 µs | 19% |
| 1 query / 10 handles | SQLite EAV | 87.5 µs | 76.8 µs | 12% |
| 11 queries / 20 handles | Datafriend | 350.1 µs | **195.5 µs** | **44%** |
| 11 queries / 20 handles | SQLite task rows | 442.1 µs | 321.5 µs | 27% |
| 11 queries / 20 handles | SQLite EAV | 737.9 µs | 620.7 µs | 16% |

On the eleven-query workload, Datafriend is now **1.64× as fast as the task-table
comparison** and **3.18× as fast as EAV**. Its median run p99 is 231 µs, down from
403 µs. The largest observed Datafriend edit across those three updated runs was
1.22 ms; host scheduling produced visible tail noise. These are not mobile frame
measurements or a guaranteed latency bound.

## Where the gain comes from

The diagnostic builds separate query matching from view maintenance. The earlier
numbers below are the preserved [previous diagnostic](datafriend-view-maintenance.md);
the new numbers come from three fresh rotated rounds. They are useful attribution,
not an exact additive partition of the same-session streaming improvement.

| Stage, across all eleven queries/views | Previous | Packed/boxed |
| --- | ---: | ---: |
| Query matching and collection, buffered | 136.2 µs | 116.8 µs |
| View maintenance, buffered | 201.3 µs | **63.0 µs** |
| Construct/collect EA keys, detailed passes | 86.0 µs | **1.0 µs** |
| Look up entries and compare values, detailed | 50.0 µs | **27.7 µs** |
| Build seen-key sets, detailed | 22.9 µs | 17.5 µs |
| Scan previous membership, detailed | 9.4 µs | 11.6 µs |
| Apply the one actual tree replacement, detailed | 0.42 µs | 0.38 µs |

New query buffers hold numeric identities and borrowed value references; they no
longer copy owned triples. This is reflected by the new `query_match_borrowed_facts`
profile label. Keys no longer require encoding or hashing. Lookup avoids temporary
shared ownership and usually resolves equality through the unchanged box pointer.
The experiment combines these changes and the resulting tree shape; it does not
isolate a standalone rank-mixer or boxing speedup.

Diagnostic ordering still changes performance: the new uninstrumented medians are
198.5 µs streaming, 185.4 µs buffered and 180.8 µs in separate detailed passes.
Production keeps ordinary streaming; diagnostic flags default off. Query matching
and validating result membership now dominate, rather than key construction.

## Validation and reproduction

The 78 paired-comparison processes and 41 diagnostic processes passed expected
result, fingerprint, retained-snapshot and shared-root checks. Uneven groups and
odd toggle counts exercise nonuniform sizes and final-state checks. Profiled edits
also assert exactly one published replacement.

Native engine/tree tests pass in all six normal/profile/buffered/detailed builds.
New coverage checks namespace mismatch, packed bounds, shared box identity,
retention after deletion/engine close, final-owner reclamation, long attribute
names, persisted dictionaries, abandoned/deleted ID reservations and uint32
exhaustion across reopen. All 15 Flutter tests pass; Flutter analysis is clean.
Linux AddressSanitizer, UndefinedBehaviorSanitizer and leak detection pass both
the engine and persistent tree suites. Android arm64 and iOS simulator app
builds succeed with the updated native library and namespace FFI entry point.

Build the Release configurations as documented in the earlier comparison and
view-maintenance reports, then run:

```sh
python3 datafriend-benchmarks/benchmarks/run_sqlite.py --output-prefix datafriend-packed-comparison --baseline-dir /tmp/datafriend-packed-before
python3 datafriend-benchmarks/benchmarks/run_sqlite.py --view-breakdown --output-prefix datafriend-packed-views
```

The baseline directory contains `native/` and `profile/`, each with the saved
pre-change `sqlite_comparison` and `libdatafriend.dylib` (or `.so`). Omit
`--baseline-dir` for a fresh current-only comparison. Historical outputs are not
overwritten by these prefixes.

Raw results: [comparison](benchmarks/datafriend-packed-comparison.csv),
[comparison profiles](benchmarks/datafriend-packed-comparison-profile.csv),
[comparison metadata and binary hashes](benchmarks/datafriend-packed-comparison-metadata.json),
[diagnostic controls](benchmarks/datafriend-packed-views.csv),
[view profiles](benchmarks/datafriend-packed-views-profile.csv),
[diagnostic metadata](benchmarks/datafriend-packed-views-metadata.json).

There are no new implementation blockers. Background disk flushing, live server
transport and physical-device/full-frame validation remain the existing integration
gaps; no async durability or network work is included in these measurements.
