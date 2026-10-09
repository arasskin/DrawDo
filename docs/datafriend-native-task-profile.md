# Native task-list phone profile — 2026-10-08

Follow-up: [content-keyed texture reuse and matched results](datafriend-text-texture-reuse.md).
The measurements below describe the original cache, before that optimization.

Ordinary edits maintained 60 Hz presentation. Continuous scrolling and rapid
filter switching exposed text-cache stalls. Datafriend was not the dominant cost
in those slow frames. No model correctness failure occurred.

## Method

Moto G7, Android 10, 1080×2270, 60 Hz; optimized arm64 C++/Sokol/GLES3 with the
production Datafriend engine, asynchronous disk flushing and no networking.
Two shuffled rounds of seven workflows at 12 and 1,000 tasks: **10,080 measured
frames**, plus 60 warmup frames per run. Battery temperature stayed 29.2–29.4°C.
CPU clocks were not pinned. Source/APK hashes and device state accompany samples.

Edits occur every sixth frame, roughly ten per second at 60 Hz. Scrolling moves
three logical pixels per frame over repeated 360-pixel traversals. Checkbox,
filter and reorder use actual view handlers. Rename uses the editor's model
completion method; add/remove operates outside the viewport. Physical touch
delivery, IME latency, startup, long-distance reorder and networking are excluded.

Counters are disabled during normal use. Profiling uses disposable private
`tasks-profile` data and restores the ordinary `tasks-v1` app afterward.

## Results

Times are pooled **median / p95 milliseconds**. Action includes the blocking
edit and UI callbacks. Frame includes action, layout/text, draw submission and
the diagnostic revision read; it excludes the subsequent `eglSwapBuffers`.
Frame wall time includes driver waiting and is not CPU or GPU execution time.
Display timings come from EGL presentation fences.

| Tasks | Workflow | Action ms | Frame ms | Presented FPS, rounds 1 / 2 | Display gaps >25 ms |
|---:|---|---:|---:|---:|---:|
| 12 | Idle | — | 12.31 / 13.83 | 60.0 / 60.0 | 0 |
| 12 | Toggle | 1.00 / 1.31 | 20.16 / 21.91 | 60.0 / 60.0 | 0 |
| 12 | Rename | 1.99 / 2.56 | 18.63 / 21.49 | 60.0 / 60.0 | 0 |
| 12 | Reorder | 0.99 / 1.25 | 23.02 / 27.25 | 60.0 / 60.0 | 0 |
| 12 | Add/remove | 1.09 / 3.18 | 11.23 / 13.86 | 60.0 / 60.0 | 0 |
| 12 | Filter | 4.08 / 6.94 | 53.40 / 64.37 | 51.1 / 50.1 | 72 |
| 12 | Scroll | — | 12.60 / 15.60 | 57.4 / 57.4 | 17 |
| 1,000 | Idle | — | 12.33 / 13.84 | 60.0 / 60.0 | 0 |
| 1,000 | Toggle | 1.92 / 2.26 | 19.90 / 23.25 | 60.0 / 60.0 | 0 |
| 1,000 | Rename | 2.06 / 2.59 | 18.83 / 21.21 | 60.0 / 60.0 | 0 |
| 1,000 | Reorder | 2.11 / 2.39 | 25.43 / 28.60 | 60.0 / 60.0 | 0 |
| 1,000 | Add/remove | 3.13 / 6.49 | 12.59 / 16.17 | 60.0 / 60.0 | 0 |
| 1,000 | Filter | 9.11 / 12.87 | 52.34 / 59.51 | 50.7 / 51.4 | 81 |
| 1,000 | Scroll | — | 12.49 / 21.26 | 56.4 / 56.4 | 25 |

Gap counts pool 718 inter-presentation intervals per row. Frame statistics use
action frames, except idle/scroll which use all frames. Scroll's median/p95 can
hide infrequent expensive row boundaries: its worst callback was **72.1 ms**,
with a largest display gap of **66.8 ms**. Filter switching reached an **83.5 ms**
display gap. Previously queued frames can absorb isolated long callbacks;
60 Hz here does not imply these edits can run every frame without jank.

## Cost breakdown

Median action phases for 1,000 tasks, in milliseconds:

| Workflow | Blocking engine call | UI publication | Entire action |
|---|---:|---:|---:|
| Toggle | 0.93 | 0.94 | 1.92 |
| Rename | 0.83 | 0.02 | 2.06 |
| Reorder | 0.94 | 1.10 | 2.11 |
| Add/remove | 0.84 | 1.98 | 3.13 |
| Filter | 0.91 | 8.05 | 9.11 |

The engine measurement includes owner-thread dispatch, transaction, indexes,
queries and results. UI publication invokes builders; filter changes also
destroy/create subscriptions. Subscription timers overlap publication or
viewport preparation and must not be added twice. Rename creates a temporary
row query for stale-editor protection; add queries the last order and allocates
an entity. Component medians need not sum to the median total.

Counter builders scan their result, and list builders reconstruct sorted IDs;
these account for increased publication cost on large lists. Filter switching
recreates the list query and several row subscriptions.

**Text is the primary bottleneck.** The cache uses drawing-slot number, text,
size and width. Scrolling shifts existing titles into different slots, causing
up to 16 labels to rerasterize at a row boundary. Changing the visible row count
also shifts detail/footer slots. Swapping two rows rerasterizes four labels in
this alternating-done workload, despite causing no row-query rebuilds.

A focused four-run capture adds finer timers without changing algorithms:

| Active-frame median ms, 1,000 tasks | Reorder | Filter |
|---|---:|---:|
| JNI attachment | 2.50 | 5.64 |
| Java text layout / bitmap drawing | 14.49 | 28.06 |
| Bitmap access / pixel copy | 0.58 | 1.60 |
| Texture replacement/upload | 1.13 | 3.22 |
| JNI detachment | 0.85 | 2.14 |
| Entire text preparation | 20.12 | 41.67 |

First try reusing textures by content or stable label identity, independent of
drawing order. Keeping the render thread attached to JNI, or batching under one
attachment, can remove additional overhead. A glyph atlas is not required to
fix the cache invalidation. These optimizations have not been implemented here.

**Smoothness and latency differ.** Idle preparation is about 0.19 ms; total
callback wall time is about 12.3 ms, but app-thread CPU is only 2.7–2.9 ms.
The focused capture locates **10.13 ms** in `sg_begin_pass`, versus **0.28 ms**
in subsequent shape submission. This indicates substantial waiting;
driver/surface backpressure is the likely explanation. No GPU execution timer
or scheduler trace was collected, so this is not an attribution to a particular
GPU operation. Engine-worker CPU is excluded from app-thread CPU.

Median render-dispatch-to-scanout is roughly **54 ms** at idle and on ordinary
edits, and **71–72 ms** on filter actions, using
[EGL display-present timestamps](https://registry.khronos.org/EGL/extensions/ANDROID/EGL_ANDROID_get_frame_timestamps.txt).
This excludes input delivery; it is not touch-to-photon latency. Frame pacing
and queue depth warrant a separate experiment after text-cache maintenance.

The diagnostic `revision()` read also dispatches to the engine every frame,
costing roughly **0.4–0.5 ms** at idle. It is included in measurements, with log
writes suppressed. Queries do not require this read. Continuous idle drawing
also remains; power consumption was not profiled.

## Validation and reproduction

All 28 matrix and four focused runs passed model/count checks. EGL coverage was
100%, with distinct presentation timestamps. Raw counters confirm one edit per
action. Rename only rebuilt row/detail views; completion additionally rebuilt
the counter; reorder rebuilt the list without row/counter rebuilds. Both native
test suites and the generated-attribute check passed. The regular app was
restored with saved tasks intact. No blocker remains for profiling.

```sh
make -C demo-app android-run
python3 demo-app/tools/profile_tasks.py
python3 demo-app/tools/profile_tasks.py \
  --prefix datafriend-native-task-text-profile --counts 1000 \
  --cases idle reorder filter scroll --rounds 1
```

The runner now emits the finer counters too; historical matrix captures predate
those additions. Timers/EGL polling can perturb execution. These short warmed
runs do not characterize long-session thermal, GC or lifecycle stalls.

Artifacts: [matrix summaries](benchmarks/datafriend-native-task-profile.json),
[raw frames](benchmarks/datafriend-native-task-profile-runs),
[device state/hashes](benchmarks/datafriend-native-task-profile-metadata.json),
[fine summaries](benchmarks/datafriend-native-task-text-profile.json),
[fine frames](benchmarks/datafriend-native-task-text-profile-runs),
[fine metadata](benchmarks/datafriend-native-task-text-profile-metadata.json).
