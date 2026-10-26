# CRDT demo app

One editable text buffer: black text on a white screen, shared between an Android
phone and an iPhone. Tap to type. There are no navigation, pairing, or debug controls.
The package/bundle is `dev.drawdo.crdt`, separate from the larger Datafriend demo.

The C99 app uses Sokol's lifecycle and frame callbacks. `text_buffers` owns the
native editor, selection, composition, and keyboard interaction. `text_crdt`
wraps the existing pinned Yrs library. Datafriend persists changes and synchronizes
with Littlebear; each installation owns its namespace and subscribes to its peer.
The app thread serializes model, database, and network access. Android's native
editor exchanges snapshots with that thread through the text buffer library.
Socket readiness, native editor notifications, and network deadlines wake that
thread independently of rendering. Android uses the existing Sokol `ALooper`;
iPhone uses its main `CFRunLoop`. No extra model/network thread is created.

`src/main.c` runs the app and clears the background. `src/app.c` connects the editor
to the document. `src/document.c` stores one CRDT state fact and merges incoming
states. The platform files provide storage paths and editor bounds. `src/loop.h`
is the small platform boundary for wakeups, a socket watch, and a deadline.

## Build and pair

Requires the same Android SDK/NDK, Xcode, Rust targets, and signing setup as
[the larger demo](../demo-app/README.md). No Gradle or Flutter project is needed.

Android defaults to API 29 (Android 10), enabling Sokol's Choreographer frame
scheduling. `--api 24` builds the older buffer-swap-paced fallback for comparison
or older devices; the native target and APK minimum OS version change together.
Both build scripts accept `--frame-poll` to run the previous frame-driven app
processing path for comparison. Event-driven processing is the default; Sokol
still draws the background each frame in either mode.

```sh
python3 crdt-demo-app/tools/build_android.py run --serial ZY227364TP
python3 crdt-demo-app/tools/build_ios.py run --device Phone --team 466U33S75F
python3 crdt-demo-app/tools/pair.py --android ZY227364TP --iphone Phone
```

Launch each app once before pairing. The script exchanges public keys, writes
`peer.txt`, and restarts both installations. Signing keys remain in each platform's
secure storage. The default endpoint is `147.182.181.182:7777`; override it with
`--host` and `--port`. Pairing configuration and document data live in the app's
private directory.

Edits work offline and synchronize when the foreground app reconnects. Clearing
the text is a CRDT edit and propagates; an absent peer namespace does not erase the
last known document. Incoming edits update the native editor while it is active,
deferring during IME composition. A refresh that races with typing is retried
against the latest native snapshot. Cursor offsets are clamped to valid UTF-8
boundaries; CRDT-relative cursor rebasing remains deferred.

## Tests

```sh
cmake -S crdt-demo-app -B crdt-demo-app/build/tests -G Ninja
cmake --build crdt-demo-app/build/tests
ctest --test-dir crdt-demo-app/build/tests --output-on-failure
```

Host tests cover concurrent offline edits, stale displayed drafts, repeated state
delivery, persistence, deletion, native refresh rejection, coalesced input,
composition, and pause/resume.

The phone tests in `tests/android` and `tests/ios` run together on paired devices.
They replace the **demo document** with test text, verify both sync directions and
an active iPhone editor, background and resume both apps, restart the iPhone app
to check persistence, then clear
the test text. They do not touch the separate Datafriend demo installation.

```sh
python3 crdt-demo-app/tests/android/run.py --serial ZY227364TP > /tmp/crdt-android-test.log 2>&1 &
xcodegen generate --spec crdt-demo-app/tests/ios/project.yml
xcodebuild test -project crdt-demo-app/tests/ios/CRDTSyncTest.xcodeproj \
  -scheme CRDTSyncTest -destination 'platform=iOS,name=Phone' \
  -derivedDataPath /tmp/crdt-ios-tests -allowProvisioningUpdates \
  -allowProvisioningDeviceRegistration DEVELOPMENT_TEAM=466U33S75F
wait
```
