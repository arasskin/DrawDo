# Littlebear component development

The storage API uses namespace-scoped functions and explicit creation/cleanup;
the dense directory uses C++ lifetime management for its backing arrays. The storage index now uses slab-allocated wide nodes; the
old 16-byte binary nodes remain only in reference tests/benchmarks. The tests use standard
containers to check the custom structures independently.

The [module map](../littlebear/README.md) describes production interfaces.
Historical binary-tree/arena sources live under `littlebear-benchmarks/benchmarks/reference`
and their tests under `littlebear/tests/reference`.

## Commands

On Ubuntu 24.04, install `build-essential`, `liburing-dev` and `libsodium-dev`, then run:

```sh
make -C littlebear all test
make -C littlebear sanitize
make -C littlebear-benchmarks benchmark
make -C littlebear-benchmarks benchmark-table
```

`build/server` is the optimized TCP server. `make debug` produces `build/server-debug`;
it does not overwrite the release binary. Header changes trigger rebuilds.
On Linux the storage test also links liburing and exercises real kernel I/O.
`make test CXX=clang++` works on macOS with that kernel-specific portion omitted.
Sanitizer validation is performed on Linux.

Sanitizer checks use a 16 MB quarantine and a 64 KB per-thread quarantine by
default. This keeps test bookkeeping appropriate for the small server while
retaining address/undefined-behavior checks. Override `ASAN_OPTIONS` when needed.
Run compilation sequentially on the droplet and run expensive validation under
a separate resource limit; the deployment workflow runs checks on GitHub runners.

## File-offset allocator contract

- Supported payload sizes are 1 through 65,535 bytes. Zero and larger requests
  fail without changing allocator state.
- Class 0 occupies 32 bytes. Eight subdivisions per doubling give 89 classes,
  ending at a 65,536-byte physical slot. Physical slot sizes use 32 bits; packed
  tree value references retain the original 16-bit payload length.
- Offsets and file bounds are integers. Offset zero is a valid successful
  allocation; callers must inspect `has_error`.
- Release a live allocation exactly once, with its original payload size.
  A failed metadata allocation returns `false` and leaves the allocation live
  so the caller can retry. This primitive does not track double releases.
- Metadata free lists use indexes so growing/reallocating their arena cannot
  invalidate links. Growth failure preserves the old capacity and allocation.
- This allocator manages file offsets. The [storage API](littlebear-storage.md)
  supplies record I/O and reserves release metadata before asynchronous work.

## Current wide-tree contract

`index.h` is the explicit-lifetime storage facade over
the shared mutable tree in `datafriend/include/datafriend/`. It uses direct
child pointers, values only in leaves, and count/XOR augmentation. The 24-byte
namespace header can relocate with table growth or packed deletion. Node
allocation statistics are disabled in the temporary tree view; the shared slab
pool tracks its total reserved/live capacity.

One pool serves all namespaces. Slabs are 64 KiB with the existing roughly 25%
node-capacity classes, and requests above 4 KiB fall back to malloc. Free lists
are stored in freed blocks; there is no per-live-node allocation header.
At most one empty slab per used class is retained, and explicit pool trimming
returns those blocks. Partially occupied slabs cannot be returned until empty.

`unsafe_associate` requires an absent key and reports allocation failure.
`replace` changes a present value and its ancestor XORs without allocation.
`erase` reports removed/missing/no-memory. Insert/delete allocation failures
preserve logical content and shape; any successful capacity growth may remain.
Deletion prepares merge capacity before mutating, and optional shrinking may
be skipped on allocation failure. Size, rank selection, range count/XOR,
bounded iteration and balanced range cuts use cached subtree counts.

Bounds are half-open, with an optional upper bound representing infinity so
`UINT64_MAX` remains usable. The pool, namespace roots, and table retain explicit
lifetime ownership through the [storage API](littlebear-storage.md).

## Legacy binary arena and tree contract

The following describes the retained binary baseline, not the storage index.

The rank arena reserves approximately 64 GiB of virtual address space and the
fingerprint array approximately 32 GiB. These are sparse reservations; touched
pages determine physical consumption. Guard pages are aligned using the actual
system page size, and failed guard setup releases the mapping.

The mappings use `MAP_NORESERVE` where supported, allowing them to initialize on
the droplet's default heuristic overcommit policy without promising backing for
the entire virtual capacity. Strict Linux overcommit mode 2 ignores this flag;
allocation failure is reported rather than changing system policy. See the
[Linux overcommit documentation](https://docs.kernel.org/mm/overcommit-accounting.html).

All 32 partitions are bounded, including the one-slot highest rank. The invalid
index is never allocated. Free slots can be reused after a partition fills.
Large virtual capacity does not enforce the application's eventual resident-memory
budget; admission limits remain part of the storage integration work.

Tree insertion, deletion, lookup, and fingerprint queries use iterative paths.
Insertion computes the split summary before unzipping; deletion carries the
remaining summary while zipping. Both maintain fingerprints without adding
per-node storage, recursion, or temporary heap allocations. Failed insertion
returns any partially allocated nodes and leaves the existing tree unchanged.

`unsafe_associate` requires an absent key; `unsafe_dissociate` requires an
existing key and its matching value hash. `replace` updates a present record and
its path fingerprints without allocation, preserving topology and rank priorities;
an absent key leaves the tree unchanged. Range queries
are exactly `[low, high)`, with empty/reversed ranges returning zero.
`UINT64_MAX` is a valid key, so use `get_full_fingerprint` for the entire set.
The XOR summaries match the [compact reconciliation engine](reconciliation.md);
the wide-tree source adapter uses record keys and stored fingerprints separately.

## Namespace table contract

The directory uses the vendored `ankerl::unordered_dense` v5.1.0 map. Keys are
internal, uniformly distributed 128-bit namespace fingerprints: hashing reuses
`low64`, marked avalanching, and equality compares both halves. All keys are
ordinary entries, including all ones. Public wire identity remains protocol work.

Each dense value is a 16-byte key plus a relocatable 24-byte wide-tree header.
The library maintains a separate group index. `get` returns a pointer to the
stored header or `nullptr`; an empty tree remains a present namespace. Insertion,
reserve and deletion can relocate headers, **including failed growth** when the
value allocation succeeds before the index allocation fails. Always re-look up
by namespace ID after table mutation. Pending storage I/O already does this.

The directory owns its backing arrays; storage owns tree roots and file records.
Storage destroys roots before removing entries or destroying the directory.
Copying the directory is disabled. `associate` replaces a header without
reclaiming its former root; callers must manage that lifetime explicitly.

There is no application namespace-count limit. Creation allocates lazily by
default; an optional initial reserve is a capacity hint, not a ceiling. Growth
continues until allocation fails or the library's index representation is
exhausted. Both report failure through the adapter; `create_namespace` returns
`no_memory`. Existing namespace creation remains idempotent.

Failed insertion/reserve preserves existing contents; successful capacity growth
may remain. Allocation injection covers both values and index groups using the
same callback throughout the directory's lifetime. Hash, equality, and header
moves cannot throw. `memory_usage` counts the directory object, `index_bytes()`
and dense value capacity, excluding allocator overhead, temporary growth arrays,
tree nodes and file storage. Growth is synchronous and remains part of eventual
event-loop latency measurements.

## Verification and measurements

The suite checks every supported request size and its allocation/reuse behavior,
randomized non-overlap, capacity boundaries, metadata growth/failure, every arena
rank, and guard-page protection. Tree tests validate ordering, rank priorities,
lookup results, leaf/node counts, free lists, subtree fingerprints, range results,
and allocation rollback against a reference map. They include 40,000 seeded
random operations and 2,304 four-key insertion/deletion permutation cases.

Namespace-table tests compare 50,000 seeded operations with a reference map and
exercise identical hashes, boundary keys, replacement, deletion, growth beyond
262,144 namespaces, and failures in creation, value growth, index growth and
reserve. A test with two real trees checks mutable stored headers and absent-versus-empty behavior.

On the one-core droplet (458 MiB reported RAM), GCC 13 with `-O3 -DNDEBUG`
produced this sample on 2026-09-26:

| Allocator microbenchmark | Time per operation |
| --- | --- |
| Arithmetic size-class rounding | 4.17 ns |
| Reference binary-search rounding | 37.72 ns |
| Reserve/release, counting each separately | 5.08 ns |

Checksums agree between rounding implementations. The benchmark uses 67,008
bytes of heap metadata for 4,096 reusable slots and peaked at 1,664 KiB resident
memory. The logical file extent is not written or resident value data. These
measurements cover allocator arithmetic and metadata only, not record I/O, tree
throughput, or end-to-end sync; timings will vary on a shared virtual machine.

The following namespace-table results are historical, before the dense migration.
The original eight-slot namespace-table benchmark on the same droplet produced:

| Namespaces | Table memory | Lookup hit | Lookup miss | Insert including growth |
| --- | --- | --- | --- | --- |
| 1,024 | 81,984 bytes | 8.85 ns | 18.48 ns | 107.39 ns |
| 16,384 | 1,310,784 bytes | 38.92 ns | 31.50 ns | 228.40 ns |

Both samples occupied 50% of bucket slots, approximately 80 bytes per namespace.
The process peaked at 3,948 KiB resident memory. Keys are uniformly distributed;
these measurements cover table operations only, excluding trees and record I/O.
Collision-heavy tests check bounded failure behavior separately.

A subsequent comparison used five uniform key sets and three repetitions per
set, alternating execution order. At 16,384 namespaces, median results were:

| Design | Table memory | Lookup hit | Lookup miss | Insert including growth | Delete/reinsert, per operation |
| --- | --- | --- | --- | --- | --- |
| Eight slots | 1.25 MiB | 37.30 ns | 31.59 ns | 206.73 ns | 33.17 ns |
| Four slots (selected) | 1.50–3.00 MiB | 28.07 ns | 23.39 ns | 221.13 ns | 26.24 ns |
| `std::unordered_map` | 0.91 MiB | 44.09 ns | 54.19 ns | 193.35 ns | 114.19 ns |

Four-slot buckets use 96 bytes per namespace at 50% occupancy, versus 80 for
eight slots. One of the five key sets forced the four-slot table to grow again,
to 25% occupancy and 192 bytes per namespace. The then-selected four-slot layout
accepted this space cost for shorter scans, with a 65,536-bucket ceiling. Both
the layout and ceiling have now been replaced by the dense directory. See the
[dense comparison](drawdo-hash-table-benchmark.md).

The standard map used GCC/libstdc++ 13.3, its default allocator and maximum load
factor of 1.0, and a `noexcept` hash XORing the two 64-bit key halves. Its allocation
sizes were counted in a separate untimed pass. All table-memory figures exclude
allocator bookkeeping and fragmentation; standard-map node allocations therefore
have additional overhead. These are component microbenchmarks, not sync timings.

The full Linux sanitizer suite passed under a 160 MB memory cap and 75% CPU
quota, with peak resident memory of 53,760 KiB. An earlier run with the default
large sanitizer quarantine exhausted the droplet and was killed by the kernel;
the deployed service's health check passed after recovery. This motivated the
bounded sanitizer defaults and separate resource limits for on-server validation.
