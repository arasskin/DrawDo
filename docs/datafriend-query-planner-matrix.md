# Complete query planner matrix

Median of three process medians, in microseconds. Each process uses 20 warmups and 100 measured calls.

The baseline already has incremental entity-local updates. Values above 1 in the speedup column favor the new planner. See [methodology and interpretation](datafriend-query-planner.md).

## 100 tasks (+100 owners)

| Workload | Before µs | Planned µs | Speedup | Before p99 µs | Planned p99 µs |
| --- | ---: | ---: | ---: | ---: | ---: |
| subscribe_wide | 104.959 | 61.875 | 1.70× | 119.167 | 72.000 |
| subscribe_rare_first | 5.583 | 5.541 | 1.01× | 9.458 | 11.583 |
| subscribe_rare_last | 15.750 | 5.541 | 2.84× | 19.625 | 12.000 |
| subscribe_broad | 23.333 | 13.375 | 1.74× | 29.333 | 21.209 |
| subscribe_forward | 9.250 | 5.500 | 1.68× | 13.666 | 9.125 |
| subscribe_reverse | 5.208 | 5.375 | 0.97× | 8.500 | 8.625 |
| subscribe_chain | 12.625 | 5.750 | 2.20× | 30.208 | 11.583 |
| subscribe_value_join | 53.666 | 6.625 | 8.10× | 60.542 | 12.334 |
| subscribe_equality | 10.875 | 7.917 | 1.37× | 14.875 | 12.875 |
| subscribe_disconnected | 16.583 | 14.208 | 1.17× | 23.083 | 19.833 |
| subscribe_empty | 6.541 | 4.666 | 1.40× | 10.000 | 9.708 |
| edit_wide | 18.167 | 17.416 | 1.04× | 31.000 | 30.834 |
| edit_rare_last | 17.167 | 16.750 | 1.02× | 29.958 | 34.125 |
| edit_forward | 21.417 | 17.583 | 1.22× | 39.958 | 51.292 |
| edit_reverse | 17.542 | 16.792 | 1.04× | 46.500 | 32.000 |
| edit_chain | 23.750 | 17.500 | 1.36× | 31.792 | 59.500 |
| edit_value_join | 65.916 | 17.625 | 3.74× | 78.625 | 25.084 |
| predicate_flip | 17.042 | 17.208 | 0.99× | 26.542 | 35.334 |
| remote | 19.500 | 12.209 | 1.60× | 39.459 | 19.834 |
| delete_reinsert | 16.458 | 16.667 | 0.99× | 31.000 | 32.459 |
| batch32 | 236.083 | 220.625 | 1.07× | 261.209 | 241.417 |
| shared32 | 16.958 | 17.583 | 0.96× | 34.333 | 27.291 |
| distinct32 | 25.416 | 21.042 | 1.21× | 40.458 | 36.458 |
| unrelated | 16.541 | 16.792 | 0.99× | 26.250 | 25.958 |
| noop | 7.750 | 7.625 | 1.02× | 14.458 | 33.000 |

## 1,000 tasks (+100 owners)

| Workload | Before µs | Planned µs | Speedup | Before p99 µs | Planned p99 µs |
| --- | ---: | ---: | ---: | ---: | ---: |
| subscribe_wide | 993.666 | 515.458 | 1.93× | 6445.460 | 553.792 |
| subscribe_rare_first | 9.541 | 8.667 | 1.10× | 16.041 | 12.000 |
| subscribe_rare_last | 114.500 | 7.084 | 16.16× | 127.917 | 12.625 |
| subscribe_broad | 182.209 | 79.125 | 2.30× | 203.083 | 92.083 |
| subscribe_forward | 50.833 | 7.916 | 6.42× | 60.500 | 13.416 |
| subscribe_reverse | 7.541 | 7.750 | 0.97× | 53.459 | 18.542 |
| subscribe_chain | 80.291 | 7.250 | 11.07× | 102.333 | 12.792 |
| subscribe_value_join | 482.917 | 14.708 | 32.83× | 506.625 | 19.875 |
| subscribe_equality | 60.917 | 34.333 | 1.77× | 72.500 | 43.500 |
| subscribe_disconnected | 119.917 | 87.709 | 1.37× | 132.708 | 99.334 |
| subscribe_empty | 25.875 | 5.667 | 4.57× | 30.875 | 8.375 |
| edit_wide | 20.709 | 19.375 | 1.07× | 54.959 | 39.958 |
| edit_rare_last | 18.334 | 18.375 | 1.00× | 33.125 | 42.417 |
| edit_forward | 60.750 | 18.791 | 3.23× | 77.750 | 36.625 |
| edit_reverse | 19.416 | 18.625 | 1.04× | 40.958 | 36.000 |
| edit_chain | 89.959 | 18.833 | 4.78× | 106.541 | 35.375 |
| edit_value_join | 493.042 | 23.375 | 21.09× | 529.000 | 48.250 |
| predicate_flip | 18.167 | 18.125 | 1.00× | 28.708 | 34.625 |
| remote | 86.042 | 13.583 | 6.33× | 101.208 | 25.000 |
| delete_reinsert | 16.834 | 17.416 | 0.97× | 55.583 | 40.292 |
| batch32 | 245.042 | 225.417 | 1.09× | 275.167 | 256.958 |
| shared32 | 18.666 | 18.750 | 1.00× | 34.625 | 28.750 |
| distinct32 | 25.416 | 22.125 | 1.15× | 45.917 | 30.917 |
| unrelated | 17.417 | 17.625 | 0.99× | 41.917 | 34.084 |
| noop | 7.375 | 7.583 | 0.97× | 18.292 | 23.750 |

## 10,000 tasks (+100 owners)

| Workload | Before µs | Planned µs | Speedup | Before p99 µs | Planned p99 µs |
| --- | ---: | ---: | ---: | ---: | ---: |
| subscribe_wide | 11423.100 | 6294.460 | 1.81× | 12805.200 | 7221.750 |
| subscribe_rare_first | 31.542 | 22.500 | 1.40× | 38.292 | 30.792 |
| subscribe_rare_last | 1169.750 | 22.209 | 52.67× | 1387.040 | 34.750 |
| subscribe_broad | 1806.120 | 698.500 | 2.59× | 2404.540 | 998.334 |
| subscribe_forward | 419.000 | 15.125 | 27.70× | 542.875 | 24.792 |
| subscribe_reverse | 18.750 | 14.958 | 1.25× | 26.667 | 26.334 |
| subscribe_chain | 703.709 | 15.875 | 44.33× | 897.833 | 28.500 |
| subscribe_value_join | 4891.670 | 94.500 | 51.76× | 9796.620 | 126.208 |
| subscribe_equality | 588.334 | 296.333 | 1.99× | 788.083 | 464.083 |
| subscribe_disconnected | 1122.210 | 759.583 | 1.48× | 1340.290 | 1144.040 |
| subscribe_empty | 188.542 | 5.958 | 31.65× | 198.291 | 8.416 |
| edit_wide | 26.000 | 26.833 | 0.97× | 70.875 | 89.166 |
| edit_rare_last | 20.917 | 21.250 | 0.98× | 28.209 | 30.917 |
| edit_forward | 429.834 | 26.792 | 16.04× | 752.250 | 49.541 |
| edit_reverse | 30.833 | 27.042 | 1.14× | 47.667 | 43.041 |
| edit_chain | 718.958 | 26.458 | 27.17× | 941.666 | 48.959 |
| edit_value_join | 4803.830 | 70.750 | 67.90× | 10581.200 | 98.959 |
| predicate_flip | 20.541 | 20.666 | 0.99× | 42.500 | 40.500 |
| remote | 722.042 | 25.666 | 28.13× | 967.667 | 42.708 |
| delete_reinsert | 17.917 | 17.333 | 1.03× | 78.416 | 76.166 |
| batch32 | 263.167 | 240.458 | 1.09× | 402.875 | 331.708 |
| shared32 | 22.250 | 21.667 | 1.03× | 70.875 | 79.167 |
| distinct32 | 29.666 | 27.166 | 1.09× | 60.792 | 62.000 |
| unrelated | 19.917 | 19.833 | 1.00× | 64.458 | 65.333 |
| noop | 8.750 | 9.000 | 0.97× | 32.041 | 29.250 |

