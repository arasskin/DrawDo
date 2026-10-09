# Selective row caching

Task and sticker models now survive edits to unrelated rows in the same query.
Each `_Rows` adapter owns a cache keyed by namespace-local entity ID. A query
notification evicts changed entities; subsequent lazy reads reconstruct only
those models. Native ordering indexes survive edits that do not affect result
membership or ordering attributes.

For a completed task in a 1,000-task result, the next full read now builds one
Task and decodes three row fields. The toggle itself reads one additional boolean.
The other 999 model objects are reused, and no ordering index is rebuilt.

## Notification contract

The blocking edit still returns changed subscription IDs. Existing `onChanged`
callbacks are unchanged. An adapter that needs details reads `view.changes`
inside its callback; the native result handle is acquired lazily and retained for
these reads. The additive `df_result_changes` ABI returns:

- Changed **result** entity IDs.
- Changed **projected** attribute names (a union for the query).
- A membership-change flag, and a reset flag for initial/recovered state.

The evaluator collects these sets at actual result writes and removals. It does
not infer them from the original input edits: changing an owner can affect the
projected tasks in a joined query. Identical subscriptions share one native
summary, and each callback can read it without consuming it. Dart summaries own
their metadata and can be retained, but they must be obtained synchronously in
the callback; native storage only retains the latest evaluation's summary.

Initial subscription/recovery evaluation does not collect a large fine-grained
delta; it marks the result for reset. On any failed edit/import, Dart
conservatively sends reset notifications to all subscriptions before rethrowing,
including failures before native maps were changed. This also handles errors
that prevent delivery of the normal edit reply. Poisoned native storage remains
unavailable until reopen.

This uses no generation counter, result read locks, polling, or additional
notification channel. The synchronous interface still serializes access.

## Cache behavior and limits

- Changes to row values evict the corresponding model. A whole row is decoded
  again on demand; there is no per-field cache.
- Membership changes and writes/removals to `order`/`part` discard the ordering
  index. All other edits retain it. Attributes used only as query predicates do
  not invalidate ordering unless the resulting membership actually changes.
- Additions/deletions/imports invalidate the corresponding entity. Invalidations
  accumulate across multiple edits before the next read.
- List wrappers remain lazy and cache accessed models by index. A new wrapper
  uses the surviving entity cache; previously accessed immutable models remain
  valid values. Old unread list entries are not historical database snapshots.
- Stroke models combine multiple chunk entities; they retain the existing
  conservative grouping/model rebuild on stroke-query notifications. Task and
  sticker changes do not invalidate that separate query.
- Full exports still traverse every requested row and construct a list. The
  completed-task header still uses a native count scan.

## Validation

All 23 Flutter tests pass with profiling on and off; analysis is clean. Tests
check model identity across a 1,000-task edit, exact read/model/order counters,
multiple edits without reads, reordering, deletion/reinsertion through import,
shared joined-query summaries, no-op edits and failure resets.

Native randomized differential checks compare change entities, projected
attributes and membership against before/after result maps for 12 query shapes,
including cross-entity joins, predicates and imports. Recovery requests resets
for every result, and the additive C ABI has an encoding/lifetime check.

Linux AddressSanitizer, UndefinedBehaviorSanitizer and leak checks pass (231.5 s).

## Benchmark results

Microseconds, median of three interleaved round medians. The baseline already
contains the direct decoder optimization. Both Dart AOT executables use the same
new native library, so both pay native change tracking; only the current adapter
reads summaries and selectively invalidates its Dart cache. This isolates the
adapter benefit rather than comparing two complete native engine versions.

| Workload | 100 tasks before | After | 1,000 tasks before | After |
|---|---:|---:|---:|---:|
| App ordering/list setup after edit | 1.791 | 0.084 | 11.875 | 0.084 |
| App task read after edit, edit excluded | 25.084 | 7.125 | 212.042 | 56.959 |
| Document export/read after edit, edit excluded | 25.208 | 7.625 | 217.750 | 60.292 |
| 15 visible rows + count, edit excluded | 6.292 | 2.375 | 22.167 | 7.542 |
| Complete edit only | 13.334 | 15.791 | 15.708 | 17.250 |
| Complete edit + document read | 39.458 | 21.709 | 233.167 | 76.750 |
| Complete edit + 15 rows + count | 19.958 | 16.166 | 38.000 | 24.125 |

At 1,000 tasks, complete edit/full read is **3.0× faster** (67% less time), and
complete edit/visible read takes **37% less time**. Median round p99 improves
from **286 to 103 µs** for edit/full read and **47 to 32 µs** for edit/visible.
Edit-only calls cost about **1.5 µs more**, reflecting summary retrieval and cache
invalidation. At 100 tasks that overhead is about 2.5 µs. The controlled field
decoder/probe workloads remain approximately unchanged.

These are warm-cache edits: the fixture reads all models before the timed loop,
then repeatedly toggles one task. No cold-start improvement is claimed. A visible
UI only caches rows it has actually requested. Full reads still traverse 1,000
IDs and fill the new lazy list's index cache even though the models are reused;
the ~60 µs export is not just the cost of decoding one row.

All **352 processes pass** (88 small odd/even validations, 264 timed processes).
Each timed process has 1,000 warmups and 3,000 samples across 22 cases, two sizes
and three rounds. Old/new order alternates; runs are sequential and began after
sanitizer checks. Checksums, final values/order and notification counts are
verified outside timing. The original harness is unchanged.

```sh
python3 datafriend-benchmarks/benchmarks/run_read_path.py \
  --dart /Users/alex/developer/flutter/bin/cache/dart-sdk/bin/dart \
  --baseline-executable datafriend-benchmarks/build/dart-read-path-before-cache \
  --baseline-metadata docs/benchmarks/datafriend-decoder-metadata.json \
  --output-prefix datafriend-row-cache
```

The verified baseline is a preserved local build artifact. The new Dart adapter
requires a native library exporting `df_result_changes`; older Dart binaries can
still use that library. Raw [CSV](benchmarks/datafriend-row-cache.csv) and
[metadata](benchmarks/datafriend-row-cache-metadata.json) contain distributions,
commands, load averages, source/binary hashes and the baseline's provenance.
Host macOS arm64, task-only fixture, no rendering/network/fsync. Edits still use
real transactions with `durable:false`. Physical-device frame timing remains
integration work.
