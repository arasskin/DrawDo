# Wide-tree size-class slab allocator

The slab allocator cuts allocation/free replay time roughly in half and makes
map destruction substantially faster. Improvements in complete tree operations
are smaller: insertion and reinsertion usually improve, while mixed reads and
edits have no consistent gain across map distributions. The default allocator
remains malloc/free; the slab version is an optional experiment.

Measured 2026-09-26 on macOS 26.5.1 arm64, Apple Clang 17, C++20,
`-O3 -DNDEBUG`. **130 fresh processes**, five seeds, serial execution. Each case
is paired with its other allocator, with randomized pair order and randomized
allocator order within each pair. No B-tree cases were run in this matrix.

## What changed

The tree now accepts a compile-time allocator policy. Both policies execute the
same tree algorithms with the same 24-byte node header, 8-byte slots, capacity
classes, rank selection, growth and shrink rules. There is no runtime virtual
allocation dispatch. Node copying and shifting are unchanged.

The slab policy uses 64 KiB-aligned, 64 KiB slabs, shared across all map roots
in the many-map cases. Each slab serves one existing capacity class. It has a
bump cursor, a free list and a live count; freed blocks hold free-list links.
The node capacity identifies its class and address alignment identifies its
slab. Each class retains at most one empty slab. Additional empty slabs are
freed; explicit `trim()` releases the retained empty slabs. Requests above
4 KiB, or sizes outside the class table, use malloc/free.

No locks, worker pools, rank partitioning, node-layout changes or production
backend changes are involved. Allocation failure still aborts, as in the
existing prototype.

## Allocator cost in isolation

A recording allocator captures actual allocation sizes and lifetimes while a
100,000-record tree is built, half its records are deleted/reinserted, and the
tree is destroyed. Each policy replays that same tape four times. Replay touches
and checks the first/last bytes of allocations but excludes tree traversal and
node copying. Reported cost is per allocation **or** free, including replay-loop
and marker-check overhead.

| Replay pass | malloc/free, ns/call | Slab, ns/call |
| --- | ---: | ---: |
| First | 23.44 | 8.59 |
| Fourth | 19.65 | 9.01 |

The median paired fourth-pass ratio is **0.456**, with seed ratios from
0.412 to 0.480: about 54% less time. The first pass is not a cold system-heap
measurement: constructing the tape already exercised malloc. Retained empty
slabs remain between replay passes; excess empty slabs are released normally.

## Complete tree operations

For one million uniformly distributed keys:

| Operation | malloc/free, ns | Slab, ns |
| --- | ---: | ---: |
| Insert | 310.58 | 295.85 |
| Lookup | 166.54 | 167.78 |
| Replace | 193.23 | 192.65 |
| Delete during half-table churn | 554.52 | 519.13 |
| Reinsert | 475.43 | 466.32 |

These are modest whole-operation gains despite the much cheaper allocation
path. Lookup and replacement do not allocate nodes. Their small differences
reflect placement/cache effects and measurement variation, not eliminated
allocation work. The experiment does not profile individual CPU costs.

## Many independent maps

The existing single-threaded trace performs 200,000 events: 60% lookups, 25%
replacements, 10% range queries and 5% erase/reinsert pairs, with 90% of traffic
directed to 10% of maps. Empty maps receive misses. All variants use the same
namespace directory and precomputed inputs.

| Distribution | Mixed ns/event, malloc | Mixed ns/event, slab | Remove 10% of roots, malloc/slab ms | Recreate those roots, malloc/slab ms |
| --- | ---: | ---: | ---: | ---: |
| 20,000 maps, cycling 0/1/8/32 records | 55.15 | 55.48 | 0.18 / 0.13 | 1.35 / 1.24 |
| 10,000 maps × 100 records | 110.04 | 112.10 | 0.50 / 0.27 | 8.27 / 7.71 |
| 1,000 maps × 1,000 records | 135.51 | 128.48 | 0.32 / 0.13 | 11.49 / 10.93 |
| 10,000 maps: 90% 16, 9% 256, 1% 4,096 | 71.37 | 74.09 | 0.35 / 0.17 | 9.42 / 8.83 |

Recreation inserts saved records one at a time into new empty trees. The
namespace directory remains intact; no disk loading or network transfer is
included. Median paired recreation ratios are 0.925–0.947 across the profiles.
The 1,000-record maps improved mixed-event time in all five pairs, but the
other profiles did not show a consistent improvement.

After the reconciliation phase, clearing all remaining roots took
5.28 ms with malloc and 1.84 ms with slabs for the 100-record-map profile.
For the 1,000-record-map profile it took 3.79 versus 1.37 ms. Both return all
nodes to their allocator; neither figure promises that all pages leave RSS.

## Memory and reclamation

| Distribution | malloc requested/accounted MiB | Slab accounted MiB | Build RSS delta, malloc/slab MiB |
| --- | ---: | ---: | ---: |
| Tiny | 8.12 | 8.95 | 9.36 / 6.86 |
| 100 records/map | 29.93 | 31.13 | 34.00 / 28.69 |
| 1,000 records/map | 28.75 | 29.74 | 32.77 / 27.28 |
| Skewed | 23.62 | 24.63 | 26.86 / 22.39 |

The slab figures count **entire slabs**, including headers, unused blocks,
tails and retained empty slabs, plus large fallback allocation requests. The
malloc figures count requested live node capacity but cannot include its
internal metadata or retained free blocks. Both include namespace wrappers,
the directory's values/buckets and the shared pool object where applicable.
System allocator bookkeeping and directory allocation overhead remain excluded.

Slabs therefore reserve roughly 0.8–1.2 MiB beyond the malloc node-capacity
accounting in these many-map cases. RSS deltas were lower with slabs, but those
deltas are affected by previous heap reuse, untouched pages and allocator/page
rounding. They are not exact physical ownership totals. In particular, the
slab's full reserved capacity must not be confused with resident touched pages.

Deleting a random 10% of roots released little slab capacity: their free blocks
were spread across slabs containing surviving nodes. Reinsertion reused those
blocks. After all roots were destroyed, the pool retained 1.00–1.44 MiB in empty
slabs; explicit trimming reduced tracked reserved bytes to **zero** in every
case. This does not promise immediate OS reclamation of malloc-backed pages.

Sharing the pool matters. A standalone 1,000-record random-key tree requested
only about 0.03 MiB of live nodes with malloc, while its slab pool reserved
1.38 MiB across the capacity classes it had used. A separate pool per small
entity map would be a poor configuration for this policy.

## Reconciliation and validation

Sparse reconciliation produced identical differences, packets, bytes and turns
for every allocator pair. At one million records CPU was malloc/slab
83.61 / 85.45 ms; applying the resulting edits took 8.54 / 8.04 ms. This is not
evidence of faster reconciliation. The client used malloc-backed wide nodes
in both cases; the server allocator changed. No sockets or disk I/O are measured.

Normal and UndefinedBehaviorSanitizer tests passed. They include randomized
byte-integrity checks across all classes and large fallbacks, alignment,
free-block reuse, bounded empty-slab retention, explicit trimming with live
blocks, shared-root destruction, and reconciliation. The existing randomized
tree model tests also passed with both policies, including extreme hash ranks.
All 130 benchmark cases passed exact-content checks; paired allocation/copy
counts, live node bytes and reconciliation traffic matched where measured.
The previously documented ASan runtime initialization problem on this Mac
remains; no ASan-clean claim is made.

## Reproduce

```sh
make -C datafriend-benchmarks test undefined
python3 datafriend-benchmarks/benchmarks/run_allocators.py
```

- [Full tables](benchmarks/datafriend-allocator-2026-09-26/summary.md)
- [Raw measurements](benchmarks/datafriend-allocator-2026-09-26/raw.jsonl)
- [Compiler, seeds, configuration and source hashes](benchmarks/datafriend-allocator-2026-09-26/metadata.json)
- [Allocator implementation](../datafriend/include/datafriend/tree_allocator.h)
- [Workloads and allocation replay](../datafriend-benchmarks/benchmarks/compare.cpp)
