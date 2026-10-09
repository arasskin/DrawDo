# Archived Dart/Flutter client

**Unsupported as of 2026-10-08.** Use `demo-app/` for the native C++/Sokol app.
The source and commands below are historical reference; compatibility and builds
are no longer maintained.

The C++/Sokol task and particle demo has moved to
[`demo-app/`](../../demo-app/README.md). This directory holds the independent
Dart/Flutter baseline and its comparison tools.

```sh
make -C demo-app android-run # build/install the Sokol app from the repository root
```

## Flutter baseline

The Dart/Flutter iCare rewrite remains available for behavior and benchmark
comparisons, independently of the native app:

```sh
cd src/app # from the repository root
flutter pub get
flutter run
```

Developed with Flutter 3.41.4 / Dart 3.11.1. Use `flutter devices` to select an
Android device/emulator or iOS simulator. A physical iPhone also needs your own
signing team configured in `ios/Runner.xcworkspace`. Application code lives in
`lib/`; `ios/` and `android/` contain Flutter's standard platform runners.

## What works

- Create, complete, edit, delete, and reorder personal tasks. Tap a title to edit;
  submitting an empty title deletes it. Drag the handle to reorder.
- Drag the original animated artwork around My space, with short inertial throws.
  Tap the star to open My list.
- Draw on either screen with six ink colors, erase whole strokes, or undo the
  latest stroke on that screen. Choose Draw for immediate strokes, or long-press
  to draw in Move mode. Choose Move to scroll, drag, and edit again.
- List drawings use content coordinates and scroll with the list. They are not
  attached to individual tasks, so reordering a task does not move its doodles.
- Save tasks, strokes, and sticker positions automatically on the device and
  restore them on launch. Save failures remain visible and can be retried.

The three bundled animations in `assets/animated/` are unchanged copies from the
root `assets/animated/` reference collection.

## Structure

| Path | Responsibility |
| --- | --- |
| `lib/model/document.dart` | Immutable plain Dart tasks, strokes, positions, versioned JSON |
| `lib/data/document_store.dart` | Injectable storage interface, file implementation, memory test substitute |
| `lib/data/datafriend.dart` | Blocking C ABI, queries and shared live results |
| `lib/data/datafriend_document_store.dart` | Task/sticker/stroke views and atomic document edits |
| `lib/app_controller.dart` | Synchronous production commits, loading and error state |
| `lib/ui/` | Task list, draggable canvas, drawing gestures, navigation |
| `lib/main.dart` | Resolve the device support directory and start the app |

`path_provider` locates the app's support directory. Production data lives in a
fresh `datafriend-v1` AELMDB directory, owned by the native Datafriend service.
The old JSON file is not migrated. Local edits block through the store transaction and affected-view publication;
disk flushing is asynchronous by default. Drawings are split into bounded entity
records. See [the native contract](../../docs/datafriend-native.md).

Android packages the CMake-built native library; iOS packages a local CocoaPod.
The Dart UI does not require ClojureDart or a Kotlin rewrite. The platform runner
still contains Flutter's standard Android scaffolding.

## Checks

Build the desktop native library from the repository root before running tests:

```sh
cmake -S datafriend -B datafriend/build/native -DCMAKE_BUILD_TYPE=Release
cmake --build datafriend/build/native -j 4
cd src/app
flutter analyze
flutter test
flutter build ios --simulator --debug # macOS with Xcode
flutter build apk --debug --target-platform android-arm64
```

Tests exercise file reloads, concurrent writes, interrupted temporary files,
corrupt/future-version data, save recovery, task edits, drawing/erasing, dragging,
scroll coordinates, and keyboard layout.

## Next slices

Friends and their lists, identity onboarding/key storage, likes and notifications
remain UI work. Datafriend supports TCP reconciliation and Ed25519 owner writes;
see the [network API](../../docs/datafriend-network.md). Callers supply an owner
identity for pushes or a public-key namespace for pulls. The app displays local
save state only; it does not yet surface network connection status. A server cache
must not become the authority for the owner's local document.

The original force-based physics (body repulsion and stroke obstacles) is also
not ported yet; the initial canvas implements bounded dragging and deceleration.
