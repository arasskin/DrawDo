# Where native edit time goes

Historical profile before query sharing. Sharing is now implemented; see the
[updated measurements](datafriend-query-sharing.md).

Profiled 2026-09-27 using a separate CMake Release build with
`DATAFRIEND_PROFILE=ON`. Production builds compile out all phase timers and work
counters. No disk flush (`MDB_NOSYNC`), networking, Dart, or Flutter is included.
The edit still performs an AELMDB transaction and writes dirty pages to the OS;
this is not a pure in-memory storage benchmark.

Raw [phase timings](benchmarks/datafriend-native-profile.csv),
[work counters](benchmarks/datafriend-native-profile-counts.txt), and
[uninstrumented controls](benchmarks/datafriend-profile-control.csv).

Each sample toggles one completion attribute. The original workload has 10,000
entities, three facts per entity, and ten independently subscribed **identical**
queries. Each query selects 100 entities and projects 200 facts. There are 100
warmups followed by 1,000 samples. Phase values below are arithmetic means from
the same samples; this avoids incorrectly adding independently computed medians.

| Phase | Mean µs |
| --- | ---: |
| Reevaluate queries and update result trees | 294.41 |
| AELMDB commit without fsync | 9.45 |
| Caller overhead and thread handoff | 5.10 |
| LBR3 reconciliation | 3.21 |
| Stage AELMDB writes and determine changed attributes | 2.45 |
| Read/modify touched entity | 0.84 |
| Update query indexes and find subscribers | 0.70 |
| Edit persistent working view | 0.59 |
| Publish roots | 0.05 |
| Remaining owner work, including cleanup | 1.58 |
| **Total call** | **318.37** |

Query reevaluation is **92.5%** of the total. The measured owner-thread work is
313.27 µs. A separate empty owner-thread roundtrip averages 2.83 µs; the 5.10 µs
call-minus-owner difference also includes caller-side argument copies, future
setup, wakeup and result delivery, so it should not all be called scheduling.

For every edit the counters report:

- 3,000 successful clause-candidate visits (three clauses × 100 entities × ten queries).
- 2,000 projected facts passed through pair hashing, result lookup and a temporary
  membership set—even though most facts are unchanged.
- 2,000 old result entries walked to find removals.
- Only **ten result-tree replacements**, one per subscription.

The evaluator correctly uses AEV/AVE to find the 100 matching entities. It does
not scan all 10,000 entities. However, it reruns each complete affected query and
maintains an independent result for each subscription. It has no deduplication of
identical subscriptions and no changed-entity incremental path. This explains
why a fast persistent tree does not imply a similarly cheap whole edit.

Scaling corroborates the work counts:

| Entities | Subscribers | Query mean µs | Total mean µs |
| ---: | ---: | ---: | ---: |
| 1,000 | 1 | 3.53 | 17.93 |
| 10,000 | 1 | 32.47 | 50.30 |
| 10,000 | 10 | 294.41 | 318.37 |
| 10,000 | 100 | 2,972.54 | 3,033.87 |
| 100,000 | 10 | 3,557.47 | 3,615.66 |

Largest opportunities: share evaluations/results for identical subscriptions;
for entity-local queries, reevaluate only the changed entities. General joins
need dependency handling before applying that second optimization. Reducing
query scratch allocations and repeated encoding/lookups is a secondary target.
These optimizations have not been applied in this profiling change.

Reproduce:

```sh
cmake -S datafriend-benchmarks -B datafriend-benchmarks/build/profile \
  -DCMAKE_BUILD_TYPE=Release -DDATAFRIEND_PROFILE=ON
cmake --build datafriend-benchmarks/build/profile -j 4
datafriend-benchmarks/build/profile/engine_profile 10000 10
```

Both instrumented and production native test suites pass. Timers/counters add
measurement overhead; the uninstrumented control checks its practical size.
These are host measurements and do not establish a mobile frame-time guarantee.
