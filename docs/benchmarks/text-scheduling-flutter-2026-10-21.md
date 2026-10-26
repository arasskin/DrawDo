# Text scheduling: native C app versus native Flutter release

Physical Moto G7 (Android 10, 60 Hz) and iPhone 13 (iOS 26.5, 60 Hz), measured
2026-10-21. Flutter 3.41.4 stable, framework `ff37bef603`, engine `e4b8dca3f1`,
Dart 3.11.1. Flutter used release/AOT builds; the C app used its optimized `-O3
-DNDEBUG` native build. Android logs report an attempted Vulkan backend followed
by Impeller OpenGLES; iPhone uses Flutter's Metal path.

The Flutter app is a separate benchmark, not a port of the production C app. It
uses a normal Flutter `EditableText`, with native keyboard integration and Flutter
rendering. The C app uses its existing `text_buffers` native editor.

## What was measured

A shared C background-thread producer makes 60 updates ready, 503 ms apart,
following 15 seconds of warmup. Each update changes the counter at the start of a
1,000-byte ASCII document. Both editors use 18-point/dp text, 16-point/dp margins,
white backgrounds, and closed keyboards. This tests replacement of displayed
text; it does not test typing or IME composition.

The C app polls the producer from its Sokol frame callback. Flutter receives a
`NativeCallable.listener` notification and updates its controller. The start time
is the shared producer's actual monotonic timestamp, not its intended timer
wake-up time. This controlled comparison includes event delivery and rendering,
but **excludes networking and CRDT work**. The existing paired sync benchmark was
also repeated separately with additional probes.

Android's endpoint is SurfaceFlinger actual presentation for the editor's buffer
layer. iPhone's endpoint is delivery of the first ReplayKit capture containing the
updated counter, confirmed by OCR. **The iPhone result is not a physical display
presentation timestamp.** Its extra capture latency can differ by rendering path;
it must not be treated as a precise comparison of panel latency.

## Results

All times are milliseconds from producer-ready. The headline table uses the
second run of each variant, with 60 timed updates per device. P95 is nearest rank;
ambiguous Android buffer matches are excluded. All repeats are listed below.

| Variant | Android median / p95 | Matches | iPhone capture median / p95 | Matches |
|---|---:|---:|---:|---:|
| Native API 24 | 62.077 / 70.657 | 59/60 | 32.794 / 39.582 | 60/60 |
| Flutter release | 60.898 / 80.459 | 60/60 | 49.812 / 57.436 | 60/60 |
| Native API 29 | 56.161 / 63.925 | 59/60 | 31.182 / 37.808 | 60/60 |

The API 29 row changes Android only; its iPhone numbers are additional unchanged
native control measurements. Run order was native1 → flutter1 → flutter2 → API29
run1 → native2 → API29 run2. A Flutter-app installation overlapped the first native
iPhone run; that run is retained as evidence, but the headline uses its clean
repeat. No software installs overlapped the headline timed update intervals.

| Run | Android median / p95 | iPhone capture median / p95 |
|---|---:|---:|
| native1 | 62.074 / 70.981 | 33.037 / 39.825 |
| native2 | 62.077 / 70.657 | 32.794 / 39.582 |
| flutter1 | 61.461 / 81.363 | 48.681 / 57.073 |
| flutter2 | 60.898 / 80.459 | 49.812 / 57.436 |
| choreographer | 55.521 / 62.922 | 30.858 / 38.513 |
| choreographer2 | 56.161 / 63.925 | 31.182 / 37.808 |

All six local runs recorded all 60 ready events on both devices, with no recorder
overflow. Both Flutter runs matched all 60 Android buffers and all 60 iPhone
captures; all Flutter engine-frame matches were unique. Native Android runs
`native1`, `native2`, and `choreographer2` each had two plausible buffers for the
first update (300001); these three endpoints were excluded. The other experiment
matched all 60. Every native iPhone update matched a captured frame.

## Findings

The original Android build targets API 24. In the checked-in Sokol implementation,
`#if __ANDROID_API__ >= 29` gates the Choreographer path. Thus the original build
uses its buffer-swap-paced loop on this Android 10 phone. An experimental API 29
build enables the existing Choreographer path without changing the text editor.
The default build remains API 24; `--api 29` is an explicit experiment option and
also declares the APK's minimum OS version correctly.

Across the two experiments, Choreographer moved the native UI handler from
11.96–12.45 ms after the recorded vsync to 3.54–3.61 ms. Presentation improved
by about 6 ms at the median and 7 ms at p95. This is an observed improvement in
this workload, not a universal gain from raising the minimum Android API.

The native producer exposed a half-refresh-scale delay before the C app sees a
ready update. Flutter's notification reaches Dart in under 1 ms at the median,
but its controller assignment does not include text layout, painting, rasterization,
or presentation. Comparing controller mutation alone with `Editable.replace()`
would incorrectly attribute deferred work to a performance gain.

On Android, probes inside the actual adapter show that most of its approximately
13–14 ms assignment block is spent inside `Editable.replace()`. The replacement
call includes platform work triggered by the edit; this experiment does not break
that down into shaping, layout, span updates, or other internal costs. The native
draw call itself takes about 1.5 ms. Flutter's corresponding frame performs about
18 ms of build/layout/paint and 12 ms of raster work on this phone.

The real paired sync rerun produced 56 individually observed Android receives,
all matched to one compositor frame: receive callback to presentation median
54.919 ms, p95 56.024 ms. Replacement itself was 11.925 ms median; UI handoff was
1.584 ms. On iPhone, 60 receives were recorded and 58 matched captured frames:
median 23.222 ms, p95 24.348 ms. Intermediate versions can coalesce, and OCR/capture
can miss observations; this run does not isolate the reason for every missing
state. Missing tags were not assigned invented timings.

## Interpretation

Flutter's event delivery avoids the frame-poll wait, but its measured total latency
is not automatically lower. The Choreographer experiment is evidence that aligning
our existing native-editor pipeline more closely with display timing can improve
Android latency without replacing native text rendering.

A useful next experiment is to wake processing when data becomes ready, rather
than waiting for the next render frame, while preserving one owner of app/editor
state. That could reduce the measured polling delay. Its benefit to *presentation*
needs another measurement: removing work or a queue does not necessarily cross a
display deadline. Optimizing Android's replacement-triggered platform work is a
separate opportunity.

## Evidence and reproducibility

[Benchmark source and instructions](../../crdt-demo-app/bench/flutter/README.md)
cover building, device collection, probes, and matching rules.
[Raw traces, frame matches, and OCR crops](text-scheduling-flutter-2026-10-21/)
retain the measurements. Each update's candidate count is preserved. Android
matches with multiple plausible buffers are excluded. Counts and outliers are
reported; no blanket warmup trimming is applied to the 60 timed updates.

The first Flutter Android collection incorrectly shell-quoted its layer name.
After the run, its complete 64-frame history was recovered from SurfaceFlinger's
127-frame buffer. All 60 updates then matched uniquely. The second run used the
corrected collector throughout and reproduced the result.

Flutter's engine clock and C `CLOCK_MONOTONIC` have different epochs on iPhone.
A native/Dart/native clock sandwich estimates the offset; a unique engine
build-finish within 2 ms of the tagged framework callback identifies each frame.
Internal Flutter build/raster durations use engine-clock differences directly.
The iPhone engine-clock mapping is approximate and is not used to compute the
capture endpoint. Neither `rasterFinish` nor a commit callback is treated as proof
of display presentation.

Android's continuous Choreographer diagnostic callback is present in both apps
and can perturb scheduling. ReplayKit also adds overhead. Devices were tested
serially, without randomized order or thermal control. These are small workload
experiments, not general framework performance claims.

## Final workspace/device state

The benchmark and timing probes are retained for reproduction. Default Android
API compatibility remains 24. Both normal C apps were rebuilt with timing OFF
and restored to their ordinary stores. The separate Flutter benchmark remains
installed. Existing host model/editor tests passed (2/2), Flutter static analysis
passed, and all device benchmark builds succeeded. No server changes were made.
