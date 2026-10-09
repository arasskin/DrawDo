# Batched sorted-dense maintenance

The sorted-dense candidate now has two maintenance paths in
[`sorted_dense_map.h`](../datafriend-benchmarks/include/datafriend/sorted_dense_map.h):

1. Existing-key value replacements update in place. Structural changes are
   coalesced by packed key until publication, with the last operation winning.
2. Publication sorts only new keys and deleted keys. If removals are a suffix
   and additions follow the surviving last key, it removes from the back and
   appends in ascending order. There is no forced global sort/hash rebuild;
   normal hash-table capacity growth can still rehash.
3. Otherwise it extracts the already sorted dense array, compacts deleted
   entries, merges additions backwards into the same array, then rebuilds the
   hash index once. It moves value references, not boxed payloads. Temporary
   merge storage is proportional to the changed keys, not the full view.

General maintenance is O(n + k log k), rather than a full O(n log n) sort.
It retains O(n) hash-index rebuilding for middle insertions/deletions. Tail-only
batches use ordinary map insert/erase costs plus sorting their changed keys.
The pending hash map retains capacity for reuse, which adds memory overhead.
Sparse pending batches are cleared by erasing their active entries, avoiding an
O(retained capacity) hash-table clear after a large initial construction. Dense
batches still use a bulk clear.

This is an experimental container, not yet the engine's production result map.
Point reads include pending operations; ordered traversal requires `publish()`.
An owning transaction must handle allocation failure/recovery, just as the
engine does for its other mutable indexes.

## Validation

A randomized model test compares 10,000 batches against `std::map`, checking
pending reads, exact final values, key ordering and the hash index. Deterministic
cases cover extreme keys, append/suffix fast paths, canceled structural edits,
middle edits, clearing and release of shared boxed-value references. Both normal
and UBSan runs pass. All five benchmark adapters also pass UBSan checks at small,
medium and large sizes with matching checksums.

## Phone comparison

The previous full-sort candidate remains in the same executable as the optimized
candidate, flat/nested hash maps and the slab-allocated mutable wide tree. The
[previous comparison's scope](datafriend-android-views.md) still applies: identical
borrowed box pointers, native container work only; no query, database, FFI or Dart
work. Existing ownership behavior is exercised separately by the model test.

In addition to the previous read, value-update and entity-replacement workloads:

- `append_remove_tail` appends one new highest-ID entity and then removes it,
  with **two publications per measured cycle**. It isolates both fast paths and
  returns to the initial contents each time.
- `replace_batch16` removes and adds up to sixteen entities in one publication.
  The one-entity shape uses a batch of one. Alternating batches restore the
  original contents; exact checks run after every workload.

The normal-scheduling run completed all 75 processes / 63,000 timed samples.
Five shapes × five layouts × three interleaved rounds; 100 samples after 20
warmups per metric, except construction at 40 samples. Small operations use inner
batches to amortize clock overhead. Values below are pooled medians in **µs**.

### 1,000 entities × one attribute (order-only list)

| Operation | Original full sort | Batched merge | Wide tree + slabs |
| --- | ---: | ---: | ---: |
| Replace one entity (one publication) | 52.47 | 36.15 | 0.781 |
| Append + delete tail (two publications) | 64.17 | 0.990 | 0.730 |
| Replace 16 entities (one publication) | 55.99 | 41.48 | 8.15 |
| Grouped traversal of the full view | 3.80 | 3.65 | 10.99 |
| Read all fields using cached IDs | 49.43 | 56.15 | 122.81 |
| Replace one existing value | 0.077 | 0.071 | 0.127 |
| Build and destroy the view | 167.99 | 264.01 | 405.29 |

### 1,000 entities × five attributes

| Operation | Original full sort | Batched merge | Wide tree + slabs |
| --- | ---: | ---: | ---: |
| Replace one entity (one publication) | 331.48 | 216.67 | 3.18 |
| Append + delete tail (two publications) | 394.53 | 2.92 | 2.76 |
| Replace 16 entities (one publication) | 330.00 | 241.56 | 51.04 |
| Grouped traversal of the full view | 12.60 | 12.60 | 32.66 |
| Read all fields using cached IDs | 115.31 | 122.71 | 495.55 |
| Replace one existing value | 0.077 | 0.071 | 0.169 |
| Build and destroy the view | 888.75 | 1441.95 | 2174.82 |

The append/suffix cycle improves about **135×**, while general replacement
improves **35%**. Middle edits still scan/move entries and rebuild the hash index;
they remain much slower than the wide tree's structural edits. Grouped traversal
remains about 12.6 µs, since the published dense array has the same representation.

### 1,000 entities × 32 attributes

| Operation | Original full sort | Batched merge | Wide tree + slabs |
| --- | ---: | ---: | ---: |
| Replace one entity (one publication) | 2253.12 | 1327.61 | 25.99 |
| Append + delete tail (two publications) | 2409.90 | 13.44 | 24.48 |
| Replace 16 entities (one publication) | 2556.75 | 1609.56 | 420.60 |
| Grouped traversal of the full view | 78.57 | 82.14 | 584.43 |
| Read all fields using cached IDs | 838.49 | 894.51 | 3785.13 |
| Replace one existing value | 0.077 | 0.071 | 0.179 |
| Build and destroy the view | 6916.64 | 12562.55 | 13670.50 |

General replacement drops from 2.25 ms to 1.33 ms (**41%**). The append/suffix
cycle drops from 2.41 ms to 13.4 µs. These tail measurements are warmed, steady-size
cycles with retained capacity; occasional hash-table growth is not eliminated.

### Tiny views and overhead

For a one-entity/two-attribute view, replacing that entity grows from **0.61 to
2.23 µs**. The append/delete cycle grows from 0.86 to 2.34 µs. Extra pending-change
bookkeeping and temporary small allocations cost more than sorting two entries.
Value-only edits remain about 0.1 µs. This experiment does not add a tiny-view
special case, and does not claim every edit improves.

The generic `get` also checks pending changes. That adds work compared with the
original direct hash lookup. Android scheduling makes the normal-run lookup
comparison noisy; the CPU-0 control below provides another measurement. A future
integration can distinguish published UI reads from in-transaction pending reads,
but that optimization is not included in these results.

### Device conditions

Normal run: charging at 62–64%, battery temperature 33.9–34.0 C, thermal status
normal (`0`) before/after every process, one-minute load 3.28–3.88. The Mac's load
spiked during orchestration; timings use the phone's native clock inside the
operation and exclude build, ADB and telemetry delays. Normal Android frequency
scaling remains active. The phone is a slower target, not a strict worst-case bound.

### Reproduce

```sh
python3 datafriend-benchmarks/benchmarks/run_android_views.py --serial ZY227364TP \
  --output-prefix datafriend-android-sorted-merge
python3 datafriend-benchmarks/benchmarks/run_android_views.py --serial ZY227364TP \
  --cpu 0 --output-prefix datafriend-android-sorted-merge-pinned
```

[Normal summary](benchmarks/datafriend-android-sorted-merge.csv),
[normal samples](benchmarks/datafriend-android-sorted-merge-samples.csv),
[normal metadata/hashes](benchmarks/datafriend-android-sorted-merge-metadata.json).

## CPU-0 control and final checks

The second matrix restricts only the benchmark process to CPU 0, leaving Android
frequency scaling and device settings unchanged. All 75 processes / 63,000
samples pass. For 1,000 entities × five attributes, medians in µs:

| Operation | Original full sort | Batched merge | Wide tree + slabs |
| --- | ---: | ---: | ---: |
| Replace one entity | 528.72 | 317.14 | 6.20 |
| Append + delete tail (two publications) | 582.14 | 5.31 | 4.84 |
| Replace 16 entities | 493.65 | 358.67 | 97.99 |
| Grouped traversal | 19.38 | 19.38 | 53.18 |
| Read all fields using cached IDs | 222.50 | 249.58 | 746.62 |
| Build and destroy view | 1333.72 | 2284.97 | 3009.24 |

The append/suffix advantage and roughly one-third-to-two-fifths improvement for
middle edits hold under this control. Initial construction is slower because it
builds the pending-change table as well as the published map. The pending-aware
lookup is about 12% slower in this case; contiguous traversal is unchanged.
These are explicit tradeoffs of this prototype, not measured end-to-end UI gains.

Pinned conditions: charging at 65–67%, battery temperature 34.0–34.1 C, thermal
status normal throughout, one-minute load 3.62–3.94. Across both retained matrices,
**150 processes / 126,000 timing samples** pass. All layouts agree on checksums
for every shape; recorded source hashes match the final implementation.

[Pinned summary](benchmarks/datafriend-android-sorted-merge-pinned.csv),
[pinned samples](benchmarks/datafriend-android-sorted-merge-pinned-samples.csv),
[pinned metadata/hashes](benchmarks/datafriend-android-sorted-merge-pinned-metadata.json).


