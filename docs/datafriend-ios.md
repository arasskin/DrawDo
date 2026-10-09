# iOS demo and Android comparison

2026-10-08. The native demo now builds and runs on Android and iOS. The iPhone
build shares the Datafriend engine, task/particle models, scoped subscriptions,
UI layout/actions, shaders, text texture cache and GPU batching with Android.
Apple adapters provide UIKit/Core Text rasterization, UITextField editing,
safe-area insets and the application's private storage directory. Sokol uses
Metal on iOS and GLES3 on Android.

## Build and portability

```sh
DEVELOPMENT_TEAM=466U33S75F make -C demo-app ios-run
make -C demo-app android-run
```

The team above is the current development machine's team; use your own when
building elsewhere. `IOS_DEVICE` defaults to `Phone`. Subsequent builds reuse
the team in the CMake cache. The iOS output is
`demo-app/build/ios/Release-iphoneos/drawdo_sokol.app`.

The device build uses CMake/Xcode command-line tools and `devicectl`, without
requiring manual Xcode project maintenance. It statically links Datafriend;
other clients retain shared linkage by default. iOS uses AELMDB's POSIX semaphore
backend rather than its Darwin-default System V path. CocoaPods now selects the
same backend. Release flags explicitly set `-O3 -DNDEBUG` for C, C++ and
Objective-C++; the Objective-C++ adapters and Sokol use ARC.

Android and iOS now have functioning app targets. macOS runs the engine/model
checks, but has no configured demo UI target. Windows, desktop Linux and browser
UI builds have not been implemented or validated. The native demo remains local:
Littlebear TCP transport is still provided by the Flutter client, not this shell.
The iOS Keychain implementation is linked, but these local task benchmarks do
not exercise server authentication.

## Measurement

Devices: iPhone 13, iOS 26.5; Moto G7, Android 10. Both physical phones run
optimized ARM64 builds at their native pixel dimensions and approximately 60 Hz.
This comparison includes hardware, OS, compiler and renderer differences; it
cannot isolate an iOS-vs-Android software overhead.

```sh
python3 demo-app/tools/profile_tasks.py --prefix datafriend-crossplatform-android
python3 demo-app/tools/profile_ios.py --prefix datafriend-crossplatform-ios
```

Each phone runs seven workloads at 12 and 1,000 tasks, twice: 28 runs,
360 measured frames after 60 warmup frames, 10,080 measured frames per phone.
Edits occur every sixth frame; scrolling updates every frame. These are the same
scripted callbacks/model actions, query publication checks and seeded task
counts. They use disposable databases; ordinary saved tasks are preserved.
Disk flushing is asynchronous and networking is inactive. Physical touch,
keyboard latency, startup and first-frame compilation are not timed.

Android uses EGL display timestamps. iOS registers a Metal drawable presentation
handler and reads its actual `presentedTime`, with a host-clock offset for the
frame-start comparison. See [Apple's presentation timestamp documentation](https://developer.apple.com/documentation/metal/mtldrawable/presentedtime).
Unreported Apple queue/GPU/latch fields stay at -1; they are not inferred.

"Action" is elapsed time through the callback, blocking Datafriend edit and
publication. "App CPU" is CPU time on the UI/render thread for the entire active
frame, excluding sleep and engine-worker CPU. Rendering/waiting is included in
frame wall time: Metal can wait acquiring a drawable, while Android mainly waits
in `sg_begin_pass`. A 16 ms callback therefore does not mean 16 ms of CPU work.

Comparison uses the first complete matched pass: 14 runs / 5,040 measured
frames per phone. Each edit workload has 60 action samples. Values below are
milliseconds, median / p95; scrolling/idle have no database edit.

| 1,000 tasks | Moto G7 action | iPhone 13 action | Moto G7 app CPU | iPhone 13 app CPU |
| --- | ---: | ---: | ---: | ---: |
| idle | — | — | 2.80 / 3.10 | 0.70 / 0.78 |
| toggle | 1.84 / 2.13 | 0.96 / 2.04 | 3.64 / 4.32 | 1.09 / 1.22 |
| rename | 1.83 / 2.35 | 0.77 / 1.99 | 3.30 / 3.87 | 0.85 / 0.94 |
| filter | 7.38 / 11.56 | 2.22 / 3.89 | 5.13 / 7.28 | 1.31 / 1.44 |
| reorder | 1.38 / 2.38 | 0.99 / 2.04 | 2.68 / 4.50 | 1.11 / 1.21 |
| scroll | — | — | 2.38 / 3.16 | 0.71 / 0.80 |
| add/remove | 3.39 / 6.78 | 1.74 / 3.00 | 5.11 / 7.34 | 1.49 / 1.88 |

Both phones have 100% presentation coverage and zero display intervals over
25 ms in this matched pass: 60.0 Hz on Moto G7, approximately 59.9 Hz on iPhone.
At 1,000 tasks, iPhone app-thread CPU is approximately 2.4–3.9× lower across
these cases. Elapsed edit gains are smaller; engine time includes waiting for
the engine thread. Some iPhone engine p95s are slower than Android, despite
lower UI CPU time. These paced UI workloads do not measure maximum engine
throughput.

| 1,000 tasks | Moto G7 engine elapsed | iPhone 13 engine elapsed | Moto G7 publication | iPhone 13 publication |
| --- | ---: | ---: | ---: | ---: |
| toggle | 0.90 / 1.17 | 0.65 / 1.64 | 0.87 / 1.07 | 0.30 / 0.44 |
| rename | 0.72 / 0.96 | 0.51 / 1.64 | 0.03 / 0.03 | 0.01 / 0.01 |
| filter | 0.90 / 1.04 | 0.47 / 1.94 | 6.39 / 10.54 | 1.76 / 2.23 |
| reorder | 0.68 / 1.08 | 0.66 / 1.66 | 0.70 / 1.25 | 0.33 / 0.43 |
| add/remove | 0.79 / 1.27 | 0.55 / 1.00 | 2.05 / 2.35 | 0.56 / 0.75 |

Display buffering remains: median frame-start-to-display is about 49 ms for
most iPhone workflows (35 ms for the filter run), versus 54–55 ms on Moto G7.
This is not physical touch-to-photon latency. Faster CPU work has not removed
the display queue. Native fonts, resolutions and safe areas differ, so the
pixel output and visible viewport are not identical.

iPhone reported nominal thermal state and Low Power Mode off. Android battery
temperature remained 29.2–29.4 °C across both passes.

| 12 tasks, action | Moto G7 | iPhone 13 |
| --- | ---: | ---: |
| toggle | 1.01 / 1.18 | 0.54 / 1.53 |
| rename | 1.73 / 2.47 | 0.73 / 1.60 |
| filter | 3.17 / 6.14 | 1.14 / 2.28 |
| reorder | 0.91 / 1.11 | 0.54 / 1.56 |
| add/remove | 1.12 / 3.66 | 0.77 / 2.14 |

## Relaunch limitation

Android completed both passes (28 runs / 10,080 frames) at 60 Hz with no >25 ms
gaps. The second iPhone pass had three late interrupted runs: 1,000-task
add/remove and toggle had three >25 ms gaps each, and 12-task rename received no
presentation timestamps. The user reported the app turning black while the
screen stayed on. A keep-awake setting did not resolve every failed relaunch,
so this should not be attributed solely to auto-lock.

All original captures remain in `datafriend-crossplatform-ios*`; focused retries
are in `datafriend-ios-awake-*`. Some retries also received no presentation
timestamps. The runner now rejects incomplete presentation coverage, preserves
raw results, and restores the ordinary app without passing the empty environment
dictionary that `devicectl` rejects.

A sustained diagnostic run had full presentation coverage, approximately 60 Hz,
no GPU errors, and a foreground-active window. The user confirmed the screen
stayed visible when automatic relaunches stopped. The app was restored to its
ordinary interactive task screen. The intermittent black screen during repeated
automated relaunches is **not fully diagnosed**; these runs are excluded from the
headline comparison. The table deliberately uses the complete first pass on
both phones, rather than mixing selected successful retries.

Optional `DATAFRIEND_RENDER_DIAGNOSTICS=1` logs window/scene state and GPU command
errors. It is disabled in normal use and benchmark runs.

## Validation and limits

Five Datafriend tests and three demo tests pass. Both signed app packages install
and launch. The benchmark checks final task counts and selective publication:
rename does not rebuild the list/counter, toggle does not rebuild the All list,
and reorder does not rebuild rows/counter. The iPhone task UI runs through these
rendered workloads; native keyboard interactions have not received a complete
manual device QA pass. These results do not establish particle-simulation limits
or multiplayer/document-editing capacity.

The initial iOS configure attempt failed inside the host sandbox and left empty
C/C++ Release flags in its cache. An exploratory build consequently compiled the
core at `-O0`; those measurements were rejected. The final build was checked at
the compiler-command level for `-O3 -DNDEBUG` and ARC. Provisional captures are
kept only under ignored `demo-app/build/rejected-ios-profiles/` and are excluded
from the comparison.

Raw samples, per-run summaries, source/binary fingerprints and available device
state are under `docs/benchmarks/datafriend-crossplatform-{android,ios}*`.
