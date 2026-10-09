# Mutable wide tree versus current LittleBear tree

Measured on 2026-09-26, macOS 26.5.1 arm64, Apple Clang 17, `-O3 -DNDEBUG`.
Three seeds per case, 216 serial benchmark processes, 1,000 / 100,000 / 1,000,000
records. The backend implementation was included unchanged. The new code is an
isolated [mutable prototype](../datafriend/README.md), not a backend migration.

The results support further work on the wide tree: it uses less index memory
and improves large random-key lookups, replacements, and range queries. It is
not uniformly faster, and count-based reconciliation is not always smaller on
the wire.

Follow-up: [Negentropy B-tree and many-namespace comparison](datafriend-btree-benchmark.md).
The measurements below remain the original run; the follow-up has its own
source hashes and results archive.

## Tree measurements

Medians for **one million uniformly distributed keys**:

| Metric | Existing binary tree | Mutable wide tree |
| --- | ---: | ---: |
| Requested/index-slot bytes per record | 48.0 | 30.0 |
| Process RSS increase during build | 46.27 MiB | 32.50 MiB |
| Lookup hit | 245.4 ns | 157.6 ns |
| Replace existing record | 343.4 ns | 181.0 ns |
| Range XOR | 658.5 ns | 464.7 ns |
| Insert | 395.4 ns | 300.7 ns |
| Delete during half-table churn | 564.0 ns | 499.7 ns |
| Reinsert after deletion | 618.8 ns | 458.7 ns |

The binary baseline has N leaves and N-1 internal nodes. Its 16-byte nodes plus
8-byte fingerprint entries approach 48 bytes per record, even with perfect slot
reuse. Its high-water accounting includes reusable free gaps. The wide result
includes cached counts, fingerprints, headers, and unused node capacity, but
excludes malloc bookkeeping/rounding. RSS includes those effects and other
process/page effects; it is a noisy corroborating measurement, not exact
ownership accounting. Payload storage is excluded from both.

The current allocator retains an advantage on **ordered insertion**. At one
million sequential keys, insertion took 42.3 ns for the current tree and
104.1 ns for the wide tree. The wide tree still won lookup and replacement in
that case. At 1,000 records, wide-tree range XOR queries were slower across all
three key distributions. Keep these cases in future comparisons.

The wide prototype uses ordinary malloc/free with capacity classes and shrink
hysteresis. It has no custom pool. It also uses key-derived ranks; the existing
tree uses the initial record fingerprint for rank and keeps topology during
replacement. These are measurements of the two actual implementations, not a
fan-out-only experiment.

## Reconciliation measurements

A custom range protocol encodes and decodes actual messages between two local
indexes. Both sides refine ranges on responses; the client always has counts.
Server strategies are cached wide-tree counts, current binary-tree separators,
and an enumeration-based balanced-split control. All use the same codec, fan-out
target of 16, 32-entry list threshold, and 64 KiB packet limit. The control's wire
bytes/packets/turns exactly match the wide tree in every measured case.

For **one million records with scattered changes** (1% of existing records
changed, half replacements/half deletions; another 0.5% added):

| Server strategy | Reconciliation CPU | Sequential turns | Encoded bytes |
| --- | ---: | ---: | ---: |
| Binary tree, separators | 165.13 ms | 10 | 8,966.4 KiB |
| Binary tree, scan for balanced splits | 232.57 ms | 8 | 8,710.6 KiB |
| Wide tree, cached counts | 83.85 ms | 8 | 8,710.6 KiB |

The wide tree reduces both local work and sequential exchanges here. A turn is
one direction's batch, potentially containing multiple packets; the terminal
empty message is included. These are not measured socket round trips.

The bandwidth result changes with dataset size. At **100,000 records** with the
same scattered-change proportions:

| Server strategy | Reconciliation CPU | Sequential turns | Encoded bytes |
| --- | ---: | ---: | ---: |
| Binary tree, separators | 10.50 ms | 10 | 836.6 KiB |
| Wide tree, cached counts | 6.03 ms | 7 | 1,135.3 KiB |

Balanced splits use fewer turns but about 36% more bytes in this case. The
interaction between fan-out, list threshold, and where differences fall matters.
Actual latency/bandwidth measurements are needed before claiming a network win.

Empty-client, empty-server, identical, clustered-change, and disjoint cases are
also included. Every case checks the exact expected differing keys, applies only
those differences according to client authority, and verifies full record
equality. The benchmarks do not infer success merely from equal XORs.

## Validation and limits

- Normal tests and UndefinedBehaviorSanitizer tests passed. They cover randomized
  mutation against a reference map, ordering, hash/count invariants, rank/select,
  bounded ranges, degenerate hash ranks, insertion-history independence, zero and
  maximum keys, reclamation, binary message parsing, and authoritative convergence.
- AddressSanitizer could not run on this machine: the Apple arm64 runtime
  deadlocked in initialization before main; a sampled stack located the recursive
  initialization lock. An x86_64 attempt exited before test output. ASan validation
  remains outstanding on a working runtime. The stalled process was terminated
  and overlapping timings were discarded before this clean run.
- No network latency, payload transfer, disk I/O, encryption, concurrent mutation,
  or server scheduling is measured. The harness buffers messages and differences
  and is not yet bounded per event-loop turn. Application view/FFI performance
  and the full within-a-frame workflow remain unmeasured.
- The mutable prototype aborts on allocation failure. Production integration
  needs recoverable failure semantics, memory budgets, and server lifecycle tests.
- Three seeds provide an initial comparison, not a rigorous tail-latency study.
  Linux/server-hardware measurements and workloads with many small namespaces
  remain necessary before replacing the existing index.

## Reproduce and inspect

```sh
make -C datafriend-benchmarks test undefined
python3 datafriend-benchmarks/benchmarks/run.py --sizes 1000,100000,1000000
```

- [Full tables](benchmarks/datafriend-2026-09-26/summary.md)
- [Raw measurements](benchmarks/datafriend-2026-09-26/raw.jsonl)
- [Compiler, platform, seeds, and source hashes](benchmarks/datafriend-2026-09-26/metadata.json)
- [Tree implementation](../datafriend/include/datafriend/wide_tree.h)
- [Experimental reconciliation implementation](../datafriend-benchmarks/benchmarks/reconcile.h)
