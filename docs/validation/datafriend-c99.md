# Datafriend C99 rewrite

The store, query evaluator, retained results, reconciliation, TCP driver, disk
flush worker and Apple/Android identity bridges now compile as C99. AELMDB was
already C and remains unchanged. Existing C++ applications use header-only
adapters; the Datafriend library has no C++ runtime dependency.

The persisted codec/schema and LBS5/LBR3 wire formats are unchanged. The port
keeps AEV/AVE indexing, grouped query planning, exact query sharing, fixed-entity
and literal-predicate dependency routing, incremental local-query updates,
precommit publication/recovery, stable read snapshots and atomic staged pulls.

## Implementation boundaries

`engine.c` owns store lifetime and coordinates edits. `query.c` owns index and
query work; `result.c` exposes retained views; `sync.c` owns reconciliation
snapshots and transfers. `network.c` drives nonblocking sockets on the caller.
`flush.c` runs only the coalesced disk-sync syscall on its pthread worker.
`identity.c` owns secret keys and validates write envelopes before signing.

Values use a single allocation containing their reference count, cached hash,
length and bytes. Indexes and results retain the same immutable allocation.
Maps use dense entries and 32-bit bucket positions with backward-shift deletion.
Attribute IDs map to compact schema positions, avoiding holes in per-store
column arrays. The old C++ implementation files have been removed.

The C++ adapter preserves the high-level query/edit interface. Reads now borrow
`std::string_view` instead of requiring an owned `std::string` in every value
box. Its query/edit convenience methods encode the C wire API; native network
operations call the C store directly. App call sites that need an owned string
make that copy explicitly.

## Validation

- Six native suites: direct C99, engine semantics, async flushing, reconciliation,
  identity and slab/index compatibility.
- Direct C test exercises 200,000 map operations, allocation-failure rollback,
  failed query registration, retained handles after close and malformed query
  variables. Test-only allocation injection adds no production allocator cost.
- Five app model/cache suites, plus the shared-document TCP integration client.
- Native TCP integration against Littlebear covers signed empty creation,
  optimistic updates and fallback, subscription publication, multiplexing,
  paged transfers, Unicode, deletion, offline/reconnect and edits during sync.
- Linux release integration uses one CPU and 512 MB RAM. Docker needs
  `--security-opt seccomp=unconfined` for the server's io_uring initialization.
- Android arm64 shared and iOS arm64 static library builds use only C. These are
  build checks; Keychain/Keystore persistence was not exercised on physical
  devices in this run.
- macOS ASan hangs in the runtime initializer before `main` on this host;
  sanitizer validation uses Linux instead. The sanitizer configuration uses
  `-O1`, ASan/UBSan and leak detection. The initial debug run found an empty
  `qsort(NULL, 0, ...)` call in reconciliation preparation and a staging-table
  allocation left behind when publishing the final transfer page. Both are fixed.
  macOS TCP integration also caught unsigned subtraction of a connection start
  timestamp newer than the pump timestamp; the timeout check now guards it.

The CMake library-only configuration (`DATAFRIEND_TESTS=OFF`) needs no C++
compiler. Dynamic dependency inspection shows no libc++/libstdc++ dependency
on macOS, Linux or Android.

## Small performance comparison

Five runs per implementation on the same macOS host, compiled with release
optimization, using the saved pre-port C++ sources as the baseline. Workload:
4,096 entities / 12,288 facts, 64 literal-partitioned local queries, 10,000
two-fact edits in buffered durability mode, then one million borrowed reads.
Both implementations produced the same checksum. These are small local
measurements, not a claim about all workloads or phone performance.

| Median | Original C++ | C99 with C++ adapter |
| --- | ---: | ---: |
| Two-fact edit | 8.64 µs | 8.02 µs |
| Borrowed read | 5.03 ns | 4.11 ns |
| Peak process memory | 16.30 MB | 13.45 MB |

The benchmark source and raw runs are in the ignored
`datafriend-benchmarks/build/c99-comparison/` directory. No benchmark code was
added to the production store repository.
