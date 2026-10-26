# Native range updates: work reduced, median presentation largely unchanged

Measured 2026-10-21 on the same Moto G7 and iPhone 13, with the same 1,000-byte ASCII
document, 503 ms edit interval and 60 attempted updates per direction as the
[whole-string baseline](crdt-native-latency-2026-10-21.md).

The adapters now find the common UTF-16 prefix/suffix, expand boundaries to avoid
splitting surrogate pairs, replace the changed range, and skip unchanged selection
assignments. Android also groups the mutation and selection in a native batch edit.
The C API, native composition checks and stale-revision/ACK rules are unchanged.

## Comparison

The table uses the complete 60-receive Android batch run and the complete 60-receive
iPhone range run. The iPhone implementation was identical in both new runs. Each
endpoint is measured from the receiving Datafriend watch callback, after import;
network transit and pre-callback polling/import delays are excluded.

| Endpoint | Baseline median | Updated median | Baseline p95 | Updated p95 |
|---|---:|---:|---:|---:|
| Android native update block only | 20.043 ms | **12.232 ms** | 22.691 ms | 13.274 ms |
| iPhone native update block only | 6.713 ms | **1.746 ms** | 8.031 ms | 2.184 ms |
| Android callback → compositor presentation | 54.278 ms | **54.851 ms** | 70.428 ms | **55.625 ms** |
| iPhone callback → captured-frame delivery | 23.807 ms | **23.088 ms** | 24.388 ms | 23.726 ms |

The native update block is stage 5 minus stage 4 for each sample, not subtraction
of aggregate medians. Its median fell about 39% on Android and 74% on iPhone.
Android's measured presentation p95 improved by about 15 ms, while its median was
essentially unchanged. iPhone's captured-frame median changed by less than 1 ms.
These are small experiments, not guaranteed bounds or device-wide performance claims.

The iPhone endpoint includes ReplayKit delivery and is **not a physical display
presentation timestamp**. Do not directly compare that row with Android presentation.

## Repeated measurements and completeness

| Variant | Device | Received / attempted | Native update median | Presentation/capture median | p95 |
|---|---|---:|---:|---:|---:|
| Range replacement | Android | 56 / 60 | 12.458 ms | 54.658 ms | 55.856 ms |
| Range replacement | iPhone | 60 / 60 | 1.746 ms | 23.088 ms | 23.726 ms |
| Range replacement + Android batching | Android | 60 / 60 | 12.232 ms | 54.851 ms | 55.625 ms |
| Range replacement + Android batching | iPhone | 56 / 60 | 1.722 ms | 23.215 ms | 23.757 ms |

The first run did not individually observe Android tags 200028, 200030–200032;
the second did not individually observe iPhone tags 100051–100054. State sync can
coalesce intermediate versions; these runs do not isolate the reason those tags
were absent at the callback. Missing tags were reported and excluded, never assigned
invented render times. Every observed receive had a frame match, with no recorder
overflow and no rejected native refreshes. Android matches were unique within the
one-refresh submission window. Final state converged.

Batching produced no substantial additional visible improvement in this workload.
It remains a small adapter-local grouping of text and selection changes, with a
matched `endBatchEdit` in `finally`; it does not introduce an application queue.

## Interpretation

Saving native update work is useful, but it did not save a display frame at the
median. The remaining delay is substantially after text assignment. This is
consistent with frame/traversal and compositor scheduling limiting this workload;
it does not prove an irreducible native latency floor. The next experiment should
trace the update against native vsync/traversal deadlines, including the currently
excluded network-pump wait, before introducing extra threads or a custom renderer.

The patch still scans and transfers whole strings. Large documents and widely
separated changes may need an explicit edit-range interface rather than this
single-range optimization. No large-document performance claim is made here.

## Validation

- Real native adapters on both phones passed 15 text/selection cases and deliberate
  stale-revision rejection: emoji changes that share a surrogate, combining marks,
  Arabic, newlines, joined emoji, identical text, empty text, and cursor positions.
  The Android fixture retried one startup revision race, as the app normally does.
- Both existing host CRDT/editor tests passed.
- The paired device UI test passed on Android and iPhone: typing, remote refresh
  with the iPhone keyboard open, stable editor origin, restart persistence and
  propagated deletion (15.5 s iPhone test).
- Tests and timing used the isolated benchmark identity/stores. Normal documents
  were not edited. Normal optimized apps were rebuilt and installed afterward.

[Raw evidence and source hashes](crdt-native-range-2026-10-21/) include both runs,
per-update matches, native test output and captured crops. Reproduce with the
[probe instructions](../../crdt-demo-app/bench/README.md).
[Rendering research and alternatives](../text-rendering-research.md) explain the
tradeoffs of TextKit 2, Android editable layouts, precomputation, glyph caching,
MSDF and front-buffer rendering.
