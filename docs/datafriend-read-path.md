# Dart read-path benchmark

These measurements precede the [direct decoder optimization](datafriend-decoder.md),
which subsequently reduced full-read time. The controls and findings below
describe the original decoder.

The main cost is repeatedly decoding native boxed values into Dart values.
Walking the native ID array is small; allocating `Task` objects is also small.
There is still a substantial per-field lookup/FFI cost before decoding.

At 1,000 tasks, a fresh app read takes **388 µs** without the edit, versus
**18 µs** when the same app projection is already cached. Each task needs three
fields: two short ASCII strings (`id`, `title`) and one boolean (`done`). A done
edit invalidates the entire task projection, so a subsequent full read decodes
all 3,000 fields again even though only one changed. A visible-row read only
decodes 45 fields, but still rebuilds ordering and scans the completion count.

## Controlled results

Microseconds; median of three round medians. These are separate controlled
workloads, **not additive phases**. They include loop/checksum work where fields
or models are consumed. ID construction cases close their buffer in the timer.

| Workload | 100 tasks | 1,000 tasks |
|---|---:|---:|
| Iterate existing native IDs (`for in`) | 0.583 | 5.583 |
| Iterate existing native IDs (indexed) | 0.459 | 4.500 |
| Iterate existing Dart ID list | 0.083 | 0.542 |
| Build native ordering index by `order` | 1.250 | 9.792 |
| Build native index without order field | 0.792 | 4.208 |
| Probe three fields/task, skip decoding | 8.084 | 89.500 |
| Read/decode three fields/task, no models | 31.709 | 323.292 |
| Decode pre-encoded Dart bytes, no FFI | 18.709 | 193.959 |
| Construct models from existing Dart values | 1.167 | 10.166 |
| Read/decode into ordinary list of models | 32.792 | 333.875 |
| Traverse cached app lazy models | 1.917 | 18.167 |
| Traverse ordinary list of existing models | 0.084 | 0.875 |
| Native completion count | 0.584 | 4.958 |
| Fresh app ordering/lazy-list setup | 1.708 | 12.000 |
| Fresh app materialization, ordering prepared | 39.833 | 375.292 |
| Copy cached app models to immutable list, consume | 2.416 | 22.792 |
| Fresh app task read | 41.917 | 388.250 |
| Fresh app document export/read | 42.667 | 392.875 |
| Fresh 15-row read + completion count | 9.417 | 24.041 |
| Complete edit only | 15.250 | 17.041 |
| Complete edit + document read | 56.625 | 412.458 |
| Complete edit + 15 rows + count | 23.084 | 39.833 |

`order_unordered` means no order *attribute*: the current native implementation
still sorts by entity ID. It is not an unsorted raw-map traversal.

For 1,000 tasks, median round p99 is **465 µs** for a fresh document read,
**485 µs** including the edit, and **58 µs** for edit + visible rows/count.
Full-read medians reproduce the previous roughly 0.4 ms result. This fixture
contains only tasks, unlike the Redux benchmark's additional retained drawing
and stickers; do not interpret small differences as a code improvement.

## What the controls establish

- **Iteration:** native ID traversal is about 6 µs. Replacing it with a Dart
  list saves only a few microseconds here, and copying that list would add work.
- **Lookup boundary:** `containsField` uses the same `_readValue`, string-to-ID
  cache and `df_result_value` call as `value`, but skips wrapping/decoding the
  returned bytes. Its 90 µs includes Dart checks, FFI and native map probes;
  this benchmark does not separate those three costs.
- **Conversion:** adding byte wrapping and decoding raises the field scan from
  90 to 323 µs. Decoding prebuilt `Triple.value` bytes entirely within Dart still
  takes 194 µs. These controls establish conversion as the largest cost; their
  difference is not an exact allocation or UTF-8 phase profile.
- **Models:** constructing a list of 1,000 tasks from existing Dart strings/bools
  costs 10 µs. Direct reads into such a list take 334 µs. Most of the expense
  happens before the `Task` constructor.
- **App adapter:** materializing fresh lazy models with ordering already prepared
  takes 375 µs. Its per-index `Map.putIfAbsent`, closure dispatch and cache inserts
  add work. Reusing this cache takes 18 µs, versus 0.9 µs for an ordinary list.
  Separate controls cannot assign the whole difference to a single operation.

The native result remains a shared mutable map. No triples or second database
are reconstructed by the app. Its boxed values contain encoded bytes, however,
so reading those values still produces Dart strings and scalars.

## Clear next targets

1. Simplify single-value decoding. `_decodeValue` currently uses the general
   `_Reader`; even reading a type tag or boolean calls `take`, which creates a
   typed-list view. String decoding traverses further typed/byte-data views.
   Direct offsets and type-specific reads could avoid this wrapper work while
   retaining bounds checks and owned Dart string/byte results. Allocation counts
   and the achievable speedup have not been measured yet.
2. Avoid re-decoding unchanged values after every query notification. Current
   invalidation drops all cached task models and ordering, even for a done-only
   edit. More selective cache invalidation could save the most total work, but
   would require a deliberate change to publication/cache information.
3. After decoding, investigate the per-field boundary and lazy model cache.
   Batching field access or changing cache representation are experiments, not
   demonstrated gains from this run. Ordering is a smaller full-read cost,
   although material for a 15-row read.

No production behavior was changed for this benchmark.

## Reproduction and validation

```sh
python3 datafriend-benchmarks/benchmarks/run_read_path.py \
  --dart /Users/alex/developer/flutter/bin/cache/dart-sdk/bin/dart
```

- macOS arm64, Dart AOT, separate uninstrumented C++ Release library.
- 22 cases × two sizes × three rounds, each with 1,000 warmups and 3,000 timed
  samples. Case order rotates/reverses between rounds. Processes run sequentially.
- Another 44 small odd/even validation processes check both toggle outcomes and
  fewer/more than 15 rows. **All 176 processes pass**.
- Every iteration checks the observed checksum outside the timer; final checks
  verify exact IDs, titles, completion state, order and publication count.
- `fresh_*` edits happen outside the timer. `fresh_materialize` also obtains the
  native ordering index before timing. `edit_*` includes the complete operation.
- Reused IDs and attribute-ID caches are warmed for field microbenchmarks.
  Encoded triples and decoded fixture values are built before timing their
  respective controls. Model construction escapes into a retained list.
- No per-field timers or profiling counters; a single stopwatch surrounds each
  case invocation. `dart analyze` passes for the harness. Production tests were
  not rerun because production code is unchanged.
- No rendering, networking or synchronous disk flush. Fresh/edit cases still
  perform real atomic transactions using `durable:false`. Short ASCII fields and
  host hardware do not establish mobile frame times or long-string/binary costs.
- Raw [CSV](benchmarks/datafriend-read-path.csv) and
  [metadata](benchmarks/datafriend-read-path-metadata.json) include all round
  distributions, process commands, load averages, source/binary hashes and build
  configuration. Tail timings vary more than medians under host scheduling/GC.

Sources: [Dart harness](../src/app/tool/benchmark_read_path.dart),
[runner](../datafriend-benchmarks/benchmarks/run_read_path.py).
