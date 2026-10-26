# Native UI libraries

Implemented and installed on the Moto G7 (`ZY227364TP`) and iPhone 13 (`Phone`).
The app imports the top-level `accessible_ui`, `clipboard`, `haptics`, and
`text_buffers` C99 libraries. Their native adapters use Objective-C or Java/JNI;
no app, Sokol, Datafriend or Yrs headers occur inside these libraries.

## Coverage

- Eight host suites pass, including accessibility snapshot reuse, changed states,
  duplicate IDs, removed nodes, and bounded action-queue ordering/backpressure.
  [Results](native-ui/host-tests.txt).
- Android instrumentation passes using the installed app's actual C exports:
  Unicode clipboard round trip (emoji, accents, Japanese, combining marks,
  newline), empty text, invalid UTF-8 rejection without clobbering prior text,
  and all three haptic requests. It saves/restores the complete prior ClipData.
- Android accessibility checks use the OS accessibility tree and ACTION_CLICK,
  ACCESSIBILITY_FOCUS and SCROLL_FORWARD, not coordinate taps. They cover labels,
  enabled/clickable states, native editor handoff, navigation across all three
  screens and task-list scrolling. [Results](native-ui/android-tests.txt).
- iPhone native tests run inside a foreground test host. They check the same
  clipboard cases while saving/restoring all pasteboard items, all three haptic
  calls, UIKit button semantics, activation, disabled/removed activation rejection,
  and element identity retention. The UI tests locate controls by accessibility
  labels, navigate all three screens, check native editor handoff, and verify
  initial caret placement and keyboard layout. [Results](native-ui/ios-tests.txt).
- An independent strict-C99 CMake consumer built and ran against `text_buffers`
  and `accessible_ui`, with a fake platform publisher and no demo/Datafriend
  target. This verifies that the extracted core libraries stand on their own.
- Shared container tests cover NUL-terminated storage growth, overflow and
  allocation-failure preservation, alongside the existing array/map tests.

The initial Android build exposed a stripped JNI install entry point for the
otherwise unused clipboard service. Each native target now retains its Java
installation symbol explicitly. iOS clipboard tests initially ran in the UI-test
runner, where the pasteboard is unavailable; they now use a foreground test host.

These are native integration tests, not a full VoiceOver/TalkBack usability audit.
Haptic requests were exercised on both phones; automation cannot confirm how a
vibration feels or override system settings. Accessibility covers standard app
controls and list scrolling. Custom particle-grid painting and general hardware
keyboard traversal do not yet have equivalent semantic actions.

## Reproduction

Build/install the app with the usual `build_android.py run` / `build_ios.py run`.
Then run:

```sh
python3 demo-app/tests/android/run.py --serial ZY227364TP
xcodegen generate --spec demo-app/tests/ios/project.yml
xcodebuild test -project demo-app/tests/ios/EditorVerification.xcodeproj \
  -scheme EditorVerification -destination 'platform=iOS,name=Phone' \
  -derivedDataPath /tmp/drawdo-services-ios/build \
  -allowProvisioningUpdates DEVELOPMENT_TEAM=your_team
```

The UI tests expect the paired document demo to be configured and initially
visible, with nonempty text. They do not type or delete document content.
The Android test APK is separate from the app; the iOS native unit test uses
`dev.drawdo.ServicesHost`. Neither test fixture is included in production code.

Session artifacts: `/tmp/drawdo-services-ios/final.xcresult`,
`/tmp/drawdo-services-ios-final.log`, `/tmp/ui-services-android-final-test.log`,
`/tmp/native-libraries-smoke.log`, `/tmp/ui-libs-ios-final.log`, and
`/tmp/ui-libs-android-final.log`.

The container tests also passed under UBSan. AddressSanitizer could not run on
this Mac: its runtime deadlocked during dyld/malloc initialization, before the
test entered `main` (confirmed by a process stack sample). Leak detection is
also unsupported by this Apple sanitizer runtime. The stalled processes were
stopped; this is not recorded as an ASan pass.
