# CRDT demo: receiving update to native text

Measured 2026-10-21 on the physical Moto G7 (Android 10) and iPhone 13 (iOS 26.5),
using the minimal Sokol-loop app and shared `text_buffers` native controls.
60 received updates per device, 1,000-byte ASCII text, keyboard closed, app foregrounded.
Each sender changed the first-line counter at 503 ms intervals, in separate phases,
through the existing Littlebear server at 147.182.181.182:7777.

All numbers below are milliseconds from entry to the receiving Datafriend watch
callback. Socket receipt, network polling and database import before the callback
are excluded. These are post-sync UI timings, not device-to-device latency.

| Endpoint | Android median | Android p95 | iPhone median | iPhone p95 |
|---|---:|---:|---:|---:|
| CRDT merge and local publish complete | 0.807 | 1.080 | 0.306 | 0.337 |
| Native update requested | 0.959 | 1.614 | 0.331 | 0.362 |
| Native editor starts handling request | 2.826 | 3.600 | 0.356 | 0.394 |
| Native text/selection assignment complete | 22.936 | 25.452 | 7.056 | 8.481 |
| Native `onDraw` complete | 30.401 | 33.173 | — | — |
| Native frame commit callback | 36.752 | 40.725 | — | — |
| OS compositor presentation | **54.278** | **70.428** | — | — |
| First captured frame containing new text delivered | — | — | **23.807** | **24.388** |

The last two rows have different measurement boundaries and should not be treated
as a direct platform speed comparison. Android presentation ranged 42.828–71.855 ms;
iPhone captured-frame delivery ranged 22.811–25.416 ms. P95 uses nearest rank.

The expensive application-side step is replacing text in the native control:
measured directly from native begin to native set, median 20.043 ms on Android and
6.713 ms on iPhone. The current remote-refresh path assigns the entire string.
CRDT merge/publish is under 1 ms at the median on both phones; these results do not
suggest that reconciliation is the cause of the post-sync UI delay.

## Evidence and limits

Android uses the actual `PopupWindow` buffer surface containing the EditText,
not Sokol's white-background surface. SurfaceFlinger reports desired, actual-present
and ready timestamps in that order ([AOSP FrameTracker](https://android.googlesource.com/platform/frameworks/native/+/8c9cd3c537f7d4781862e2d95b32247fa5bb3d66/services/surfaceflinger/FrameTracker.cpp)).
Each of the 60 tagged draws matched exactly one buffer submission within one refresh
interval, 3.462–7.514 ms after draw completion; all 60 presentation timestamps were
unique. Commit callbacks alone do not prove visibility ([Android ViewTreeObserver](https://developer.android.com/reference/android/view/ViewTreeObserver)).

iPhone Instruments recording failed with the installed Xcode 26.0.1. Instead,
benchmark-only ReplayKit capture saved small crops around the changing counter,
then offline Vision OCR matched all 60 received IDs to captured pixels. We used
capture callback arrival on the same monotonic clock as the receiving callback.
The raw video PTS has a different epoch and was not used for subtraction.
ReplayKit supplies screen sample buffers ([Apple startCapture](https://developer.apple.com/documentation/replaykit/rpscreenrecorder/startcapture(handler:completionhandler:))); this endpoint includes capture delivery overhead
and is **not a measured physical display presentation time**. Capture itself can
perturb performance. No iPhone presentation p95 is inferred from these results.

The first driver run was discarded because the app remained inactive after capture
permission. The valid driver explicitly reactivated it. The accepted run has 60/60
receives and 60/60 frame matches on both devices, zero recorder overflow and no
rejected native updates. The iOS layout hook did not fire for these refreshes;
its absence is not interpreted as zero layout cost.

Timing used a separate identity alias and separate local stores/namespaces. The
regular documents were not edited. Both phones were rebuilt, installed and launched
without timing/recording afterward. Both existing CRDT demo tests passed; their
sandboxed run could not open database resources, and the normal local run passed.

Raw traces, per-update matches, summary, OCR labels, cropped frames and source hashes
are in [the evidence directory](crdt-native-latency-2026-10-21/).
Reproduction and stage definitions are in [the probe README](../../crdt-demo-app/bench/README.md).
