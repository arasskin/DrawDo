# Hash-table comparison

Medians of 3 seeds; 12 serial fresh processes. Times in ns; churn is per erase/insert pair.

| Entries | Engine | Insert | Hit | Miss | Update | Churn pair | Erase | Build bytes/entry | Churn bytes/entry | Occupancy |
|---:|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 16384 | current | 39.16 | 11.46 | 10.54 | 17.08 | 34.50 | 13.57 | 96.00 | 192.00 | 50.0% |
| 16384 | dense_prehashed | 14.20 | 3.39 | 2.12 | 5.59 | 15.70 | 10.12 | 51.00 | 51.00 | 50.0% |
| 16384 | dense_xxhash | 19.53 | 5.97 | 4.45 | 6.89 | 19.83 | 14.92 | 51.00 | 51.00 | 50.0% |
| 16384 | integrated | 15.98 | 3.38 | 2.17 | 5.01 | 14.22 | 10.37 | 51.01 | 51.01 | 50.0% |
