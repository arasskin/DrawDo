# Direct value decoder

These measurements isolate the decoder change. The subsequent
[selective row cache](datafriend-row-cache.md) additionally avoids decoding
unchanged task/sticker rows after edits.

The decoder now reads the value tag, fixed-width fields and blob length directly
from the input bytes. Strings pass a byte range to `Utf8Decoder`; binary values
make one owned copy. It no longer constructs a general `_Reader` and intermediate
typed-list views for tags, booleans, lengths and string payloads. Numeric values
use one byte-data view with the correct input offset.

The wire format, FFI calls, mutable result maps, query notifications and app
cache invalidation are unchanged. Length, trailing-data, unknown-tag and UTF-8
validation remain in place. Returned strings/bytes remain owned Dart values.

## Results

Microseconds, median of three interleaved round medians. Both executables use the
same native Release library and the unchanged read-path harness. These are
separate controlled workloads, not additive phases.

| Workload | 100 tasks before | After | 1,000 tasks before | After |
|---|---:|---:|---:|---:|
| Probe three fields/task without decoding | 8.292 | 8.084 | 88.875 | 89.417 |
| Decode pre-encoded Dart bytes, no FFI | 18.708 | 3.708 | 190.750 | 36.791 |
| Native field lookups + decoding | 32.125 | 13.708 | 319.708 | 154.375 |
| Fresh app task read, edit excluded | 42.000 | 25.166 | 388.084 | 214.583 |
| Fresh document export/read, edit excluded | 42.750 | 25.541 | 396.500 | 216.000 |
| Complete edit + document read | 56.834 | 39.166 | 412.708 | 233.875 |
| Complete edit + 15 visible rows + count | 23.166 | 19.625 | 39.958 | 37.417 |

At 1,000 tasks, decoding without FFI is **5.2× faster**; native field lookup plus
decoding is **2.1× faster**. A complete edit and document read is **43% faster**
(1.76×), with median round p99 improving from **498 to 286 µs**. The fresh
document-read p99 improves from **480 to 274 µs**.

Visible-row gains are smaller: **6%** at 1,000 tasks and **15%** at 100 tasks.
Only 45 fields are decoded there, while native ordering, counting and the edit
remain. Native probe-only time stays approximately 89 µs, so the earlier
conversion bottleneck was reduced without changing lookup behavior.

The result supports keeping this simple decoder change. The remaining full-read
cost includes per-field FFI/map lookup and app projection construction. Full
projection invalidation still re-decodes unchanged task fields after an edit;
that behavior has not been changed. Integration work can proceed with this gain
without redesigning the result interface.

## Validation and reproduction

- All **21 Flutter tests pass**, including a new test for signed integer limits,
  floating-point limits, empty/Unicode strings, entity references, binary ownership
  and values retained after replacement of the native boxes.
- Changed Dart files analyze cleanly. An additional local VM check invoked the
  private decoder and passed **399 cases** covering every truncated prefix of
  representative values, trailing data, unknown tags, invalid UTF-8, oversized
  blob lengths, nonzero buffer offsets and owned binary results.
- All **352 benchmark/validation processes pass**: 88 small odd/even validation
  processes and 264 timed processes. Each timed process has 1,000 warmups and
  3,000 samples. Old/new order alternates between rounds; each pair runs adjacent.
- The preserved baseline executable matches the original read-path metadata.
  Source hashes confirm the Dart production change is confined to the decoder;
  the harness, app adapter and native sources are unchanged. The runner was
  extended to accept and verify a baseline executable and record both versions.
- Benchmarks ran sequentially after tests. Host macOS arm64, Dart AOT, native
  Release, short ASCII task fields. No rendering, network or synchronous disk
  flush; edits use real AELMDB transactions with `durable:false`.

```sh
python3 datafriend-benchmarks/benchmarks/run_read_path.py \
  --dart /Users/alex/developer/flutter/bin/cache/dart-sdk/bin/dart \
  --baseline-executable datafriend-benchmarks/build/dart-read-path-before-decoder \
  --baseline-metadata docs/benchmarks/datafriend-read-path-metadata.json \
  --output-prefix datafriend-decoder
```

The baseline executable is a preserved local build artifact. A current-only run
can omit both baseline options. Preserve historical output with a new prefix.

Raw [CSV](benchmarks/datafriend-decoder.csv) and
[metadata](benchmarks/datafriend-decoder-metadata.json) contain all 22 workloads,
two sizes, three rounds, p50/p95/p99/max/mean, commands, load averages,
configuration and source/binary hashes. See the
[original read-path methodology](datafriend-read-path.md) for each control's
scope. Physical-device frame performance remains unmeasured.
