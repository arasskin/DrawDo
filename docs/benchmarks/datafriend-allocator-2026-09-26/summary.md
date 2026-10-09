# Wide-tree allocator comparison

Medians of 5 seeds; paired, serial fresh processes. `wide` = malloc/free; `slab` = size-class slabs.

## Allocation replay

| Count | Keys/profile | Allocator | First pass ns/call | Pass 1 ns/call | Pass 2 ns/call | Pass 3 ns/call |
|---:|---|---|---:|---:|---:|---:|
| 100000 | uniform | slab | 8.59 | 8.77 | 8.72 | 9.01 |
| 100000 | uniform | wide | 23.44 | 20.14 | 19.80 | 19.65 |

## Single-map operations

| Count | Keys/profile | Allocator | Reserved/requested MiB | RSS MiB | Insert ns | Lookup ns | Replace ns | Erase ns | Reinsert ns |
|---:|---|---|---:|---:|---:|---:|---:|---:|---:|
| 1000 | sequential | slab | 1.25 | 0.08 | 61.25 | 40.03 | 29.58 | 92.75 | 122.50 |
| 1000 | sequential | wide | 0.03 | 0.31 | 90.92 | 39.02 | 29.46 | 108.17 | 142.00 |
| 1000 | uniform | slab | 1.38 | 0.08 | 119.50 | 41.71 | 49.33 | 94.83 | 122.75 |
| 1000 | uniform | wide | 0.03 | 0.36 | 152.08 | 42.41 | 49.25 | 108.17 | 142.50 |
| 100000 | sequential | slab | 3.56 | 1.23 | 79.87 | 91.24 | 55.43 | 216.21 | 220.21 |
| 100000 | sequential | wide | 2.76 | 3.38 | 89.45 | 91.64 | 55.43 | 222.07 | 231.52 |
| 100000 | uniform | slab | 3.75 | 1.36 | 192.30 | 90.60 | 107.83 | 214.18 | 218.34 |
| 100000 | uniform | wide | 2.86 | 3.77 | 203.06 | 88.76 | 101.74 | 220.50 | 220.83 |
| 1000000 | sequential | slab | 28.71 | 22.28 | 97.11 | 169.30 | 69.37 | 529.22 | 463.31 |
| 1000000 | sequential | wide | 27.72 | 31.38 | 104.74 | 169.20 | 70.03 | 541.58 | 484.17 |
| 1000000 | uniform | slab | 29.66 | 23.30 | 295.85 | 167.78 | 192.65 | 519.13 | 466.32 |
| 1000000 | uniform | wide | 28.57 | 32.78 | 310.58 | 166.54 | 193.23 | 554.52 | 475.43 |

## Many maps

| Count | Keys/profile | Allocator | Accounted MiB | RSS MiB | Mixed ns/event | Drop 10% ms | Refill 10% ms | Clear all ms |
|---:|---|---|---:|---:|---:|---:|---:|---:|
| 1000 | large | slab | 29.74 | 27.28 | 128.48 | 0.13 | 10.93 | 1.37 |
| 1000 | large | wide | 28.75 | 32.77 | 135.51 | 0.32 | 11.49 | 3.79 |
| 10000 | medium | slab | 31.13 | 28.69 | 112.10 | 0.27 | 7.71 | 1.84 |
| 10000 | medium | wide | 29.93 | 34.00 | 110.04 | 0.50 | 8.27 | 5.28 |
| 10000 | skewed | slab | 24.63 | 22.39 | 74.09 | 0.17 | 8.83 | 1.39 |
| 10000 | skewed | wide | 23.62 | 26.86 | 71.37 | 0.35 | 9.42 | 3.89 |
| 20000 | tiny | slab | 8.95 | 6.86 | 55.48 | 0.13 | 1.24 | 0.61 |
| 20000 | tiny | wide | 8.12 | 9.36 | 55.15 | 0.18 | 1.35 | 1.00 |

## Reconciliation

| Count | Keys/profile | Allocator | CPU ms | Apply ms | Wire KiB | Turns |
|---:|---|---|---:|---:|---:|---:|
| 100000 | uniform | slab | 5.95 | 0.43 | 1127.13 | 7.00 |
| 100000 | uniform | wide | 5.91 | 0.44 | 1127.13 | 7.00 |
| 1000000 | uniform | slab | 85.45 | 8.04 | 8708.78 | 8.00 |
| 1000000 | uniform | wide | 83.61 | 8.54 | 8708.78 | 8.00 |

Slab memory includes entire slabs, headers, unused blocks and retained empty slabs, plus large malloc fallbacks. Malloc memory counts requested live node capacity, excluding allocator overhead/retention; RSS is reported separately. Many-map accounting includes shared pool and namespace wrappers/directory.

Each allocator pair has identical allocation/copy counts and live node bytes (where measured), exact record oracles, and identical reconciliation traffic. Raw results include capacity after deletion/refill, retained empty slabs, and explicit trim checks. Replay timing includes loop/marker checks; its malloc heap has already been used to construct the trace.
