# Negentropy B-tree and many-namespace comparison

The mutable wide tree remains the strongest candidate in this experiment.
It uses less index memory and has lower mixed-operation time in all four
many-namespace profiles. The existing binary tree remains competitive for tiny
maps and wins namespace rebuilding in these traces. Negentropy's B-tree pays
for fixed-capacity nodes and its storage/API machinery, especially on small maps.

Measured 2026-09-26 on macOS 26.5.1 arm64, Apple Clang 17, C++20,
`-O3 -DNDEBUG`. Results are medians of three seeds, from **192 fresh processes
run serially**. All operations are single-threaded. Backend files were included
unchanged; this is an isolated benchmark, not a backend migration.

## Implementations

The repository already contained Negentropy's
[BTreeCore](https://github.com/hoytech/negentropy/blob/b3687652922785c6f3b6c00684f51c6977965784/cpp/negentropy/storage/btree/core.h)
at `b3687652922785c6f3b6c00684f51c6977965784`, verified against upstream HEAD
on the measurement date. This is Negentropy's own in-memory B-tree design;
it is not an AELMDB disk-storage benchmark.

| Variant | Records and aggregates | Storage |
| --- | --- | --- |
| Binary | 64-bit key/value/hash; subtree XOR; no subtree counts | Existing rank-partitioned arena and separate hash array, shared between namespaces |
| Wide | Same record fields; cached subtree XOR/count | Variable-width nodes using `malloc`/`free`, capacity classes and shrink hysteresis |
| B-tree XOR | Same record fields; cached subtree XOR/count | Upstream algorithms with adapted types; 2,632-byte nodes in a shared node-ID hash table |
| B-tree native | Upstream 32-byte IDs, 256-bit accumulator and SHA-256 fingerprints/counts | Upstream core; 3,952-byte nodes in the same kind of shared node store |

Both B-tree variants retain upstream thresholds of 30/60/80 and an extra
overflow slot. The XOR variant changes the namespace/type include through a
build-time generator, preserving the core algorithms. It is not retuned to
fill 4 KiB pages. The node store follows `BTreeMem`, with one pool and separate
roots for the many-namespace workload.

Native IDs encode the synthetic key/value/hash tokens, with the stable key in
the timestamp field. Both adapters implement replacement using lookup, erase,
then insert: upstream has no direct value-replacement operation. Their measured
edit costs include that choice. These results describe these implementations,
not a general limit on B-trees or a controlled comparison of fan-out alone.

## One million random keys in one map

| Metric | Binary | Wide | B-tree XOR | B-tree native |
| --- | ---: | ---: | ---: | ---: |
| Accounted node bytes/record | 48.0 | 30.0 | 48.4 | 72.6 |
| Build RSS increase, MiB | 46.28 | 32.78 | 57.53 | 71.86 |
| Lookup hit, ns | 265.1 | 171.7 | 334.1 | 372.4 |
| Replace, ns | 363.1 | 191.1 | 790.8 | 856.9 |
| Range fingerprint, ns | 763.8 | 498.4 | 2,468.4 | 3,083.9 |
| Insert, ns | 401.5 | 306.5 | 523.3 | 552.9 |
| Erase during churn, ns | 603.0 | 522.5 | 632.3 | 712.3 |

The wide tree wins these cases. The binary tree still wins ordered insertion:
at one million sequential keys, binary/wide/XOR-B-tree/native-B-tree took
42.3 / 104.2 / 306.2 / 327.4 ns per insertion. Binary also beats wide on
1,000-record range queries. Sequential insertion packs B-tree nodes better:
the XOR version drops to 33.5 accounted bytes/record.

## Many independent entity maps

Each variant uses the same namespace directory and independently generated keys
per map. A precomputed 200,000-event trace directs 90% of traffic to 10% of maps:
60% lookups, 25% replacements, 10% range fingerprints, 5% erase/reinsert pairs.
Empty maps receive misses. Timings include namespace lookup and common result
checks/model updates. Entity hashing and trace generation happen outside timing.

**Mixed-operation time, ns per event:**

| Map distribution | Total records | Binary | Wide | B-tree XOR | B-tree native |
| --- | ---: | ---: | ---: | ---: | ---: |
| 20,000 maps, cycling 0/1/8/32 records | 205,000 | 58.1 | 55.5 | 130.1 | 150.3 |
| 10,000 maps × 100 records | 1,000,000 | 143.0 | 106.4 | 289.4 | 373.3 |
| 1,000 maps × 1,000 records | 1,000,000 | 179.6 | 129.7 | 276.7 | 336.5 |
| 10,000 maps: 90% 16, 9% 256, 1% 4,096 | 784,000 | 97.7 | 71.8 | 158.7 | 193.8 |

**Accounted memory after building, MiB, including namespace wrappers/directory:**

| Distribution | Binary | Wide | B-tree XOR | B-tree native |
| --- | ---: | ---: | ---: | ---: |
| Tiny | 10.72 | 8.13 | 39.18 | 58.07 |
| 100 records/map | 46.39 | 29.93 | 101.71 | 152.06 |
| 1,000 records/map | 45.84 | 28.75 | 48.07 | 71.97 |
| Skewed | 36.50 | 23.63 | 55.77 | 83.25 |

Binary accounting includes reusable high-water slots, not merely live nodes.
Wide accounting includes requested capacity and unused slots. B-tree accounting
includes hash-table values and buckets, but excludes per-allocation bookkeeping;
the common namespace directory has the same limitation. These are capacity
accounting figures, not exact allocator ownership totals. Prototype wrappers
are included; they are not proposed production namespace-header layouts.

RSS corroborates the gap: for 10,000 × 100 records, binary/wide/XOR-B-tree/native
B-tree increased RSS by 46.98 / 33.95 / 126.31 / 157.69 MiB. RSS includes page
and allocator effects and is noisy. Precomputed inputs are allocated before
the baseline. OpenSSL initialization occurs before both timers and RSS baselines.

Small maps amplify the B-tree's fixed allocation: one record already needs a
full node. Splits add full internal nodes too. Its rightmost-node split policy
favors appends; random insertion can leave that rightmost node lightly occupied.
Wide leaves allocate for their current capacity, while binary allocates small
slots. Sharing the B-tree node store removes duplicated hash-table overhead,
but cannot remove unused capacity inside each node.

The trace then destroys and recreates 10% of map roots. For 10,000 × 100
records, deletion took binary/wide/XOR-B-tree/native-B-tree
1.26 / 0.49 / 0.87 / 0.88 ms; recreation took
7.32 / 8.20 / 17.49 / 15.92 ms. The binary arena keeps its reusable gaps;
it does not allocate another arena for each map. Full timings and retained
capacity after churn are in the archive.

## Reconciliation

The shared harness uses actual encoded messages in a custom Negentropy-style
protocol: 16-way balanced splits where counts exist, 32-entry list threshold,
64 KiB packets, 64-bit wire fingerprints. It is not upstream wire format.
Native B-tree fingerprints are truncated from 16 to 8 bytes for this harness;
their computation still uses upstream accumulator/SHA-256. Native servers use
a native B-tree client; the other variants use a wide-tree client. Consequently
native CPU figures include a different client implementation too.

For one million records with scattered edits (0.5% replaced, 0.5% deleted,
another 0.5% added):

| Server | Reconciliation CPU, ms | Sequential turns | Wire KiB |
| --- | ---: | ---: | ---: |
| Binary, separator splits | 170.83 | 10 | 8,966.4 |
| Wide, cached counts | 82.73 | 8 | 8,710.6 |
| B-tree XOR, cached counts | 180.33 | 8 | 8,710.6 |
| B-tree native, cached counts | 322.76 | 8 | 8,710.6 |

The three count-based variants have identical encoded sizes, packet counts and
turn counts for every corresponding case; the runner checks this. Changing the
tree therefore changes CPU cost without changing the range protocol's traffic.
The binary baseline uses the earlier experimental separator policy, not an
accepted production protocol. At 100,000 scattered records it uses fewer bytes
than balanced splitting, but more turns; the prior bandwidth tradeoff remains.

For small-map sync, the difference is much smaller. Across 256 sessions on
100-record maps, total CPU was binary/wide/XOR-B-tree/native-B-tree
3.22 / 2.99 / 3.13 / 4.45 ms. These sessions cycle through identical maps,
three edited keys, an authoritative empty client, and an empty server. The
1,000-record-map sessions took 15.13 / 14.62 / 15.04 / 18.09 ms respectively.
Cloning peers and applying resolved changes are outside reconciliation timing.

## Validation and scope

Normal and UndefinedBehaviorSanitizer tests pass, including randomized mutation,
range fingerprints, count/separator invariants, zero/maximum keys, shared-pool
isolation, reclamation, exact differences and convergence. All 192 benchmark
cases also check exact record contents, rather than accepting matching summaries
as proof. Source hashes stayed unchanged throughout the final run.

The earlier ASan runtime initialization failure on this Mac remains unresolved;
this is not an ASan-clean claim. No sockets, disk I/O, payload files, encryption,
concurrent edits or server scheduling are measured. Reported event-batch
percentiles average 1,000 events; they are not individual-operation tail latency.
These results support the wide-tree direction but do not demonstrate the full
Datafriend/app workflow completing within a frame. Linux/server measurements
and production integration remain separate work.

## Reproduce and inspect

```sh
make -C datafriend-benchmarks test undefined
python3 datafriend-benchmarks/benchmarks/run_btrees.py
```

Requires a C++20 compiler, Python 3, `pkg-config`, and OpenSSL `libcrypto` headers
and library. The runner initializes crypto before measuring the native variant.

- [Full tables](benchmarks/datafriend-btree-2026-09-26/summary.md)
- [Raw measurements](benchmarks/datafriend-btree-2026-09-26/raw.jsonl)
- [Platform, compiler, source hashes and seeds](benchmarks/datafriend-btree-2026-09-26/metadata.json)
- [B-tree adapters](../datafriend-benchmarks/benchmarks/btree_index.h)
- [Workload implementation](../datafriend-benchmarks/benchmarks/compare.cpp)
- [Original binary/wide comparison](datafriend-tree-benchmark.md)
