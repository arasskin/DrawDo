# Littlebear component development

Production code uses plain structs, namespace-scoped functions, and explicit
allocation/cleanup. Tree nodes remain 16 bytes. The reference tests use standard
containers to check the custom structures independently.

## Commands

On Ubuntu 24.04, install `build-essential` and `liburing-dev`, then run:

```sh
make -C littlebear all test
make -C littlebear sanitize
make -C littlebear benchmark
```

`core` is an optimized server build. `make debug` produces `build/core-debug`;
it does not overwrite the release binary. Header changes trigger rebuilds.
Component tests do not require liburing, so `make test CXX=clang++` also works
on macOS. Sanitizer validation is performed on Linux.

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
- This allocator manages file offsets. Record I/O and the storage API remain
  separate work.

## Arena and tree contract

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
existing key and its matching value hash. Replacement is currently delete plus
insert; an atomic replacement API remains storage-layer work. Range queries
are exactly `[low, high)`, with empty/reversed ranges returning zero.
`UINT64_MAX` is a valid key, so use `get_full_fingerprint` for the entire set.
The fingerprints are still the prototype XOR scheme, not upstream negentropy's.

## Verification and measurements

The suite checks every supported request size and its allocation/reuse behavior,
randomized non-overlap, capacity boundaries, metadata growth/failure, every arena
rank, and guard-page protection. Tree tests validate ordering, rank priorities,
lookup results, leaf/node counts, free lists, subtree fingerprints, range results,
and allocation rollback against a reference map. They include 40,000 seeded
random operations and 2,304 four-key insertion/deletion permutation cases.

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

The full Linux sanitizer suite passed under a 160 MB memory cap and 75% CPU
quota, with peak resident memory of 53,760 KiB. An earlier run with the default
large sanitizer quarantine exhausted the droplet and was killed by the kernel;
the deployed service's health check passed after recovery. This motivated the
bounded sanitizer defaults and separate resource limits for on-server validation.
