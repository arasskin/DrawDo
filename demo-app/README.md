# Datafriend demo app

A C99/Sokol task list, particle simulation and shared-document app for Android
(GLES3) and iOS (Metal). Installed as **Datafriend demo** (`dev.drawdo.sokol`).
The app calls Datafriend's C API directly. Neither app nor database requires a
C++ runtime. Small Objective-C adapters serve iOS, Java serves Android text and
system services, and the document CRDT remains the pinned Rust Yrs dependency.

## Build and run

From the repository root:

```sh
make -C demo-app android       # Build and package
make -C demo-app android-run   # Build, install, launch on the connected phone
make -C demo-app android-logs
make -C demo-app test
```

Use `ANDROID_SERIAL=ZY227364TP` when multiple devices are connected. The APK is
`demo-app/build/android/drawdo-sokol.apk`. Existing app data and signing identity
are preserved during updates. The build uses the Android SDK, NDK, CMake/Ninja,
Python and a JDK directly; it does not require Gradle or Android Studio.
Defaults are SDK Build Tools 35.0.0, platform android-35, NDK 28.2.13676358 and
ARM64/API 24. Override `ANDROID_HOME`, `ANDROID_NDK_HOME` and `JAVA_HOME` as needed.

For an iPhone with Developer Mode, Xcode and a development signing identity:

```sh
DEVELOPMENT_TEAM=your_team make -C demo-app ios-run
# Later builds reuse the cached team:
make -C demo-app ios
```

Set `IOS_DEVICE` to select a device. Output is
`build/ios/Release-iphoneos/drawdo_sokol.app`, targeting ARM64/iOS 15+.
The C99 port has an unsigned iOS build check; its physical-device validation in
this change is Android. Older iPhone validation records describe the C++ app.

C code is optimized with `-O3`; host debug symbols are retained while APK
libraries are stripped. Packaging verifies signing and 16 KiB alignment.
Native code, Java/DEX and resources have separate incremental caches. The
ignored `.android-debug.keystore` persists across build-directory cleanup.
The APK contains `libdrawdo_sokol.so` and `libdatafriend.so`, without libc++.

## Module boundaries

| Module | Responsibility |
| --- | --- |
| `src/main.c` | App lifecycle, navigation, frame submission; orchestrates the modules |
| `src/base.h`, `src/base.c` | Small buffers, checked growth, byte codec, clocks and file helpers |
| `../text_buffers/` | UTF-8 text buffers, selection/composition and app-owned native editor sessions |
| `src/datafriend_ui.h`, `src/datafriend_ui.c` | Typed C values/queries, subscriptions, atomic edits, synchronous publication |
| `src/task_model.c`, `src/task_screen.c` | Persisted tasks, narrow row queries, layout and gestures |
| `src/particle_model.c` | Particle physics, 96 cell queries and persisted controls |
| `../text_crdt/crdt.c` | Yrs ownership, UTF-8 edits, snapshots and independent editor drafts |
| `src/document_model.c`, `src/document_screen.c` | Owner/peer stores, TCP sync, shared document UI |
| `src/render/` | Content-keyed text LRU, batched GPU drawing, shaders, Sokol implementation |
| `src/platform/` | App-specific text rasterization, insets, logging and presentation services |
| `../accessible_ui/`, `../clipboard/`, `../haptics/` | Reusable C interfaces with native iOS/Android implementations |
| `src/profiling/` | Optional task/frame measurements and clock calibration |
| `tests/` | C99 model, cache, CRDT and network regression checks |

Headers declare structs and prefixed functions; implementations live in `.c`
files. Platform adapters use `.c` on Android and `.m` on Apple. Only the Sokol
implementation is compiled as Objective-C for Metal; the shared app stays C99.
Datafriend never includes app code. See the [dependency map](../architecture.visualize).

All store access, callbacks, edits and network pumps run on the application
thread. Disk flushing runs in the database's background worker. Each row owns
an explicit `ui_watch`; teardown unsubscribes it. A publication callback may
remove other subscriptions, and dispatch skips removed handles. Callbacks read
views and update derived presentation; event handlers submit edits.

A value passed to an edit is borrowed until `ui_add` encodes it. `ui_change`
consumes the edit and publishes affected views before returning. Borrowed result
bytes remain valid only until the next mutation. Editor callback contexts remain
owned by the caller through final delivery or cancellation. No virtual widget
tree or separate state scheduler is involved.

## Behavior

Tasks support add, rename, completion, filters, selection, scrolling, drag
reordering and deletion. `tasks-v1` persists on the device and seeds twelve
examples once. Row views mount only for visible tasks; renaming does not rebuild
the list/counter. A late editor submission cannot resurrect a deleted entity.
See [the task interface](../docs/datafriend-native-tasks.md).

Particles use 96 fixed spatial queries, a 3×3 moving patch or all-moving mode,
four particle counts, pause/resume and dragging. The controls are Datafriend facts.
Changing count replaces particle facts and count in one transaction, preserving
view handles and other controls. `particle-session` is disposable on launch.
The C RNG preserves the old mt19937_64 seed/distribution; comparison against the
saved C++ app matched every coordinate at initialization and after 100 ticks.
Neighbor and update arrays retain capacity between frames.

Text rasterization uses Android StaticLayout or UIKit/Core Text. The shared
content-keyed cache has a 100 MB texture-data budget and a GPU handle limit.
Keys include text, pixel size and clipping width, independent of position/tint.
Current-frame entries stay pinned until submission; older entries are evicted
in LRU order. Each cache entry owns its key and resource, with explicit cleanup.
Sokol draws each frame; this is separate from rebuilding query-bound UI caches.
System editors preserve IME composition, selection and paste. Canvas controls
now expose native accessibility labels, states, activation and task-list scrolling.
General hardware-keyboard navigation remains follow-on work.

Task titles and document bodies use the shared inline editor. Its C API exposes
cursor movement, selection, range replacement and revision-checked native updates.
See [text editing](../docs/text-editing.md) for ownership and composition rules.

## Shared document

The document demo keeps one Yrs snapshot and one materialized text fact per
owner. Datafriend stores opaque values and remains CRDT-agnostic. Each phone
pushes only its owned namespace and subscribes to its peer. The public-key
identity alias, schema, database paths and YDF2 snapshot records are unchanged.
A live editor holds its own draft so repeated stale edits preserve unseen remote
inserts. Snapshot and visible text publish atomically.

Yrs is pinned at 0.24.0 and built through its C API with Cargo. The first build
needs dependency downloads. Its generated header has a small local C99
compatibility patch; Rust source and binary formats are unchanged.

Pair/configure phones using `tools/sync_phones.py`; see
[document sync](../docs/datafriend-document-sync.md). The existing
`document-config.txt`, scripted scenarios, reports, request timings and UDP clock
calibration remain supported. The app preserves the ordinary task and document
stores during benchmarks.

## Validation and profiling

```sh
make -C demo-app test
# Against a running Littlebear instance:
demo-app/build/tests/document_network_test 127.0.0.1 17879
python3 datafriend/tool/generate_attributes.py --check
python3 demo-app/tools/profile_tasks.py
python3 demo-app/tools/profile_ios.py --device Phone
```

Task profiling uses a separate disposable database. It exercises idle, toggle,
rename, filter, reorder, scroll and add/remove with 12 and 1,000 tasks. Android
uses EGL presentation timestamps; iOS uses Metal presented callbacks. These are
automatic app-thread actions, not measurements of physical touch or IME latency.
CPU, frame, query publication, rasterization and texture statistics are recorded
under `docs/benchmarks/` with source/APK hashes and device conditions.

See [C99 validation and matched benchmarks](../docs/validation/app-c99.md).
Archived Flutter clients and benchmark branches remain historical references.

## Shaders and provenance

Sokol and generated shaders retain their existing pinned sources and licenses.
Rendering derives from `reference/junstodos`; see [provenance](../docs/provenance.md).
Normal builds use checked-in `src/render/shaders.h`. Shader changes require the
compatible sokol-shdc compiler and refreshed hashes in `shaders.json`.

## Native database interface

Models use Datafriend's direct C values, numeric query clauses, edit batches and
watches from `datafriend/db.h`. The app binding retains only profiling, error
presentation and text-buffer conversion. Local edits and friend imports dispatch
the same watches. Document subscriptions use `df_network_follow`; no application
forwarding of changed query IDs is needed. Task creation commits ID allocation
and initial fields together. Task ordering reuses `df_entities` storage.

Profile `engine` time now includes native watch dispatch. The old request-encode
and separate publication measurements no longer describe this direct call path;
compare total action/frame time across versions. See the
[C API guide](../docs/datafriend-c-api.md).
