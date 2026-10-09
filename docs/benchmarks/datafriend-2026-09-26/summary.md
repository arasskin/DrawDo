# Tree comparison

Medians across seeds, each in a separate process. See metadata.json and raw.jsonl.

## Tree operations

| Records | Keys | Tree | Bytes/record* | RSS delta MiB | Hit ns | Replace ns | Range ns | Insert ns | Erase ns | Reinsert ns |
|---:|---|---|---:|---:|---:|---:|---:|---:|---:|---:|
| 1000 | clustered | binary | 48.0 | 0.42 | 61.1 | 79.8 | 113.8 | 102.2 | 126.2 | 155.7 |
| 1000 | clustered | wide | 29.3 | 0.28 | 38.2 | 30.9 | 133.1 | 79.9 | 110.0 | 133.3 |
| 1000 | sequential | binary | 48.0 | 0.39 | 66.7 | 86.0 | 112.0 | 91.2 | 128.8 | 152.7 |
| 1000 | sequential | wide | 28.7 | 0.22 | 38.6 | 29.4 | 122.7 | 97.2 | 106.4 | 141.4 |
| 1000 | uniform | binary | 48.0 | 0.39 | 52.8 | 76.2 | 111.6 | 201.8 | 119.6 | 159.8 |
| 1000 | uniform | wide | 29.8 | 0.27 | 42.5 | 51.6 | 130.6 | 141.5 | 107.8 | 128.5 |
| 100000 | clustered | binary | 48.0 | 5.06 | 124.4 | 128.2 | 246.1 | 37.1 | 248.1 | 280.9 |
| 100000 | clustered | wide | 29.1 | 3.31 | 89.4 | 54.9 | 249.4 | 90.6 | 220.3 | 223.0 |
| 100000 | sequential | binary | 48.0 | 5.06 | 122.3 | 129.4 | 244.6 | 35.3 | 255.6 | 274.4 |
| 100000 | sequential | wide | 29.0 | 3.25 | 91.2 | 55.9 | 248.2 | 88.5 | 224.4 | 229.0 |
| 100000 | uniform | binary | 48.0 | 5.09 | 123.7 | 187.0 | 285.3 | 250.3 | 247.4 | 269.1 |
| 100000 | uniform | wide | 29.9 | 3.59 | 92.1 | 103.4 | 247.1 | 207.4 | 219.6 | 223.6 |
| 1000000 | clustered | binary | 48.0 | 46.31 | 219.5 | 174.3 | 598.7 | 40.3 | 514.1 | 607.0 |
| 1000000 | clustered | wide | 29.1 | 31.27 | 160.0 | 70.5 | 523.7 | 100.5 | 491.7 | 451.2 |
| 1000000 | sequential | binary | 48.0 | 46.25 | 216.4 | 158.2 | 608.2 | 42.3 | 514.5 | 585.4 |
| 1000000 | sequential | wide | 29.1 | 31.31 | 161.4 | 69.7 | 532.9 | 104.1 | 489.3 | 449.7 |
| 1000000 | uniform | binary | 48.0 | 46.27 | 245.4 | 343.4 | 658.5 | 395.4 | 564.0 | 618.8 |
| 1000000 | uniform | wide | 30.0 | 32.50 | 157.6 | 181.0 | 464.7 | 300.7 | 499.7 | 458.7 |

*Bytes/record: wide requested allocations including spare capacity; binary high-water slots including reusable free gaps. Both exclude allocator/page rounding. RSS deltas are noisy process measurements, not exact tree ownership.

## Reconciliation

| Records | Differences | Server strategy | CPU ms | Sequential turns | Packets | Wire KiB | Apply ms |
|---:|---|---|---:|---:|---:|---:|---:|
| 1000 | client-empty | binary | 0.18 | 5 | 5 | 17.3 | 0.04 |
| 1000 | client-empty | binary-scan | 0.18 | 5 | 5 | 17.1 | 0.04 |
| 1000 | client-empty | wide | 0.16 | 5 | 5 | 17.1 | 0.10 |
| 1000 | clustered | binary | 0.08 | 6 | 6 | 4.2 | 0.00 |
| 1000 | clustered | binary-scan | 0.10 | 6 | 6 | 3.7 | 0.00 |
| 1000 | clustered | wide | 0.08 | 6 | 6 | 3.7 | 0.00 |
| 1000 | disjoint | binary | 0.54 | 6 | 6 | 41.9 | 0.35 |
| 1000 | disjoint | binary-scan | 0.74 | 6 | 6 | 49.2 | 0.36 |
| 1000 | disjoint | wide | 0.66 | 6 | 6 | 49.2 | 0.33 |
| 1000 | identical | binary | 0.00 | 2 | 2 | 0.0 | 0.00 |
| 1000 | identical | binary-scan | 0.01 | 2 | 2 | 0.0 | 0.00 |
| 1000 | identical | wide | 0.01 | 2 | 2 | 0.0 | 0.00 |
| 1000 | server-empty | binary | 0.14 | 4 | 4 | 16.4 | 0.15 |
| 1000 | server-empty | binary-scan | 0.14 | 4 | 4 | 16.4 | 0.15 |
| 1000 | server-empty | wide | 0.14 | 4 | 4 | 16.4 | 0.14 |
| 1000 | sparse | binary | 0.11 | 6 | 6 | 8.9 | 0.00 |
| 1000 | sparse | binary-scan | 0.14 | 6 | 6 | 6.2 | 0.00 |
| 1000 | sparse | wide | 0.13 | 6 | 6 | 6.2 | 0.00 |
| 100000 | client-empty | binary | 14.41 | 5 | 30 | 1630.7 | 6.49 |
| 100000 | client-empty | binary-scan | 15.88 | 5 | 30 | 1630.8 | 6.34 |
| 100000 | client-empty | wide | 13.23 | 5 | 30 | 1630.8 | 13.62 |
| 100000 | clustered | binary | 5.92 | 10 | 14 | 397.3 | 0.41 |
| 100000 | clustered | binary-scan | 6.26 | 7 | 14 | 502.8 | 0.39 |
| 100000 | clustered | wide | 3.76 | 7 | 14 | 502.8 | 0.36 |
| 100000 | disjoint | binary | 63.94 | 10 | 75 | 4326.5 | 56.83 |
| 100000 | disjoint | binary-scan | 55.59 | 8 | 67 | 3922.9 | 56.86 |
| 100000 | disjoint | wide | 48.08 | 8 | 67 | 3922.9 | 51.76 |
| 100000 | identical | binary | 0.01 | 2 | 2 | 0.0 | 0.00 |
| 100000 | identical | binary-scan | 0.01 | 2 | 2 | 0.0 | 0.00 |
| 100000 | identical | wide | 0.01 | 2 | 2 | 0.0 | 0.00 |
| 100000 | server-empty | binary | 13.16 | 4 | 29 | 1629.8 | 14.49 |
| 100000 | server-empty | binary-scan | 13.35 | 4 | 29 | 1629.8 | 14.48 |
| 100000 | server-empty | wide | 13.10 | 4 | 29 | 1629.8 | 20.08 |
| 100000 | sparse | binary | 10.50 | 10 | 21 | 836.6 | 0.52 |
| 100000 | sparse | binary-scan | 8.82 | 7 | 24 | 1135.3 | 0.53 |
| 100000 | sparse | wide | 6.03 | 7 | 24 | 1135.3 | 0.44 |
| 1000000 | client-empty | binary | 220.16 | 5 | 261 | 16298.6 | 155.20 |
| 1000000 | client-empty | binary-scan | 274.18 | 5 | 261 | 16298.7 | 154.60 |
| 1000000 | client-empty | wide | 164.22 | 5 | 261 | 16298.7 | 159.32 |
| 1000000 | clustered | binary | 89.55 | 10 | 73 | 4233.6 | 7.02 |
| 1000000 | clustered | binary-scan | 180.19 | 8 | 68 | 3974.5 | 6.41 |
| 1000000 | clustered | wide | 52.84 | 8 | 68 | 3974.5 | 5.15 |
| 1000000 | disjoint | binary | 884.60 | 11 | 724 | 45811.3 | 740.07 |
| 1000000 | disjoint | binary-scan | 700.92 | 9 | 571 | 36069.6 | 744.37 |
| 1000000 | disjoint | wide | 466.02 | 9 | 571 | 36069.6 | 593.75 |
| 1000000 | identical | binary | 0.01 | 2 | 2 | 0.0 | 0.00 |
| 1000000 | identical | binary-scan | 0.01 | 2 | 2 | 0.0 | 0.00 |
| 1000000 | identical | wide | 0.01 | 2 | 2 | 0.0 | 0.00 |
| 1000000 | server-empty | binary | 166.27 | 4 | 260 | 16297.7 | 163.20 |
| 1000000 | server-empty | binary-scan | 165.53 | 4 | 260 | 16297.7 | 163.43 |
| 1000000 | server-empty | wide | 164.78 | 4 | 260 | 16297.7 | 239.45 |
| 1000000 | sparse | binary | 165.13 | 10 | 146 | 8966.4 | 11.19 |
| 1000000 | sparse | binary-scan | 232.57 | 8 | 142 | 8710.6 | 10.13 |
| 1000000 | sparse | wide | 83.85 | 8 | 142 | 8710.6 | 7.66 |

Client always uses wide-tree counts. Server binary uses separator splits; binary-scan computes balanced splits by enumeration; wide uses cached counts. All use the same codec, 16-way target, 32-entry list threshold, and 64 KiB packets. Turns include a terminal empty message. Packets sent together do not add turns.

CPU ms is process CPU time during in-process reconciliation, including encoding, decoding, and difference collection. It excludes tree construction, actual network delay, payload transfer, and apply time. Raw rows also report elapsed time. A latency model may add turns × RTT/2 to elapsed time, but this is not a measured network result.
