# Native Flutter text scheduling comparison

A separate, release/AOT Flutter app for the physical Android and iOS phones. It is
not a rewrite of the C app and contains no Datafriend, networking, or CRDT code.

The same C producer (`../probe.c`) makes 60 updates ready on a background thread,
after 15 seconds of warmup, 503 ms apart. Each update changes the eight-character
counter at the start of a 1,000-byte ASCII document. Flutter receives a
`NativeCallable.listener` notification and sets an `EditableText` controller. The
C app's timing role 3 polls the same producer in its normal Sokol frame callback
and updates the real `text_buffers` editor. Both use an 18-point/dp field with
16-point/dp margins, the keyboard closed, and no focused cursor animation.

This deliberately compares event delivery plus rendering. The C app waits for its
next frame; Flutter's isolate receives a native notification. This is a controlled
model of an already available update, **not a network-sync benchmark**. String
construction is included after the request marker. The Flutter assignment marker
only measures controller mutation; layout/paint and rasterization follow later.
It must not be compared alone with native `Editable.replace` work.

## Build

```sh
cd crdt-demo-app/bench/flutter
flutter pub get
flutter analyze
flutter build apk --release --target-platform android-arm64
flutter build ios --release
```

Install `build/app/outputs/flutter-apk/app-release.apk` on Android and
`build/ios/iphoneos/Runner.app` on iPhone. Android uses the bundled NDK CMake target;
iOS compiles the same probe and the same ReplayKit crop recorder as the C app.
The apps have separate bundle IDs: `dev.drawdo.text_latency` (Android) and
`dev.drawdo.textLatency` (iOS). They don't access normal app documents.

## Collect

Build the XCTest project in `../schedule-ios` using the same XcodeGen/signing flow
as `../ios`. Use scheme/project `SchedulingTest`, with derived data in the driver's
`build/` directory. Then, from the repository root:

```sh
python3 crdt-demo-app/bench/schedule_run.py --kind flutter \
  --driver /tmp/crdt-scheduling-driver --output /tmp/flutter-run
/tmp/crdt-read-frames /tmp/flutter-run/ios > /tmp/flutter-run/ios-tags.csv
python3 crdt-demo-app/bench/schedule_analyze.py /tmp/flutter-run --flutter
```

For the C app, install timing builds and use `--kind native`, without `--flutter`
on the analyzer. The collector sets role 3. `build_android.py --timing --api 29`
enables Sokol's Choreographer path, now the app's default. Use `--api 24` to reproduce
the original buffer-swap-paced baseline. The API flag also updates the APK minimum OS.

The scripts target the development phones and signing team explicitly. Keep both
phones foregrounded and unlocked. Each run takes about a minute after launch.
After collection, restore both C apps with their ordinary non-timing build scripts.

## Measurement and limitations

- `probe.c` records every event in the receiving device's `CLOCK_MONOTONIC` domain.
  Android `System.nanoTime()` uses that domain too. The recorder is fixed-capacity
  and reports overflow; do not accept a run with overflow or missing ready tags.
- Android polls the *buffer-bearing* native popup or Flutter SurfaceView layer in
  SurfaceFlinger. Layer names containing spaces must be shell-quoted for adb.
  Native matches must have one submission within one refresh after `onDraw`.
  Flutter matches must have one ready buffer within 50 ms of framework frame
  completion. Ambiguous matches are reported and excluded. This is timestamp
  matching, not per-buffer OCR.
- iPhone uses identical ReplayKit crop capture and OCR. The timestamp is callback
  delivery of the first frame containing the update. It includes capture overhead
  and is **not physical presentation**, nor equivalent to the Android endpoint.
- FrameTiming timestamps have a different epoch from the C clock on iPhone.
  A native/Dart/native clock sandwich estimates the offset. A unique build-finish
  timestamp within 2 ms of the tagged framework callback identifies a Flutter
  frame. Internal build/raster durations subtract engine timestamps directly;
  cross-clock scheduling estimates are approximate. No Flutter raster-finish
  timestamp is treated as display presentation.
- Android records a continuous Choreographer callback during both variants. This
  diagnostic callback can perturb scheduling. Neither app is being measured
  without instrumentation. iPhone capture itself can perturb rendering.
- This workload covers remote-style text replacement with the keyboard closed;
  it does not measure typing, IME composition, scrolling, large documents, or
  platform/framework-wide performance.

Extra event stages: 9 producer ready; 10 Sokol frame start; 11 Android vsync time;
12 actual vsync callback; 13 native pre-draw; 18 native draw start; 20–24 Flutter
vsync/build start/build end/raster start/raster end; 31–33 native replacement
start/end/batch end; 40–42 clock calibration. Existing stages 1–8 are described in
`../README.md`. Flutter stage 6 is the framework's persistent callback after its
rendering callback, not a native draw call. Trace tags 999998/999999 identify loop
and vsync probes; tags 800000+ identify Flutter engine frames.
