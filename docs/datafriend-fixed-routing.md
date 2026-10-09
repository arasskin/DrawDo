# Fixed-entity subscription routing

Fixed-entity query clauses now register a packed `(entity, attribute)` dependency.
Variable-entity clauses retain attribute-wide dependencies, and all-facts queries
retain wildcard subscriptions. Each clause registers separately, so joins mixing
fixed and variable entities still react to changes on either side. No Dart API,
notification, persistence, or UI rebuild policy changes.

The commit path records actual changed pairs from the old/new entity records,
including removed attributes. It unions matching groups before evaluation, so a
query matched by several changed pairs runs once. Identical query descriptions
still share their group; its dependencies disappear with its last subscriber.

For the measured twelve mounted task rows, a title edit now visits one query
group instead of twelve. Completion visits the row and the counter (two instead
of thirteen). Order still visits the list alone. The final scan for changed
subscription handles remains; this does not make the entire edit independent of
subscription count.

## Verification

Native tests cover 1,000 row subscriptions, exact notification IDs, shared aliases,
last-owner unsubscribe, batched changes, predicate changes, entity zero and maximum
IDs, mixed joins, multiple fixed entities, imported deletion, and commit rollback.
A batch changing different attributes on different entities must not invalidate
the Cartesian product of those entities and attributes.

The existing Flutter tests verify one-row title rebuilds, row-and-counter
completion rebuilds, list reordering, filters, offscreen edits and subscription
lifetime. Both benchmark apps assert rendered values, rebuild scope and reopened
database contents on every run.

## Measurements

The GUI comparison alternates a preserved pre-change Release app and the current
Release app, both using the same per-row UI and byte-identical Dart AOT binary.
The preserved native library matches the previous per-row benchmark's hash.
There are three rounds at 100 and 1,000 tasks, with title, completion and order
edits: 36 fresh processes, five warmups and 20 measured edits each (720 samples).
Async flushing remains enabled. No builds or other tests run alongside timing.

```sh
# Preserve the old bundle and engine.cpp before rebuilding.
python3 src/todo_demo/tool/run_widget_benchmarks.py --skip-build \
  --samples 20 --uis rows --output-prefix datafriend-fixed-routing \
  --baseline-app /path/to/preserved/datafriend_todo_lab.app \
  --baseline-engine-source /path/to/preserved/engine.cpp
```

A separate native benchmark alternates the old/new libraries in one C++ driver.
It seeds 1,000 entities and varies fixed-row subscriptions across 12, 100 and
1,000. Each process runs 500 warmups and 3,000 title edits without frame pauses.
The timer includes owner-thread dispatch, DB/index updates and query publication,
but no FFI, Dart or Flutter. Notification IDs and evaluated-group counts are
asserted. Three rounds produce 18 processes; native summaries report the median
of the three per-process medians, not a pooled sample percentile.

```sh
python3 datafriend-benchmarks/benchmarks/run_fixed_routing.py \
  --baseline-library /path/to/preserved/Contents/Frameworks/libdatafriend.dylib
```

## Results — 2026-09-27

All three native suites, 32 app tests and six demo tests pass. All 36 GUI cases
(720 measured edits) and 18 native cases (54,000 measured edits) pass their
correctness checks. The benchmark source/binary hashes and baseline provenance
were checked against the previous run. No blockers.

Apple M3, macOS 26.5.1 arm64. GUI one-minute load ranged from 3.64 to 4.96.
At 1,000 stored tasks, GUI milliseconds, median / p95 (60 samples per cell):

| Edit | Measurement | Attribute routing | Fixed-entity routing |
| --- | --- | ---: | ---: |
| Completion | State ready | 0.355 / 0.949 | 0.317 / 0.804 |
| | Frame build | 2.121 / 3.263 | 2.088 / 2.873 |
| | End-of-frame | 13.694 / 16.277 | 13.811 / 15.518 |
| Title | State ready | 0.346 / 0.983 | 0.384 / 0.647 |
| | Frame build | 1.406 / 1.759 | 1.351 / 1.666 |
| | End-of-frame | 13.778 / 15.290 | 13.708 / 15.178 |
| Order | State ready | 0.333 / 0.618 | 0.288 / 0.883 |
| | Frame build | 4.985 / 6.705 | 5.090 / 6.981 |
| | End-of-frame | 14.063 / 17.289 | 13.820 / 17.581 |

At 100 tasks, median state-ready times were completion 0.320 → 0.388 ms,
title 0.396 → 0.366 ms, and order 0.307 → 0.303 ms. Results are mixed across
workloads and sizes: this GUI run does not establish an overall latency gain.
Order serves as a useful control: its query-group count did not change, yet its
timings also moved. Every builder-count tuple remained exactly as before:
title list/row/counter = 0/1/0, completion = 0/1/1, order = 1/12/0, screen = 0.

The tight native loop isolates the scalability benefit. Microseconds, median of
three process medians; each process measures 3,000 edits:

| Fixed row subscriptions | Before | After | Groups evaluated before → after |
| --- | ---: | ---: | ---: |
| 12 | 16.666 | 15.709 | 12 → 1 |
| 100 | 20.833 | 16.333 | 100 → 1 |
| 1,000 | 73.125 | 19.917 | 1,000 → 1 |

That is approximately 1 µs saved at the demo's twelve mounted rows and 53 µs
(73%) at 1,000 subscriptions. The latter is a stress case, not the demo's normal
viewport. This saving is too small at twelve rows to account for most of the
previous 0.12–0.14 ms UI-level difference between bulk and per-row subscriptions.
The earlier attribution of that difference to routing was only a hypothesis.
The native loop also has different pacing and simpler entity records than the
GUI workload, so its timings cannot be subtracted from GUI totals as a breakdown.

Native median-of-run p95s were 18.833 → 18.166 µs (12), 24.625 → 18.666 µs
(100), and 80.042 → 22.542 µs (1,000). One optimized 1,000-subscription sample
took 3.300 ms; these improvements are not a worst-case latency guarantee.
The remaining handle scan is still linear in subscription count. Frame timing
limits from the [per-row benchmark](datafriend-row-queries.md) still apply.

Artifacts:

- [GUI pooled results](benchmarks/datafriend-fixed-routing.csv),
  [raw runs](benchmarks/datafriend-fixed-routing-runs/), and
  [metadata](benchmarks/datafriend-fixed-routing-metadata.json).
- [Native per-process results](benchmarks/datafriend-fixed-routing-native.csv)
  and [metadata](benchmarks/datafriend-fixed-routing-native-metadata.json).
