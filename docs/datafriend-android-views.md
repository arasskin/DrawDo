# View layout comparison on Moto G7

[The follow-up maintenance experiment](datafriend-sorted-merge.md) adds batched
merging and append/suffix fast paths. The current runner includes that fifth
candidate and extra structural-edit workloads; the figures here describe the
original comparison.

Run on the connected Moto G7, Android 10/API 29, arm64, using an NDK 28.2
release executable (`-O3 -DNDEBUG`, static libc++). The phone is the preferred
benchmark target going forward; desktop measurements remain historical controls.
This is a **container-only comparison**, not full query/edit/Flutter latency.

## Representations

- **Nested maps:** the former `entity -> attribute -> value` dense-map layout.
- **Flat hash:** one `unordered_dense` map keyed by packed entity/attribute.
- **Sorted dense:** the same flat map, with its dense array sorted by packed key.
  Insertion/deletion marks it dirty; publishing extracts, sorts and replaces the
  array, rebuilding the hash index. Existing value replacements retain order.
- **Wide tree + slabs:** our mutable wide tree with its size-class slab allocator.
  Slabs are shared across view lifetimes. It retains the existing count/hash
  augmentation, with zero hashes because these views do not reconcile. No
  `std::map`, third-party B-tree, or persistent tree substitutes for it.

All representations store the same pointers to fixed-address immutable boxes,
owned by the fixture. This isolates container work. Shared-reference lifetime
accounting, value encoding/decoding, query evaluation, owner-thread dispatch,
AELMDB and FFI/Dart are excluded. A wide-tree production view would also need
its value ownership integrated; this benchmark does not claim that is done.

The wide tree gained an allocation-free `for_each` traversal. Its visitor walks
leaf records in key order, without allocating a temporary record array. Existing
randomized tests now compare that traversal with the tree's ordered list/model.

The comparison adapters also pass UBSan checks for small, medium and large views.

## Results

All values below are **microseconds**, pooled medians of three independent
processes (100 timed samples each; 40 per process for construction). Read rows
visit the **entire view**, not one task. Tiny operations use inner batches and
report per-operation time. There are 20 warmup samples for each metric.

### 1,000 entities, five attributes each (5,000 facts)

| Operation | Nested maps | Flat hash | Sorted dense | Wide tree + slabs |
| --- | ---: | ---: | ---: | ---: |
| Read all fields using cached entity IDs | 244.74 | 234.43 | 221.22 | 498.75 |
| Build ascending entity-ID array | 50.26 | 287.60 | 17.19 | 35.99 |
| Visit entities and all their attributes | 115.05 | 414.01 | 12.60 | 32.45 |
| Build UI order and read all fields | 417.92 | 474.97 | 249.06 | 895.44 |
| Replace one existing value | 0.120 | 0.074 | 0.077 | 0.165 |
| Remove one entity + add another | 1.72 | 0.781 | 331.72 | 3.28 |
| Build and destroy the entire view | 1648.36 | 463.80 | 894.45 | 2150.86 |

Flat hashing gives only a small cached-ID field-read improvement in this run;
generating its entity list costs more: it must deduplicate entity IDs across all facts. The
ordered layouts avoid that deduplication. Sorted dense storage gives the fastest
sequential entity/attribute walk, while rebuilding it after structural edits costs
about 0.33 ms. The wide tree trades slower field probes for ordered traversal and
much cheaper structural changes than sorted dense storage.

### 1,000 entities, one attribute each (order-only list)

| Operation | Nested maps | Flat hash | Sorted dense | Wide tree + slabs |
| --- | ---: | ---: | ---: | ---: |
| Read all fields using cached entity IDs | 123.78 | 39.12 | 49.58 | 123.07 |
| Build ascending entity-ID array | 55.21 | 132.19 | 12.19 | 17.50 |
| Visit entities and all their attributes | 87.81 | 152.81 | 4.84 | 6.93 |
| Build UI order and read all fields | 232.29 | 206.20 | 179.56 | 304.27 |
| Replace one existing value | 0.120 | 0.074 | 0.077 | 0.124 |
| Remove one entity + add another | 0.833 | 0.416 | 52.50 | 0.782 |
| Build and destroy the entire view | 905.62 | 90.99 | 168.93 | 405.96 |

### One entity, two attributes (individual row query)

| Operation | Nested maps | Flat hash | Sorted dense | Wide tree + slabs |
| --- | ---: | ---: | ---: | ---: |
| Read all fields using cached entity IDs | 0.142 | 0.085 | 0.112 | 0.098 |
| Build ascending entity-ID array | 0.246 | 1.10 | 0.318 | 0.353 |
| Visit entities and all their attributes | 0.302 | 0.588 | 0.031 | 0.052 |
| Build UI order and read all fields | 0.370 | 0.743 | 0.626 | 0.620 |
| Replace one existing value | 0.120 | 0.074 | 0.112 | 0.091 |
| Remove one entity + add another | 0.831 | 0.258 | 0.612 | 0.651 |
| Build and destroy the entire view | 1.15 | 0.781 | 1.41 | 0.704 |

The per-row views in the demo are tiny: all existing-value replacements are well
below a microsecond at the container level. Large-view differences should not be
multiplied by the number of UI rows; each row owns its own small query result.

### 1,000 entities, 32 attributes each

| Operation | Nested maps | Flat hash | Sorted dense | Wide tree + slabs |
| --- | ---: | ---: | ---: | ---: |
| Read all fields using cached entity IDs | 1546.67 | 990.75 | 840.83 | 3843.41 |
| Build ascending entity-ID array | 50.10 | 1346.74 | 100.57 | 397.71 |
| Visit entities and all their attributes | 241.09 | 2416.46 | 79.58 | 578.52 |
| Build UI order and read all fields | 1729.89 | 2617.19 | 1346.62 | 4992.42 |
| Replace one existing value | 0.120 | 0.074 | 0.077 | 0.177 |
| Remove one entity + add another | 5.99 | 3.28 | 2266.80 | 24.32 |
| Build and destroy the entire view | 4678.55 | 4268.36 | 6874.32 | 13580.65 |

Sorted dense membership maintenance reaches about 2.26 ms here. The existing
augmented wide tree is also slower than the nested maps for a full grouped scan
at this size. An ordered representation is not a universal improvement.

## Workload and correctness

Five view shapes: 1×2, 100×5, 1,000×1, 1,000×5 and 1,000×32. All shapes run
against four layouts in three rounds: **60 processes and 38,400 timed samples**.
Each shape/layout starts in a fresh process; shape and layout order are shuffled
with a fixed seed. Entity insertion order is shuffled identically. UI sort values
are deterministic mixed entity IDs, so UI order differs from packed-key order.

- Cached-ID reads look up every requested field individually, as the current
  field API does. The cached array already exists outside the timer.
- Entity enumeration constructs a unique ascending entity-ID array.
- Grouped scans visit ascending entities and consume every field. Nested maps
  sort their outer entity IDs and walk each inner map. Flat hashing enumerates
  entities and probes the known projected attribute IDs. Sorted dense and wide
  layouts consume consecutive entity groups directly. Field order within a group
  is unspecified, matching entity attribute iteration semantics.
- UI order sorts entities by attribute 1, then reads all fields. An EA-ordered
  tree does not eliminate this separate value-based ordering step.
- Value replacement updates one existing key and publishes, then verifies lookup.
- Structural churn removes all attributes of one entity and adds another in one
  batch, then publishes. The view remains the same size. This includes sorted
  dense reordering and hash rebuilding. It is deliberately different from a
  value-only edit.
- Construction includes insertion, publication and destruction; the wide tree
  reuses its shared slab pool across view lifetimes.

Every process checks point lookup, exact fact count/checksum, unique sorted entity
IDs and entity contents after churn. Checksums agree across all four layouts for
each shape. Randomized tree tests and native engine suites pass. The flat-result
prototype also passes 19 targeted Dart/app tests and all seven demo tests on the
host; these are correctness checks, not phone Flutter measurements.

## Device conditions and limits

Battery was 26%, charging; battery temperature was 32.7–33.0 C. Reported thermal
status was normal (`0`) before and after all 60 processes, and battery saver was
off. One-minute load ranged 3.05–3.54 on the eight-core phone. For the tables above, CPU affinity and frequency were left under Android's
normal control. A separate single-core control is reported below. This is more controlled than
the heavily loaded desktop run, but not a fixed-frequency laboratory result or a
guaranteed worst-case bound. Small timings and some field-read passes vary across
rounds; raw samples and tails are retained.

Sorted dense grouped traversal requires a corresponding native cursor/group API
to realize that benefit in widgets. Keeping the current per-field FFI API instead
uses the lookup timing. UI entity ordering is already cached until membership or
sort fields change, so the enumeration/sort work is not paid on every field edit.

The raw stderr includes container capacity/slab reservation diagnostics. Those
exclude boxes, use an isolated shared slab pool, and include spare slabs; they
must not be treated as a per-view resident-memory comparison.

## Single-core control

A second complete matrix restricts only the benchmark process to logical **CPU 0**
using `taskset`. No governor, frequency, battery or other device setting changes.
All 60 processes / 38,400 samples pass, bringing the retained comparisons to
**120 processes / 76,800 timed samples**. Conditions: charging at 28–30%, battery
32.5–32.7 C, normal reported thermal status throughout, load 3.24–3.86. Frequency
still scales under Android control (614.4–1,804.8 MHz at process boundaries).

The following table is again 1,000 entities × five attributes, microseconds:

| Operation | Nested maps | Flat hash | Sorted dense | Wide tree + slabs |
| --- | ---: | ---: | ---: | ---: |
| Read all fields using cached entity IDs | 973.15 | 233.44 | 222.76 | 735.31 |
| Visit entities and all their attributes | 141.15 | 611.30 | 19.38 | 53.65 |
| Build UI order and read all fields | 1313.99 | 706.59 | 408.59 | 1194.97 |
| Replace one existing value | 0.168 | 0.106 | 0.112 | 0.345 |
| Remove one entity + add another | 2.97 | 1.15 | 528.44 | 6.22 |

These timings reinforce the structural tradeoff: sorted dense storage gives the
fastest sequential walks but expensive membership edits; the wide tree handles
those edits much more cheaply while ordered traversal remains slower than a
contiguous array. Absolute lookup costs change considerably with core/scheduling
conditions. The phone is a useful slower target, not automatically a noise-free
or strict worst-case measurement platform.

```sh
python3 datafriend-benchmarks/benchmarks/run_android_views.py --serial ZY227364TP \
  --cpu 0 --output-prefix datafriend-android-view-layouts-pinned
```

[Pinned summary](benchmarks/datafriend-android-view-layouts-pinned.csv),
[pinned samples](benchmarks/datafriend-android-view-layouts-pinned-samples.csv),
[pinned metadata](benchmarks/datafriend-android-view-layouts-pinned-metadata.json).

## Reproduce

```sh
python3 datafriend-benchmarks/benchmarks/run_android_views.py --serial ZY227364TP
```

The runner discovers the installed NDK, builds one arm64 executable for all four
variants, pushes it to a unique `/data/local/tmp` directory, runs the matrix and
removes only that directory afterward. It records battery/thermal state and
source/binary hashes. No app installation or device settings change is required.
Use `--cpu 0` for a single-core control. Use `--sdk`, `--ndk`, `--rounds`,
`--samples`, or `--output-prefix` to override
paths and dimensions. Samples must be positive and even.

[Summary CSV](benchmarks/datafriend-android-view-layouts.csv),
[all timed samples](benchmarks/datafriend-android-view-layouts-samples.csv),
[raw per-process runs](benchmarks/datafriend-android-view-layouts-runs),
[metadata and hashes](benchmarks/datafriend-android-view-layouts-metadata.json).

The reported UI-order workload enumerates IDs without an unnecessary preliminary
ID sort; only the requested value-based sort is required. Earlier exploratory
measurements were replaced after correcting that benchmark detail.

No ordered representation has been selected for production from this comparison.
The packed flat-map prototype remains in the engine, with cached UI ordering;
the sorted dense and wide-tree view alternatives are benchmark adapters.
