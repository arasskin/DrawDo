# DrawDo hash table versus ankerl::unordered_dense

`unordered_dense` is faster and smaller for the measured namespace-directory
workloads. At 16,384 entries, using an existing fingerprint gives 3.1× faster
successful lookup, 4.7× faster unsuccessful lookup, and 2.1× faster erase/insert
churn. It uses 47% less table memory after building and 73% less after churn.
Even rehashing the entire key with xxHash, dense wins these operations.

Measured 2026-09-26 on macOS 26.5.1 arm64, Apple Clang 17, C++20,
`-O3 -DNDEBUG`. These are medians of five seeds, from 75 serial fresh processes.
The benchmark compares **DrawDo's then-current littlebear table**, with its current
24-byte wide-tree header. It does not measure the original iCare table.
Backend implementations were not modified by the original benchmark work.
LittleBear now uses the dense map. The former table is retained in
`littlebear-benchmarks/benchmarks/reference/hash_table.h`; the `current` engine name retains
its historical meaning. New runner executions also include an `integrated`
engine exercising the production adapter. Archived results below are unchanged.

## Results

At **16,384 namespace entries**, times in ns (lower is better):

| Operation | DrawDo | Dense, reuse fingerprint | Dense, xxHash key |
|---|---:|---:|---:|
| Insert, including growth | 39.01 | 15.48 | 16.23 |
| Lookup hit | 11.43 | 3.72 | 6.02 |
| Lookup miss | 10.53 | 2.25 | 4.74 |
| Update existing value | 16.53 | 6.16 | 7.09 |
| Erase + insert a distinct key, per pair | 35.31 | 17.00 | 20.31 |
| Erase during final drain | 13.52 | 10.83 | 14.45 |
| Table bytes/entry after build | 96.00 | 51.00 | 51.00 |
| Table bytes/entry after churn | 192.00 | 51.00 | 51.00 |

The same direction holds at 65,536 entries: DrawDo/dense-prehashed take
12.77/4.10 ns for hits, 12.42/2.49 ns for misses and 39.25/18.65 ns per churn
pair. Table storage is approximately 12.00/3.19 MiB, respectively.

DrawDo has some wins: at 64 entries its insertion loop takes 12.38 ns versus
dense's 89.19 ns. Its initial 24 KiB allocation occurs in the constructor,
outside insertion timing; dense allocates lazily during insertion. These short
build/drain measurements are more sensitive to timer and allocator noise than
the million-query lookup measurements. DrawDo also beats the rehashing dense
variant in several final-drain cases.

All five DrawDo runs at 100,000 entries build successfully but encounter capacity
failure during churn, after 56,403–84,935 completed pairs. These are recorded as
failures, with no partial-workload timing reported as a complete result. Both dense variants
complete every tested size. The raw archive records the failure phase and
pair count reached.

## Why memory grows during churn

DrawDo chooses between two buckets using the two halves of each fingerprint.
Each 192-byte bucket has four 16-byte keys and four 24-byte values, plus padding.
An insertion with both buckets full triggers a rebuild into a larger array;
there is no displacement to another bucket or longer probe sequence. A local
collision can therefore trigger growth well below full global occupancy.

At 16,384 entries, median slot occupancy after building is 50%. During churn,
the array doubles even though the workload maintains its entry count. At
65,536 entries, it already occupies the default maximum array of 65,536 buckets
at 25% slot occupancy. It never shrinks during these operations. The nominal
262,144 slots are not a guaranteed usable entry capacity.

The pinned competitor is
[unordered_dense v5.1.0](https://github.com/martinus/unordered_dense/tree/b7cf409efee3a4419191bb3db29054b2ff6dc02d),
which uses groups of sixteen fingerprints, value indices, and a dense value
container. This is the 5.x group implementation, not a measurement of 4.x's
Robin Hood implementation. The default arm64 build enables its NEON path.

## Method and limitations

- Identical, precomputed uniformly random 128-bit keys and 24-byte values for
  each paired seed. Actual tree operations, entity hashing, I/O, and threads
  are excluded. Values use the wide header's `rank` as a synthetic token;
  pointers stay null and are never dereferenced.
- DrawDo already treats namespace keys as fingerprints. `dense_prehashed`
  reuses `low64`, marked avalanching, while equality compares both halves.
  `dense_xxhash` computes `XXH3_64bits` over all 16 bytes inside each operation.
  The prehashed variant assumes uniformly distributed fingerprints; it is not
  a general-purpose hash for arbitrary structured 128-bit keys.
- Default growth policies, no reserve, no load-factor tuning. DrawDo uses
  `unsafe_insert` for keys known absent; dense uses `emplace`, which also checks
  for duplicates. Updates use `associate`/`insert_or_assign`.
- Each lookup/update phase uses at least 1,048,576 random query positions. The
  table is populated and validated before lookup; hit lookup gets a warm-up
  pass. The sequence is build, hits, misses, updates, churn, then drain.
- Churn alternates between two disjoint key sets for eight passes, with each
  pass visiting entries in the same shuffled order. An operation is one erase
  followed by one insert. This is a stationary synthetic trace, not production
  traffic or a mixed read/write application benchmark.
- Memory counts the table object and requested backing-array capacities,
  including unused space. Dense uses `index_bytes()` plus value capacity;
  multiplying its 5.x `bucket_count()` by `sizeof(bucket_type)` would be wrong.
  Allocator overhead, RSS, transient growth peaks and tree-node storage are
  excluded. Both implementations retain their arrays after draining.
- Every successful run checks all stored value fields after building, updating
  and churn, verifies misses and the empty final table, and checks matching
  query checksums across engines. Failed insertion/churn paths verify the
  remaining contents as well. A separate 15-process UBSan run covers all five
  sizes with seed 4, including a churn-capacity failure, without sanitizer
  errors. This is not an ASan result.
- Source hashes are checked before/after compilation and after measurement.
  Processes are serial with shuffled engine order. CPU affinity and other
  applications are not controlled; these results describe this Mac and these
  workloads, not Linux/server performance or individual-operation tail latency.

The results favored dense for this directory workload. The subsequent production
migration preserves the backend's allocation-failure behavior and header lifetime
contract; see the [current table contract](littlebear-components.md#namespace-table-contract).

## Reproduce

```sh
python3 littlebear-benchmarks/benchmarks/run_hash_tables.py
python3 littlebear-benchmarks/benchmarks/run_hash_tables.py --undefined --seeds 4 \
  --output littlebear/build/hash-undefined
```

Requires Python 3 and a C++20 compiler (`CXX` defaults to `clang++`). The pinned
dense headers and license are vendored, so the runner needs no network access.

- [All sizes and timings](benchmarks/drawdo-hash-table-2026-09-26/summary.md)
- [Raw measurements](benchmarks/drawdo-hash-table-2026-09-26/raw.jsonl)
- [Compiler, seeds, version and source hashes](benchmarks/drawdo-hash-table-2026-09-26/metadata.json)
- [UBSan validation records](benchmarks/drawdo-hash-table-2026-09-26/validation/raw.jsonl)
- [Workload](../littlebear-benchmarks/benchmarks/compare_hash_tables.cpp)
- [Runner](../littlebear-benchmarks/benchmarks/run_hash_tables.py)

## Production adapter check

After migration, a three-seed check at 16,384 namespaces compared the retained
baseline, direct dense maps and the production adapter in 12 serial processes
on the same Mac. Median successful lookup was 11.46 ns for the former table and
3.38 ns for the adapter; erase/insert pairs were 34.50 ns and 14.22 ns.
The adapter used 51.01 bytes per namespace after both build and churn, versus
96.00 and 192.00 for the former table. These are component measurements, with
no record I/O or tree operations. The adapter adds recoverable allocation and
storage lifetime handling; no application namespace limit is imposed.

See the [integration comparison](benchmarks/littlebear-dense-integration-2026-09-26/summary.md),
[raw runs](benchmarks/littlebear-dense-integration-2026-09-26/raw.jsonl) and
[source/compiler metadata](benchmarks/littlebear-dense-integration-2026-09-26/metadata.json).
