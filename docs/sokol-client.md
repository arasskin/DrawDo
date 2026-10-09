# Native Sokol client direction

Decision, 2026-10-08: move the application toward C++ with Sokol on iOS and
Android, using the existing native Datafriend engine directly. The Dart/Flutter
client remains the working behavior and benchmark baseline during the transition.
This supersedes the earlier decision to finish the UI in pure Dart.

## Recovered application

The substantial native foundation is in **junstodos**, not the current iCare
checkout. `reference/junstodos` contains the C++ app at
`f5cd92ace74f0f112cba76743786660ee0ae9ebf`. Later commits added Janet (`6d957b1`)
and the current working tree has deleted most of the implementation in favor
of Scheme. Reading the working tree alone would miss it.

The recovered code includes a native todo app; iOS bundle/build setup;
Sokol/Metal lifecycle and touch dispatch; a glyph-atlas renderer; Core Text
shaping, bidi, fallback and Core Graphics glyph rasterization; plus its old
Datalog, persistence, authentication and synchronization code. The latter are
historical reference, since current Datafriend supplies those responsibilities.

The useful architecture is already separated: `font/text.cpp` owns the GPU
atlas and batches, while `font/text_platform.h` defines the shaping/rasterization
interface and `text_ios.mm` implements it for Apple platforms.

## Reuse and Android equivalents

| Existing part | Reuse / Android equivalent |
|---|---|
| Sokol lifecycle callbacks | Same init/frame/event/cleanup organization. The recovered `sokol_app.h` already implements Android NativeActivity. |
| Metal backend in `sokol.mm` | Objective-C++/Metal for iOS; C++ with `SOKOL_GLES3` for Android initially. |
| CMake iOS bundle and plist | Keep platform-specific Apple packaging; Android now uses Make/CMake/Ninja plus direct SDK packaging tools. |
| `shaders.glsl` | Share source; generate Metal **and GLES3** variants. The archived generated header only contains Metal variants. Pin headers/compiler together. |
| Glyph atlas and batched quads | Reuse the portable renderer after validating shader/backend compatibility. |
| Core Text/Core Graphics | Keep on iOS. Android now caches labels rasterized by StaticLayout/Canvas; full editing remains follow-on work. |
| Touch coordinates | Preserve logical-coordinate API and validate density, insets, rotation, multi-touch and stylus on each platform. |
| Old query-per-frame todo app | Adapt interactions to current Datafriend live views and direct C++ edits; refresh cached rows when notified. |
| Old local database and HTTP mirror | Use current Datafriend and Littlebear integration, including native secure identity storage. |

Android's [NativeActivity](https://developer.android.com/reference/android/app/NativeActivity)
loads the application shared library through manifest metadata. Application
logic and rendering can remain C++; a small Java/JNI platform adapter is still
appropriate for system services such as secure storage and text input.

The current TCP socket/timer adapter is still Dart code in
`src/app/lib/data/datafriend_network.dart`. Moving fully native also means porting
that transport orchestration, while preserving the native reconciliation and
identity/signing code and the current Littlebear protocol. The recovered HTTP
mirror is not a replacement for the current authenticated server integration.

## Text on the old Android phone

The Moto G7 runs Android 10/API 29. Android's public
[TextShaper](https://developer.android.com/reference/android/text/TextShaper)
interface—which exposes shaped glyphs—is API 31+. It cannot be our only path on
this phone. We should not replace Core Text with codepoint-by-codepoint glyph
rendering: that would lose contextual shaping, bidi behavior and fallback.

Two text implementation directions:

1. **System layout/rasterization through a small Java/JNI adapter.** Use
   [StaticLayout](https://developer.android.com/reference/android/text/StaticLayout)
   and Canvas to rasterize cached text runs/lines. Sokol draws the resulting
   cached textures. This works with the phone's API level but needs a run/line
   cache interface alongside, or replacing, the old glyph-only interface.
   Calls should occur when text/layout changes, not once per glyph per frame.
2. **Native shaping and rasterization.** Integrate HarfBuzz/FreeType plus font
   discovery/fallback and bidi handling. This fits a glyph-atlas design, but
   adds dependencies and platform work. It is not implemented by the recovered
   Sokol code.

The first is implemented for the native particle demo, with 96 cached label
textures and JNI calls only when labels change. The second remains an option
for more cross-platform control. The recovered
app also lacks full IME composition, selection/caret editing and accessibility.
Those are explicit follow-on UI requirements, especially for document editing.

## Implementation sequence

1. Preserve and verify the native source snapshot (complete).
2. Bring up the recovered Sokol lifecycle/shaders (Android and iOS complete).
3. Attach the existing native Datafriend particle client (Android complete; a
   controlled rendered performance comparison remains separate work).
4. Port text and todo interactions (Android/iOS task list and system editors complete;
   broader UI/platform work remains).
5. Port the current TCP adapter, wire native platform identity services, and add
   keyboard/IME, text selection, accessibility and the remaining platform services.

The import is verified against Git objects. An additional Android arm64/API24
compile/link probe passes using the recovered Sokol headers, GLES3, and regenerated
simple/text shader descriptors. The resulting library exports
`ANativeActivity_onCreate` and `sokol_main`. The matching local shader compiler
successfully generated Metal simulator, Metal iOS and GLES3 variants together.
[Probe source](validation/sokol-import-android.cpp) and
[commands/hashes](validation/sokol-import-android.json) record this check.

The standalone Android app is now packaged, installed and rendering on the
Moto G7. Count changes, local/all-moving modes, pause/resume, patch dragging and
background/resume were checked; database validation passes on initialization and
suspend. See [the captured screen](benchmarks/datafriend-sokol-android.png) and
[device log](validation/sokol-android-device.txt). The current task UI and
iOS packaging are now implemented; see the [iPhone comparison](datafriend-ios.md).

## Direct Android workflow

```sh
make -C demo-app android       # native compile and signed APK
make -C demo-app android-run   # build, adb install, launch
make -C demo-app android-logs  # follow native logs
```

The [native application](../demo-app/README.md) uses CMake/Ninja, NDK Clang,
javac/d8, aapt2, zipalign/apksigner and adb. No Gradle or Flutter build is involved.
The APK is 2.8 MiB. The measured cold build took 11.44 s; the latest incremental
C++ change took 2.69 s to build/package, 4.26 s to install and 0.43 s to launch.
These are development-machine timings, not simulation performance results.
Build metadata is recorded with [cold](benchmarks/datafriend-sokol-build-cold.json)
and [incremental](benchmarks/datafriend-sokol-build-incremental.json) measurements.

The app uses 96 fixed spatial queries, one blocking edit per tick and asynchronous
disk flushing. Native simulation code is shared with the existing matched
Dart/C++ benchmark. Networking is not connected in this demo.

## Native controls now use Datafriend

The particle app now stores control settings alongside particle facts. Seven
query-bound buttons and a settings/status view update from blocking edits;
particle count changes preserve the database and subscriptions. Sokol drawing
remains frame-based. See [the implementation and checks](../demo-app/README.md#datafriend-driven-particle-controls).

The native task list is now the default screen: scoped row subscriptions, direct
edits, filtering, reordering, a shared detail panel, persistence and Android text
editing. See [the task-list implementation](datafriend-native-tasks.md).
