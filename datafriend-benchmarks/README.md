# datafriend-benchmarks

This is a linked worktree of `../datafriend`, checked out on branch `benchmarks`.
It contains a complete source checkout plus the benchmarks and experimental
implementations removed from `main`. It builds against the sibling benchmark
worktree, leaving the production checkouts independent.

Run benchmark commands here (for example `make -C datafriend-benchmarks benchmark`).
Production changes can be brought over with `git cherry-pick <commit>` for the
relevant fixes. The branch split intentionally removes experiments from `main`;
avoid merging that removal commit into this branch.

Previous generated outputs and comparison baselines are preserved in
`build/archive`. Their old CMake caches contain former absolute paths: build fresh
outputs outside the archive. The archived `benchmark-working-files` directory
also preserves ignored scratch files from the original benchmark folder. The
verified upstream reference sources live at `../datafriend-benchmarks/build/reference`.
No remotes are configured and no changes are pushed.

# Datafriend

## Authenticated networking

Datafriend connects to Littlebear through TCP with native Ed25519 owner signatures,
packed EA facts and AELMDB range reconciliation. Native Datafriend owns key creation,
platform secure storage and signing; Dart holds only a handle and public namespace.
See [the interface, setup and validation](../docs/datafriend-network.md).
Production views remain flat unordered maps; sorted candidates are benchmarks only.

## Phone benchmarks

The [two-table routing experiment](../docs/datafriend-two-table-routing.md)
compares the existing predicate router with materialized entity/attribute
dependencies and predicate match subscriptions. Enable the candidate using
`-DDATAFRIEND_DIRECT_SUBSCRIPTIONS=ON`; it is off by default.
The [batched routing follow-up](../docs/datafriend-batched-routing.md) adds
`-DDATAFRIEND_BATCH_ROUTING=ON` for either router and separately measures routing
discovery and subscription-link maintenance.

Use the connected Android phone for new timing comparisons. The
[many-small-view simulation matrix](../docs/datafriend-simulation.md) measures
the real engine and identifies query fan-out as the next bottleneck. The
[view-layout comparison](../docs/datafriend-android-views.md) includes a
reproducible arm64 runner, raw samples and device conditions.
The [sorted-merge follow-up](../docs/datafriend-sorted-merge.md) measures
append/suffix fast paths and incremental sorted-array maintenance:

```sh
python3 datafriend-benchmarks/benchmarks/run_android_views.py
```

## Current storage format

AELMDB now stores packed entity/attribute facts with shared generated attribute
IDs. See [the v3 format, schema and edit benchmarks](../docs/datafriend-fact-storage.md).
Older tree experiments and measurements remain historical references.

Portable C++ local storage, queries, shared mutable results and a blocking C
ABI, integrated into the Dart app. The shared mutable wide tree/slab allocator
also powers LittleBear. Historical allocator/tree benchmarks remain here.

Disk flushing is now [asynchronous by default](../docs/datafriend-async-flush.md),
coalesced by a separate native worker. Edits still publish consistent query results
before returning. Explicit `flush()` and checked close wait for disk persistence.

See [native design/API](../docs/datafriend-native.md),
[progress](../docs/datafriend-progress.md) and [source notices](NOTICE.md).
Live server transport is still an integration dependency.
The app now [reads fields directly from shared native results](../docs/datafriend-direct-reads.md),
with lazy UI rows and no bulk triple reconstruction.
The [Redux comparison](../docs/datafriend-redux-benchmark.md) measures the same
task edits and UI reads against production Redux Toolkit and handwritten Redux.
Entity-local queries now [reevaluate only changed entities](../docs/datafriend-incremental-queries.md).
The [grouped query planner and broader benchmark](../docs/datafriend-query-planner.md)
apply the [Datalevin research findings](../docs/datafriend-datalevin-notes.md).
The [read-path benchmark](../docs/datafriend-read-path.md) separates ID
iteration, field lookup, Dart value decoding and app model construction.
The [direct value decoder](../docs/datafriend-decoder.md) reduces the complete
1,000-task edit/read from 413 to 234 µs in an interleaved AOT comparison.
Task/sticker projections now [retain unchanged cached rows](../docs/datafriend-row-cache.md)
and native ordering indexes using change summaries from the existing callbacks.
The 1,000-task edit/full read is now 77 µs versus 233 µs with full invalidation.

Build and validate the local engine:

```sh
cmake -S datafriend-benchmarks -B datafriend-benchmarks/build/native -DCMAKE_BUILD_TYPE=Release
cmake --build datafriend-benchmarks/build/native -j 4
ctest --test-dir datafriend-benchmarks/build/native --output-on-failure
cd src/app
flutter analyze
flutter test
flutter build apk --debug --target-platform android-arm64
flutter build ios --simulator --debug
```

The CMake build produces the desktop library used by `datafriend_test.dart`.
`DATAFRIEND_LIBRARY` overrides its path. Android builds the same sources through
CMake; iOS uses the local Datafriend CocoaPod. There are no runtime downloads.

The blocking native/Dart benchmark and its frame-budget limits are documented
[here](../docs/datafriend-engine-benchmark.md). Run it with:

```sh
python3 datafriend-benchmarks/benchmarks/run_engine.py --dart /path/to/dart
```

Query subscriptions now share identical descriptions. The
[sharing benchmark](../docs/datafriend-query-sharing.md) compares ten identical
subscriptions with ten identical plus ten different queries:

```sh
python3 datafriend-benchmarks/benchmarks/run_sharing.py
```

The [packed-key comparison](../docs/datafriend-packed-benchmark.md) measures
namespace-local uint32 entity/attribute IDs and shared boxed values against the
previous design and SQLite. Those measurements are historical; the current
[query results](../docs/datafriend-live-results.md) use ordinary nested dense maps.

The [Dart phase profile](../docs/datafriend-dart-profile.md) measures FFI,
materialization and the real app adapter with profiling-disabled AOT controls:

```sh
python3 datafriend-benchmarks/benchmarks/run_dart_phases.py --dart /path/to/dart
```

Production callbacks now edit views directly. Flutter rebuilds through its
existing ChangeNotifier/ListenableBuilder, with lazy projection reads. See
[publication design and comparison](../docs/datafriend-widget-publication.md).

## Shared mutable tree and historical benchmarks

The wide tree follows ClojureDart's hash-ranked design, with values only in
leaves. Internal nodes contain alternating child pointers and separator keys.
Each leaf entry holds a 64-bit key, a 64-bit value/file-reference token, and a
cached 64-bit fingerprint. Each node has a 24-byte header containing subtree
count, subtree XOR, used slots, and allocated slots. Arrays are contiguous with
their headers. Rank is implicit in depth; the root rank is held by the tree.

Ranks use groups of four trailing zero bits from a deterministic 32-bit portion
of the key hash. Replacement preserves rank and topology. Inserts split paths;
deletions join paths. These are hash-ranked variable-width nodes, not fixed-page
B-tree nodes with minimum occupancy guarantees. Poor rank distribution can
produce very wide nodes; tests exercise that case.

Allocation uses `malloc`/`free`, roughly 25% capacity classes, in-place edits
within capacity, and shrink hysteresis. Freed blocks can be reused by the system
allocator. An optional size-class slab allocator is also available for comparison;
the generic default remains malloc/free, while LittleBear selects the shared slab
pool. Mutations now throw `std::bad_alloc` on allocation failure without changing
records or summaries; the storage facade converts that to its error result and
rolls back the file allocation. Successful capacity growth may remain after a
failed edit. Optional shrink failures retain capacity. The implementation assumes 64-bit
pointers and is single-owner/single-threaded. Its opaque transferable root state
supports LittleBear's relocatable namespace headers; it is not a shared snapshot.

See [NOTICE.md](NOTICE.md) for ClojureDart and Negentropy provenance and licensing.

## Run

From the repository root, with a C++20 compiler, Python 3, `pkg-config`, and
OpenSSL's `libcrypto` development package is needed only for the optional original
Negentropy benchmark. Production reconciliation uses
[`littlebear-benchmarks/src/reconcile.h`](../littlebear-benchmarks/src/reconcile.h) without it.
The original reference is fetched into the ignored build directory on demand:

```sh
make -C datafriend-benchmarks test
make -C datafriend-benchmarks undefined
make -C datafriend-benchmarks sanitize
make -C datafriend-benchmarks benchmark
make -C datafriend-benchmarks benchmark-btrees
make -C datafriend-benchmarks benchmark-allocators
```

`CXX=g++` or `CXX=clang++` can be supplied to `make`. Tests compare against a
reference ordered map, check subtree invariants and allocation reclamation,
exercise rank/select and bounded ranges, and verify history-independent tree
shape. Reconciliation tests encode/decode messages, check exact differences,
apply the authoritative client's state, and verify full record equality.

The Apple arm64 ASan runtime on the development machine (macOS 26.5.1) deadlocked
during shadow-memory initialization **before main**. A process sample confirmed
the recursive sanitizer initialization lock. An x86_64 ASan attempt also exited
before test output. Normal and standalone UBSan tests passed locally. During
LittleBear integration, the shared tree, allocator and reconciliation suites
also passed ASan/UBSan on Ubuntu 24.04 arm64 with GCC 13. Sanitizer runners have
a 60-second timeout. The original Mac runtime limitation still applies there.

For the full recorded matrix:

```sh
python3 datafriend-benchmarks/benchmarks/run.py --sizes 1000,100000,1000000
```

Each case runs in a fresh process. The runner randomizes case order, executes
serially, records three seeds, and saves raw JSONL, compiler/platform/source
hashes, and a Markdown summary under `datafriend-benchmarks/build/results/`. Source
changes during a run invalidate it. Run measurements without concurrent tests
or other heavy work. Archived results and interpretation live in
[docs/datafriend-tree-benchmark.md](../docs/datafriend-tree-benchmark.md).

The B-tree and many-namespace matrix is separate:

```sh
python3 datafriend-benchmarks/benchmarks/run_btrees.py
```

It uses the same fresh-process/three-seed discipline, saves under
`build/btree-results/`, and verifies equal encoded traffic between the three
count-based variants. OpenSSL initialization happens before measurement.
See [the B-tree comparison](../docs/datafriend-btree-benchmark.md) for its
archived results and workload definitions.

For the allocator comparison:

```sh
python3 datafriend-benchmarks/benchmarks/run_allocators.py
```

This compares `wide` (malloc/free) with `slab` using the **same tree code and
capacity classes**, over five seeds in paired fresh processes. It includes an
allocation-lifetime replay captured from real tree operations, single-map
operations, the four many-map profiles, and sparse reconciliation. The runner
checks matching allocation/copy counts, live node bytes, checksums, and sync
traffic. Results go to `build/allocator-results/`; the archived interpretation
is in [the allocator report](../docs/datafriend-allocator-benchmark.md).

### Slab allocator experiment

`basic_wide_tree<Allocator>` selects allocation at compile time; `wide_tree`
remains the malloc-backed alias. `slab_wide_tree` borrows a shared pool or owns
one for a standalone tree. All map roots using a borrowed pool must be destroyed
before that pool. Tree algorithms, node layout, capacity rounding, growth,
shrink thresholds and rank selection are the same for both policies. The
LittleBear integration subsequently reordered split/join allocations to make
failure recoverable; archived benchmark numbers predate that change.

The pool lazily obtains 64 KiB-aligned 64 KiB slabs. Each slab contains blocks
from one existing capacity class, a bump cursor, a free list, and a live count.
Freed blocks hold their own free-list links. The node's existing capacity gives
the class; pointer alignment locates the owning slab. No live-node allocation
header is added. Slabs and free lists are shared across namespace roots on one
thread, without rank partitioning or locks.

The pool retains at most one empty slab per used class and releases additional
empty slabs. `trim()` releases the retained empty slabs; partially occupied
slabs stay allocated. Unsupported sizes and requests above 4 KiB use malloc/free.
There is no compaction or migration between classes. Statistics count complete
slabs, including headers, free blocks and tails, plus requested fallback bytes.
They exclude system allocator bookkeeping; RSS is measured separately. The pool
returns null on allocation failure; the tree throws before changing logical
contents. A system-allocation callback supports failure injection.

Tests cover byte integrity across randomized allocations, class boundaries,
fallbacks, alignment, reuse, full reclamation, and shared-root lifetime. The
existing randomized tree model tests run with both allocator policies, including
extreme ranks that produce unusually large nodes.

## What is compared

Tree microbenchmarks use identical key/value/hash inputs for the indexes:
uniform, sequential, and clustered keys; bulk insertion; hit/miss lookup;
replacement; range XOR; deleting half the records and reinserting them. Checksums
and full record equality verify the result. Hashing inputs is outside those
timed operations. Logical storage and allocated capacity are distinguished from
noisy process RSS deltas. The old tree's free gaps remain in its high-water slot
accounting, including after churn. Wide-tree allocation/copy counters are also
reported. Neither measurement includes actual entity payload files.

The existing backend headers are included without modifications. Its rank
priorities depend on the fingerprint at insertion, whereas the new tree ranks
by stable key. Replacements preserve each implementation's existing topology.
The comparison is of these actual designs, not a controlled fan-out-only change.

### Negentropy B-tree

The vendored upstream `BTreeCore` is used at revision
`b3687652922785c6f3b6c00684f51c6977965784`, verified against upstream HEAD on
2026-09-26. Its minimum/rebalance/maximum occupancies remain 30/60/80, with an
extra overflow slot. Node IDs resolve through an `unordered_map`, matching
`BTreeMem`. Namespace roots share one node store in the many-map experiment.

- `btree-original` retains 32-byte IDs, the 256-bit accumulator, SHA-256
  fingerprinting, and 3,952-byte nodes. The stable entity key occupies the
  timestamp field. Its synthetic ID encodes the key/value/fingerprint tokens;
  this measures the native storage format, not real entity serialization.
- `btree` uses our 64-bit key/value/fingerprint entries and XOR, giving
  2,632-byte nodes. A build-time generator changes only the core's namespace
  and type include; it preserves the upstream algorithms. Node capacity is
  still 80, not retuned to fill a 4 KiB page.

Both adapters replace records by erasing and inserting, as the upstream API
provides no direct replacement operation. Thus edit costs reflect this API and
adapter, not an inherent limit of B-trees. Neither uses an auxiliary entity map.

### Many namespaces

Four distributions exercise 1,000–20,000 independent entity maps: tiny maps
(0/1/8/32 records), 100-record maps, 1,000-record maps, and a skewed mix
(90% 16, 9% 256, 1% 4,096 records). Keys vary independently across namespaces.
The binary variant shares its original rank-partitioned arena; the B-tree
variants share their node-ID map; the wide tree uses ordinary `malloc`/`free`.
Every variant uses the same namespace directory.

A precomputed, single-threaded trace has 200,000 events, with 90% of traffic
directed to 10% of namespaces: 60% lookups, 25% replacements, 10% range queries,
and 5% erase/reinsert pairs. Empty maps receive misses. Timings include directory
lookup and the common correctness checks/model update. Reported batch percentiles
are averages over batches of 1,000 events, not individual-operation tail latency.
Then 10% of namespaces are removed/recreated and 256 distinct namespaces are
reconciled (equal, edited, empty client, empty server). Every map is checked
against exact record contents; every sync checks differences and convergence.
Sync CPU excludes cloning peers and applying resolved edits.

## Reconciliation experiment

The benchmark includes a small custom Negentropy-style range-reconciliation
implementation. It uses 64-bit summaries, stable keys, and actual encoded
byte packets. It is **not upstream Negentropy wire-compatible**. The experimental
codec uses big-endian integers and XXH64 with seed zero over `(key, value-token)`;
this is benchmark data, not a selected application entity codec.

The client uses the wide tree with counts, except `btree-original`, which needs
a matching native B-tree client. XOR is used except in the native B-tree case,
which truncates its upstream 16-byte fingerprint to the codec's 8-byte field.
This keeps wire widths equal without changing native node/accumulator costs.
Server variants are:

- `wide`: cached counts choose approximately equal-cardinality ranges.
- `binary`: bounded-fanout selection of existing separators, without counts.
- `binary-scan`: enumerate the range to obtain balanced split points. This
  control has exactly the same wire sizes and sequential turns as `wide`; the
  runner checks that property. Its scans intentionally show the cost of
  obtaining counts/ranks without maintaining them.
- `btree` and `btree-original`: upstream cached counts choose the same balanced
  split points as `wide`.

Both peers refine on responses. Target fan-out is 16; ranges of up to 32 entries
use explicit `(key, fingerprint)` lists. Server-only records are handled with
paged lists and exact half-open bounds. Infinity is explicit, preserving
`UINT64_MAX` as a valid key. Frames are packed into packets of at most 64 KiB.
Each direction's batch is one sequential turn, even if it contains many packets.
An empty terminal packet is included in the reported counts.

Scenarios cover equal sets, scattered edits, clustered edits, either peer empty,
and disjoint sets. The edit cases change 1% of existing records (half replacements,
half deletions) and add another 0.5%, giving 1.5% differing keys. No markers or
transaction outbox are used. The client is authoritative when applying results.

Metrics include process CPU time, elapsed reconciliation time, sequential turns,
actual encoded bytes/packets, and application time. Transport is in-process;
there are **no measured network latencies**, record-payload transfers, disk I/O,
encryption, concurrent writes, or server scheduling. Packets and differences are
buffered in memory. Wire pagination does not yet impose a server work budget per
event-loop turn. Thus large bootstrap/disjoint cases are correctness and workload
experiments, not production resource-bound implementations.
