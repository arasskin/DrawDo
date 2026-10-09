# Direct Datafriend calls and subscription results

2026-10-08. Implemented in the requested order:

1. Removed the engine thread, queue, futures, wakeups and handoff mutex. Engine
   operations now execute directly in the application's serialized calling context.
2. Changed `subscribe(query)` to return `std::pair<uint64_t, query_result>`.
   The native UI, particle simulation and profiling code consume both values
   directly; no second result lookup is needed.

```cpp
auto [id, result] = db.subscribe(query);
```

The result is already evaluated and shares its mutable map. Identical queries
still have distinct subscription IDs but share result storage. Existing edits
update retained results before returning. The application dispatches notifications
afterward. There is no new cache or query algorithm in this change.

The application serializes engine calls, result reads, imports and destruction.
The independent disk-flush worker remains asynchronous and never accesses maps,
indexes or transactions. Network I/O is still asynchronous in the existing
transport design; applying received changes must use the serialized app context.
Dart/Flutter support is retired. Existing source and C entry points are retained,
but compatibility is not a constraint on the supported C++ interface.

## Android checkpoint measurements

Moto G7, 1,000 tasks, 60 warmup + 360 measured frames per case, 60 actions,
asynchronous disk flushing, no network traffic. Each checkpoint is one run per
case; the earlier baseline is the first matching run in the iPhone comparison.
Numbers are milliseconds, median / p95.

| Action | Earlier threaded baseline | Direct calls | Direct calls + tuple |
| --- | ---: | ---: | ---: |
| Change filter | 7.38 / 11.56 | 4.07 / 6.00 | 4.26 / 6.27 |
| Toggle task | 1.84 / 2.13 | 1.35 / 1.58 | 1.30 / 1.47 |

Both new checkpoints have 100% EGL presentation coverage, approximately 60 Hz,
and zero display gaps over 25 ms. The combined filter median is about 42% lower
than the earlier baseline. The tuple change's isolated timing effect is too small
to distinguish from run-to-run variation after handoffs are removed. Filter
switches still create queries, sort IDs and replace visible row subscriptions.
Engine CPU now runs on the app thread, so app-thread CPU measurements include
work previously charged to the engine thread; do not compare those as the same
CPU scope across architectures.

Raw records and source/APK metadata:
`benchmarks/datafriend-direct-calls-android*` and
`benchmarks/datafriend-direct-tuple-android*`. Earlier baseline:
`benchmarks/datafriend-crossplatform-android*`.

## Validation

All five core tests and three demo tests passed at both checkpoints. The old
concurrent-caller test now exercises serialized edits and immediate publication.
Returned-result tests verify initial evaluation, alias sharing, later edits and
retention after unsubscribe. Existing tests cover atomic failure recovery,
reconciliation, background-flush errors and database reopen.

The final Android package installs and the interactive demo is restored after
profiling. The iOS Release build compiles and signs successfully; it was not
reinstalled or profiled in this change. The earlier iPhone automated-relaunch
black-screen issue remains separate and unresolved.
