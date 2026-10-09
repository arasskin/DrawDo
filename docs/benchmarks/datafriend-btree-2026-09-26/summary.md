# B-tree and namespace comparison

Medians of three seeds; fresh, serial processes. See metadata.json and raw.jsonl.

`btree` is the 64-bit XOR/type adaptation. `btree-original` retains upstream 32-byte IDs and accumulator/SHA-256. Both retain upstream 30/60/80 node thresholds, algorithms, and node-ID/hash-table storage.

## Single map

| Records | Keys | Index | Node bytes/record* | RSS MiB | Lookup ns | Replace ns | Range ns | Insert ns | Erase ns |
|---:|---|---|---:|---:|---:|---:|---:|---:|---:|
| 1000 | sequential | binary | 48.0 | 0.41 | 66.9 | 84.1 | 113.5 | 95.0 | 136.2 |
| 1000 | sequential | btree | 37.1 | 0.14 | 72.0 | 349.5 | 205.8 | 260.2 | 179.9 |
| 1000 | sequential | btree-original | 55.6 | 0.14 | 83.7 | 354.0 | 575.0 | 276.6 | 206.2 |
| 1000 | sequential | wide | 28.7 | 0.30 | 39.2 | 30.4 | 124.9 | 91.2 | 108.0 |
| 1000 | uniform | binary | 48.0 | 0.41 | 54.0 | 81.4 | 111.8 | 198.6 | 121.1 |
| 1000 | uniform | btree | 53.0 | 0.17 | 70.5 | 379.4 | 223.8 | 300.3 | 175.7 |
| 1000 | uniform | btree-original | 79.4 | 0.19 | 81.3 | 362.0 | 575.3 | 287.0 | 205.1 |
| 1000 | uniform | wide | 29.8 | 0.33 | 42.7 | 49.3 | 132.9 | 147.1 | 113.4 |
| 100000 | sequential | binary | 48.0 | 5.08 | 124.0 | 128.9 | 246.6 | 35.7 | 249.7 |
| 100000 | sequential | btree | 33.6 | 4.17 | 134.1 | 455.0 | 720.7 | 283.9 | 316.5 |
| 100000 | sequential | btree-original | 50.3 | 5.14 | 156.7 | 487.0 | 1279.4 | 316.4 | 407.4 |
| 100000 | sequential | wide | 29.0 | 3.33 | 91.9 | 55.7 | 254.5 | 89.0 | 220.1 |
| 100000 | uniform | binary | 48.0 | 5.11 | 123.8 | 196.6 | 309.7 | 252.2 | 267.8 |
| 100000 | uniform | btree | 48.4 | 5.95 | 134.4 | 523.9 | 835.6 | 370.8 | 303.4 |
| 100000 | uniform | btree-original | 72.4 | 7.34 | 146.9 | 534.2 | 1235.8 | 358.3 | 355.2 |
| 100000 | uniform | wide | 29.9 | 3.92 | 92.2 | 104.3 | 255.4 | 209.3 | 221.7 |
| 1000000 | sequential | binary | 48.0 | 46.27 | 238.7 | 159.2 | 697.5 | 42.3 | 526.5 |
| 1000000 | sequential | btree | 33.5 | 39.94 | 310.4 | 443.9 | 1722.7 | 306.2 | 660.7 |
| 1000000 | sequential | btree-original | 50.2 | 49.81 | 363.6 | 556.5 | 2448.4 | 327.4 | 749.3 |
| 1000000 | sequential | wide | 29.1 | 31.34 | 167.0 | 69.4 | 544.8 | 104.2 | 516.1 |
| 1000000 | uniform | binary | 48.0 | 46.28 | 265.1 | 363.1 | 763.8 | 401.5 | 603.0 |
| 1000000 | uniform | btree | 48.4 | 57.53 | 334.1 | 790.8 | 2468.4 | 523.3 | 632.3 |
| 1000000 | uniform | btree-original | 72.6 | 71.86 | 372.4 | 856.9 | 3083.9 | 552.9 | 712.3 |
| 1000000 | uniform | wide | 30.0 | 32.78 | 171.7 | 191.1 | 498.4 | 306.5 | 522.5 |

## Many namespaces

| Profile | Maps | Records | Index | Accounted MiB* | RSS MiB | Mixed ns/event | Batch p95 ns/event | Drop 10% ms | Rebuild 10% ms |
|---|---:|---:|---|---:|---:|---:|---:|---:|---:|
| large | 1000 | 1000000 | binary | 45.84 | 46.41 | 179.57 | 229.46 | 0.94 | 10.57 |
| large | 1000 | 1000000 | btree | 48.07 | 59.81 | 276.74 | 315.46 | 0.30 | 24.00 |
| large | 1000 | 1000000 | btree-original | 71.97 | 74.73 | 336.47 | 406.29 | 0.36 | 22.29 |
| large | 1000 | 1000000 | wide | 28.75 | 32.72 | 129.72 | 165.50 | 0.37 | 11.55 |
| medium | 10000 | 1000000 | binary | 46.39 | 46.98 | 143.00 | 181.96 | 1.26 | 7.32 |
| medium | 10000 | 1000000 | btree | 101.71 | 126.31 | 289.38 | 368.75 | 0.87 | 17.49 |
| medium | 10000 | 1000000 | btree-original | 152.06 | 157.69 | 373.32 | 515.29 | 0.88 | 15.92 |
| medium | 10000 | 1000000 | wide | 29.93 | 33.95 | 106.39 | 132.83 | 0.49 | 8.20 |
| skewed | 10000 | 784000 | binary | 36.50 | 37.09 | 97.72 | 122.29 | 1.25 | 9.36 |
| skewed | 10000 | 784000 | btree | 55.77 | 69.39 | 158.74 | 182.96 | 0.46 | 19.25 |
| skewed | 10000 | 784000 | btree-original | 83.25 | 86.48 | 193.84 | 221.46 | 0.52 | 18.40 |
| skewed | 10000 | 784000 | wide | 23.63 | 26.91 | 71.80 | 102.71 | 0.35 | 9.86 |
| tiny | 20000 | 205000 | binary | 10.72 | 11.39 | 58.14 | 66.92 | 0.41 | 1.08 |
| tiny | 20000 | 205000 | btree | 39.18 | 48.47 | 130.10 | 152.12 | 0.35 | 2.67 |
| tiny | 20000 | 205000 | btree-original | 58.07 | 60.23 | 150.33 | 172.25 | 0.46 | 2.72 |
| tiny | 20000 | 205000 | wide | 8.13 | 9.30 | 55.51 | 63.25 | 0.17 | 1.36 |

Accounted bytes include namespace wrappers/directory lower bound for the many-map cases. Binary counts reusable high-water slots; wide counts malloc requests including spare capacity. B-tree counts node-map values and buckets, excluding hash-table allocation bookkeeping. All exclude system allocator/page rounding. RSS includes these effects but is noisy.

## Namespace reconciliation (256 sessions)

| Profile | Index | Total CPU ms | p95 elapsed ms/session | Total turns | Wire KiB |
|---|---|---:|---:|---:|---:|
| large | binary | 15.13 | 0.121 | 1089 | 2269.9 |
| large | btree | 15.04 | 0.113 | 1088 | 2246.6 |
| large | btree-original | 18.09 | 0.122 | 1088 | 2246.6 |
| large | wide | 14.62 | 0.107 | 1088 | 2246.6 |
| medium | binary | 3.22 | 0.027 | 1034 | 355.9 |
| medium | btree | 3.13 | 0.026 | 1024 | 344.9 |
| medium | btree-original | 4.45 | 0.036 | 1024 | 344.9 |
| medium | wide | 2.99 | 0.025 | 1024 | 344.9 |
| skewed | binary | 2.26 | 0.029 | 914 | 281.1 |
| skewed | btree | 2.20 | 0.029 | 910 | 281.1 |
| skewed | btree-original | 2.38 | 0.026 | 910 | 281.1 |
| skewed | wide | 2.12 | 0.029 | 910 | 281.1 |
| tiny | binary | 0.47 | 0.005 | 830 | 61.1 |
| tiny | btree | 0.53 | 0.005 | 830 | 61.1 |
| tiny | btree-original | 0.66 | 0.005 | 830 | 61.1 |
| tiny | wide | 0.48 | 0.004 | 830 | 61.1 |

## Large-map reconciliation

| Records | Changes | Index | CPU ms | Turns | Wire KiB |
|---:|---|---|---:|---:|---:|
| 100000 | clustered | binary | 6.02 | 10 | 397.3 |
| 100000 | clustered | btree | 6.46 | 7 | 502.8 |
| 100000 | clustered | btree-original | 12.77 | 7 | 502.8 |
| 100000 | clustered | wide | 3.83 | 7 | 502.8 |
| 100000 | identical | binary | 0.01 | 2 | 0.0 |
| 100000 | identical | btree | 0.01 | 2 | 0.0 |
| 100000 | identical | btree-original | 0.03 | 2 | 0.0 |
| 100000 | identical | wide | 0.01 | 2 | 0.0 |
| 100000 | sparse | binary | 10.75 | 10 | 836.6 |
| 100000 | sparse | btree | 9.39 | 7 | 1135.3 |
| 100000 | sparse | btree-original | 17.37 | 7 | 1135.3 |
| 100000 | sparse | wide | 6.08 | 7 | 1135.3 |
| 1000000 | clustered | binary | 93.97 | 10 | 4233.6 |
| 1000000 | clustered | btree | 102.39 | 8 | 3974.5 |
| 1000000 | clustered | btree-original | 209.64 | 8 | 3974.5 |
| 1000000 | clustered | wide | 53.58 | 8 | 3974.5 |
| 1000000 | identical | binary | 0.01 | 2 | 0.0 |
| 1000000 | identical | btree | 0.01 | 2 | 0.0 |
| 1000000 | identical | btree-original | 0.03 | 2 | 0.0 |
| 1000000 | identical | wide | 0.01 | 2 | 0.0 |
| 1000000 | sparse | binary | 170.83 | 10 | 8966.4 |
| 1000000 | sparse | btree | 180.33 | 8 | 8710.6 |
| 1000000 | sparse | btree-original | 322.76 | 8 | 8710.6 |
| 1000000 | sparse | wide | 82.73 | 8 | 8710.6 |

The native B-tree uses a native B-tree client to match its accumulator; XOR B-tree/binary use a wide client. Every result is checked against an exact record oracle. Bytes/turns are encoded in-process measurements, not real network latency.
