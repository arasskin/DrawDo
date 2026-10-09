# Datafriend demo app

A native task list, particle simulation and shared-document demo using C++/Sokol (GLES3 on Android, Metal on iOS) and the production
Datafriend engine. Installed as **Datafriend demo** (`dev.drawdo.sokol`). No Flutter,
Dart runtime, Gradle, or Android Studio is used by these builds.

From the repository root:

```sh
make -C demo-app android       # Compile and package a development APK
make -C demo-app android-run   # Build, install on connected phone, launch
make -C demo-app android-logs  # Follow app/native errors and diagnostic samples
```

Use `ANDROID_SERIAL=ZY227364TP` when more than one device is connected.
The APK is `demo-app/build/android/drawdo-sokol.apk`.

For an iPhone, with Xcode, a paired device in Developer Mode, and a development
signing identity:

```sh
DEVELOPMENT_TEAM=466U33S75F make -C demo-app ios-run
# Subsequent builds reuse the team cached in build/ios:
make -C demo-app ios
python3 demo-app/tools/profile_ios.py --device Phone
```

Set `DEVELOPMENT_TEAM` to your own Apple team and `IOS_DEVICE` to the device name
or identifier. The iOS build uses CMake/Xcode command-line tools, an optimized
Release binary, static Datafriend linkage, and `devicectl` installation/launch.
Its output is `build/ios/Release-iphoneos/drawdo_sokol.app`. Requires iOS 15+;
currently validated on iPhone 13. No Xcode GUI project maintenance is needed.
The first signing build permits Xcode to prepare the development provisioning
profile. This is a device build, not an App Store distribution archive.

## Source layout

| Path | Responsibility |
| --- | --- |
| `src/main.cpp` | App lifecycle, navigation and frame submission |
| `src/task_model.h`, `src/task_screen.h` | Datafriend task state/actions and task UI |
| `src/particle_model.h`, `src/particle_simulation.h` | Datafriend particle controls and shared simulation |
| `src/datafriend_ui.h` | Typed queries, scoped subscriptions, edits and UI publication |
| `src/render/` | Text interface/cache, shaders and Sokol implementation |
| `src/platform/` | Text raster/input, insets and lifecycle services; Android and Apple adapters |
| `src/profiling/` | Optional phase counters and rendered task benchmark |
| `android/`, `ios/` | Android activity/resources and iOS bundle metadata |
| `tools/` | Direct SDK build/deployment and phone profiling |
| `tests/` | Platform-independent task, particle and cache checks |

The demo calls the production `datafriend::datafriend` target directly on its
application thread. Subscriptions return their ID and shared live map together;
only disk flushing runs in the background. Datafriend never
includes demo code. The archived, unsupported Flutter comparison adapter uses `particle_simulation.h`
without linking Sokol. The generic subscription dispatcher and typed UI helper
are consolidated in `datafriend_ui.h`; models do not depend on Android/rendering.
The separate Sokol implementation file is required for its implementation macros
and avoids compiling the backend once per application translation unit.

Use `make -C demo-app test` for host tests. The unsupported Flutter reference remains in
[`src/app`](../src/app/README.md). See [the dependency map](../architecture.visualize).

## Build pipeline

`tools/build_android.py` runs CMake/Ninja with Android NDK Clang, `javac`/`d8` for the
small platform adapter, `aapt2` for resources/manifest, and `zipalign`/`apksigner`
for packaging. `adb install --no-incremental -r` installs updates, then the
activity is restarted. C++ is optimized (`-O3`) with symbols retained on the
host; debug information is stripped from packaged libraries. APK signing and
16 KiB native-library alignment are verified before deployment.

Native compilation is incremental. Java/DEX and resources are independently
cached by inputs; unchanged packages are reused. No-op builds do not re-sign.
The development signing key is generated at `demo-app/.android-debug.keystore`
(ignored by Git), so normal build-directory cleanup preserves update identity.

Defaults match the current development machine: Android SDK Build Tools 35.0.0,
platform android-35, NDK 28.2.13676358, ARM64/API 24 minimum, macOS host. Set
`ANDROID_HOME`, `ANDROID_NDK_HOME`, `JAVA_HOME` as needed. CMake, Ninja, Python 3
and a JDK must be installed. If replacing the NDK, start with a fresh native
CMake build directory. The output is a development APK, not a Play Store bundle.

## Task list

The app opens a persistent task list with add/edit, completion, filters, scrolling,
drag reordering and a shared detail panel. Tap **Particles** to open the simulation.
Queries and direct edits follow the Dart demo's ergonomics using C++ scoped
subscriptions. See [native task-list design and checks](../docs/datafriend-native-tasks.md).

## Particle runtime

- 96 fixed spatial queries over particle x/y/region facts.
- One blocking edit per tick, asynchronous Datafriend disk flushing.
- Local 3×3 patch or all-moving modes, four particle counts, pause/resume and drag.
- Fresh demo simulation on launch. Count changes atomically replace particle facts
  in the same database, preserving controls and live query handles. Its disposable
  store is inside this application's private `particle-session` directory.
- Datafriend owns control state as well as particle state. C++ owns simulation
  calculations, query-derived UI caches, layout, touch routing and rendering.
- A small Java adapter reports system insets and uses Android StaticLayout to
  rasterize uncached text labels. A content-keyed LRU uses a 100 MB texture-data
  budget, shared across drawing positions and tints. Text, pixel size and clipping width
  identify a raster; unchanged content never crosses JNI. Current-frame textures
  stay pinned until draw submission, and offscreen entries remain reusable.
- Shapes and particles share one batch. Cached text uses textured quads following
  the recovered junstodos renderer's design. The native simulation is shared with
  the earlier matched benchmark through `src/particle_simulation.h`.
- The screen reports recent render-callback rate and the last synchronous
  simulation step, not GPU/presentation timing. This is not a rigorous new FPS
  benchmark. Native initialization uses a C++ RNG, so the interactive scene is
  not an identical-seed comparison with the Flutter demo.

The shell includes the task UI and system text editing on Android and iOS.
Both use the same text-cache/GPU drawing code; Apple rasterization uses UIKit /
Core Text and editing uses UITextField. Insets respect the iPhone notch and home
indicator. Canvas accessibility remains follow-on work. Native document networking is
implemented; see the document-sync section below. The Datafriend engine algorithms are unchanged; iOS selects POSIX rather
than System V semaphores for AELMDB. The recovered Apple implementation remains
available in `../reference/junstodos`. JNI is used for Android system services,
not app logic.

## Datafriend-driven particle controls

`src/particle_model.h` defines the application model. Pause, count, movement mode, automatic
patch movement and patch coordinates are six facts on a controls entity, using
IDs 26–31 in the shared attribute schema. Particle x/y/region facts share the
same engine. No parallel writable button state or pending action queue exists.

`src/datafriend_ui.h` supplies `watch(query, builder)`. It subscribes once and invokes
the builder initially, then only when a blocking edit returns that query handle.
Builders read the already-published result and refresh presentation caches; they
do not issue recursive edits. The native control pattern is:

```cpp
buttons[6].press = [this] {
  set("simulation/paused", boolean(!state.paused));
};
views.watch(settings_query({"simulation/paused"}), [this](const auto& result) {
  auto& button = buttons[6];
  button.selected = number(result, "simulation/paused");
  button.label = button.selected ? "Resume" : "Pause";
});
```

The seven buttons have narrow subscriptions: four for count, two for mode, one
for pause. A separate settings view drives the active patch and status label.
Particle changes route to the 96 cell views without rebuilding any button views.
Dragging stores both coordinates and disables automatic movement in one edit.
Automatic patch advancement is batched with particle movement in the same edit.

Count changes replace all particle facts and the count setting in one transaction,
then refresh affected caches before returning to the button callback. The same
engine, query handles and shared result maps survive. Pause, mode and manual patch
placement survive count changes. Even an already-selected count button reseeds
particles; unchanged setting views receive no notification.

Sokol still draws the scene every frame. GPU resources, touch capture, layout,
frame clocks and measured performance samples are transient renderer/diagnostic
state, not database facts. Static labels and geometry are code. Persisted facts
are the source of truth for interactive application state; UI caches are derived.
The demo deliberately starts fresh on process launch, even though the controls
are stored and disk flushing is asynchronous.

Checks:

```sh
cmake -S demo-app -B demo-app/build/tests -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build demo-app/build/tests -j 4
ctest --test-dir demo-app/build/tests --output-on-failure
python3 datafriend/tool/generate_attributes.py --check
```

The text-cache test checks content reuse, scrolling/reordering, bounded eviction,
current-frame pinning, byte accounting, growth past 96 entries, raster keys and
failed-build recovery.
The integration test checks synchronous/selective publication, unchanged-view
identity across count replacements, coherent count/particle publication, paused
frames doing no database work, automatic patch batching and control persistence
across database reopen. Device validation covers all controls, drag and suspend/
resume. Logs include builder counts so drawing a frame is not confused with
rebuilding a query-bound component. See
[device checks](../docs/validation/sokol-datafriend-ui-device.txt) and
[screen](../docs/benchmarks/datafriend-sokol-ui-android.png).

## Phone profiling

```sh
make -C demo-app android-run
python3 demo-app/tools/profile_tasks.py
```

Runs 12- and 1,000-task workloads twice with real rendered frames and EGL
presentation timestamps. `tools/profile_ios.py` runs the same workloads with
Metal drawable presentation timestamps. Measures edits, subscriptions/callbacks, text/JNI,
graphics submission and app-thread CPU time. All counters are disabled during
ordinary use. Profiling uses a separate private `tasks-profile` database and
restores the ordinary task screen after the run; saved tasks are preserved.
Raw samples, summaries, device state and source/APK hashes go in
`docs/benchmarks/datafriend-native-task-profile*`.

Use `--counts 1000 --cases idle scroll --rounds 1` for a focused run, with a
separate `--prefix` to preserve previous measurements. Physical touch delivery,
keyboard latency and network activity are outside this benchmark. iOS profiling
keeps the display awake and rejects incomplete presentation timestamps. Repeated
automated iPhone relaunches have an intermittent black-screen issue still under
investigation; sustained running is stable in the checked session.
See [Android findings](../docs/datafriend-native-task-profile.md) and
[iPhone comparison](../docs/datafriend-ios.md).

## Shaders and provenance

Sokol headers are pinned in `../third_party/sokol` with original embedded licenses
and import hashes. Lifecycle and textured-quad rendering were adapted from
junstodos revision `f5cd92a`. `shaders.glsl` extends that app's shaders.

Generated shaders are checked in, so ordinary builds need no shader compiler.
When changing shader source, use the compatible `sokol-shdc` recorded in
`shaders.json`:

```sh
cd demo-app/src/render
/path/to/sokol-shdc -i shaders.glsl -o shaders.h -l metal_sim:metal_ios:glsl300es
```

Update the source/header SHA-256 entries in `shaders.json` after regeneration.
The build refuses changed source paired with stale generated shaders.


## Shared document over Littlebear

Both phones use Datafriend's native nonblocking TCP client and securely stored
Ed25519 identities. The app implements the sequence CRDT; the database/server
transport opaque immutable operations. Each phone owns one namespace and reads
its peer's namespace. Local edits are atomic, disk flushing and networking remain
asynchronous, and published results are applied on the app thread.

Build/install with the usual Android/iOS commands and launch both apps once to
create their identities, then pair them from the development machine:

```sh
python3 demo-app/tools/sync_phones.py --host 147.182.181.182 --port 7777 --case interactive --seed 0
```

`Type / edit` opens a native multiline draft editor. Committed IME changes are
sent while typing; the shared preview below reflects both writers. Remote inserts
are preserved if the local editor's draft is stale. This is a basic text-CRDT demo,
not a rich-text editor with shared cursors or remote caret transformation.
`Go offline` disconnects that phone while local edits continue; `Reconnect`
resumes synchronization. The top-right button cycles document → particles → tasks.
Existing task data is preserved in `tasks-v1`.

Scripted `typing`, `concurrent` and `offline` cases exercise 1,000-character
(default) documents. Reports include edit component timings, optional calibrated one-way
latency, native transport counters and actual EGL/Metal presentation timestamps.
The script checks identical final text/operation counts, local quiescence and
presentation coverage. See [results and limits](../docs/datafriend-document-sync.md).

Document source lives in `src/document_{crdt,model,screen}.h`; native transport
lives in Datafriend. `document_test` tests CRDT ordering/Unicode/stale edits;
`document_network_test HOST PORT` checks two real native models over TCP.

The CRDT is now pinned **Yrs 0.24.0**, accessed through `libyrs.h` and statically
linked into the app. It requires Cargo/Rust (plus `aarch64-linux-android` and
`aarch64-apple-ios` Rust targets for phone builds). CMake handles the library
build; dependencies download on the first build. Datafriend remains a C++ library.
The UI projects just `document/text` for entity 1; replication uses one encoded
`document/state` fact per owner. See [design and measurements](../docs/datafriend-document-yrs.md).
