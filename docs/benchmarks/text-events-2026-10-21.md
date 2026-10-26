# Event-driven app processing

The CRDT demo now processes socket readiness, editor mailbox notifications, and
network deadlines independently of its render frame. State still has one owner:
Sokol's existing app thread on Android and the main thread on iPhone. Android
uses `ALooper`, an eventfd, and a timerfd; iPhone uses `CFRunLoop` sources and a
timer. Sokol continues rendering the white background on every frame.

Datafriend exposes `df_network_prepare`: a borrowed socket, read/write interest,
and the next relative deadline. The deadline also covers work left in its input
buffer or channel array when the pump budget expires. Native editor producers
only signal a wakeup; they do not enter the model or database. Both build scripts
retain `--frame-poll` for a reproducible control. Event processing is the default.

## Measurements

Physical Moto G7 (Android 10) and iPhone 13 (iOS 26.5), both 60 Hz, on 2026-10-21.
Both Android modes target API 29 and use Choreographer. Optimized native builds,
closed keyboards, 60 updates per run, 1,000-byte ASCII documents, 503 ms apart,
after 15 seconds of warmup. The same background C producer records when each
update actually becomes ready. This controlled experiment excludes networking
and CRDT processing; it isolates delivery through the native text display path.

All table cells are **median / p95 in milliseconds**. Android's display endpoint
is SurfaceFlinger actual presentation. iPhone's endpoint is delivery of the first
ReplayKit capture containing the new counter, confirmed by OCR. The iPhone
endpoint includes recording overhead and is not a physical panel timestamp.

| Run | Android ready → request | Android ready → display | iPhone ready → request | iPhone ready → capture |
|---|---:|---:|---:|---:|
| Frame polling 1 | 8.991 / 16.531 | 57.250 / 77.159 | 7.748 / 15.229 | 31.538 / 38.552 |
| Frame polling 2 | 7.925 / 14.858 | 56.340 / 72.716 | 8.270 / 15.260 | 31.481 / 38.593 |
| Events 1 | 1.015 / 8.548 | 56.109 / 65.346 | 0.046 / 1.836 | 33.094 / 39.446 |
| Events 2 | 1.946 / 7.968 | 56.645 / 64.578 | invalid | invalid |
| Events 3 | 0.405 / 7.419 | 54.490 / 63.732 | 0.046 / 0.412 | 33.072 / 38.722 |

Each valid trace contains all 60 ready, request, and assignment events, with no
recorder overflow. Every Android run has 59 unique presentation matches; the
first update has two plausible buffers and is excluded from presentation stats.
Every valid iPhone run has all 60 capture matches. P95 uses nearest rank; no
additional warmup trimming is applied.

Events 2 on iPhone produced an empty capture log and retained Events 1's trace.
That iPhone run is excluded entirely. Its cause was not established. The harness
now deletes the old event trace at startup and records suspend/resume events.
Events 3 produced fresh, complete output. Raw failed-run evidence is retained.
A successful XCTest alone is not sufficient to validate a timing collection.

## Interpretation

Event delivery removes most of the half-frame polling wait. The remaining
Android delay varies: work already running on the owner thread can still delay
a wakeup. Removing this wait did not produce a consistent whole-frame reduction
in display latency. Android medians remain around 55–57 ms; iPhone captures remain
around 31–33 ms. In these runs, iPhone captures were slightly later in event mode.

Android p95 presentation improved from 73–77 ms to 64–65 ms in this comparison.
The preceding Choreographer experiments had polling p95 around 63–64 ms, so this
is not evidence of a universal 10 ms tail improvement. Display deadlines, native
editor work, and run-to-run variation still matter. The robust measured gain is
earlier processing, not a demonstrated large improvement in visible latency.

Over the roughly 55-second recording window, polling performed about 3,300 app
processing passes per device. Events 3 performed 181 on Android and 123 on iPhone,
while Sokol still rendered about 3,300 frames. This confirms that the new app
processing path is event-driven. These counts are not a CPU or battery benchmark.

## Real synchronization and validation

A separate event-driven run through Littlebear received all 60 remote updates on
each phone and matched all 60 to Android presentations / iPhone captures. From
the Datafriend publication callback, after payload import:

| Endpoint | Median | P95 |
|---|---:|---:|
| Android native assignment | 16.969 ms | 18.850 ms |
| Android presentation | 53.925 ms | 59.645 ms |
| iPhone native assignment | 2.033 ms | 2.923 ms |
| iPhone capture | 29.601 ms | 36.290 ms |

These start after import, so they exclude the socket polling wait we removed.
They validate the event-driven network path, rather than measuring total
sender-to-screen latency. No cross-device clock subtraction is used.

Host CRDT/editor tests passed (2/2), and Datafriend tests passed (7/7). The local
production-server integration test passed with 140 channels using readiness and
deadlines only, with a 50-microsecond pump budget. It covers callback growth,
allocation failure, local edits, late subscriptions, reconnect, detach, idle
read interest, disabled/empty connections, and a failed peer's retry deadline.
The fixture required the current channel-capable server binary and a raised
file-descriptor limit; initial fixture failures are not application regressions.

Native adapter checks passed on both phones: Unicode range updates, selection,
empty text, and stale-revision rejection. Android retried four startup revision
races; iPhone needed no retries.

Paired phone UI tests passed: typing, both sync directions, background/resume on
both devices, remote refresh with the iPhone keyboard open, stable editor origin,
iPhone restart/persistence, and deletion. The Android resume test initially hit
a background-launch restriction and then launched a second activity. The driver
now uses a system launch with reorder-to-front/single-top and can inspect the
editor's separate accessibility window. No product change was needed for those
fixture failures. Tests used the isolated timing stores.

Normal event-driven builds were then rebuilt for both devices, with timing and
recording disabled and the regular app stores restored.

## Reproduction and evidence

Build both phones with `--timing --frame-poll`, then use
`crdt-demo-app/bench/schedule_run.py --kind native` and `schedule_analyze.py`.
Repeat with `--timing` alone for event mode. Use `bench/run.py` and `analyze.py`
for real paired synchronization. See the [probe instructions](../../crdt-demo-app/bench/README.md)
and [Flutter comparison harness](../../crdt-demo-app/bench/flutter/README.md)
for driver setup and OCR commands.

[Raw traces, matches, counter crops, test logs, and source hashes](text-events-2026-10-21/)
include every run and its match counts. Run order was Events 1, Events 2, network,
Polling 1, Polling 2, Events 3. No installation overlapped a timed workload.
Diagnostics and ReplayKit are absent from normal builds; timing stores and
identities are separate from the regular app's document.
