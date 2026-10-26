# Shared text buffer validation — October 2026

The shared C99 buffer and editor session are connected to Android and iOS native
controls. Task titles/add and document bodies use inline bounds. Cursor and
selection remain app-owned transient state; CRDT anchor integration is separate.

Passed:

- Seven host suites: text buffer, editor session, task model, particle model,
  text cache, document CRDT and document model.
- Strict C99 builds with `-Wall -Wextra -Werror -pedantic-errors`, AddressSanitizer,
  UndefinedBehaviorSanitizer and leak detection for the two new suites in Linux
  (one CPU, 512 MiB). macOS sanitizer execution stalled and was terminated;
  sanitizer success here refers to Linux.
- Android native/Java/package build, signing verification and installation on
  Moto G7 `ZY227364TP`. App launched successfully; process observed running.
- Unsigned iOS compile/link, followed by a signed build, installation and launch
  on the paired iPhone 13 (`Phone`). Installation and launch succeeded October
  18 and were repeated successfully October 21 after a transient device transport
  reset during a process query.

Tests exercise UTF-8/UTF-16 conversions (including emoji and combining marks),
invalid encodings and positions, directional selections, insertion affinity,
range replacement, composition, asynchronous acknowledgments, commands racing
newer native input, coalesced events, stale generations/revisions, submit/cancel
and invalid native snapshots. These are automated model/bridge tests, not a
physical-keyboard or IME usability assessment. No phone touch/IME latency claim
is made.

## Android edit-and-run timing — October 21

Command: `/usr/bin/time -p python3 demo-app/tools/build_android.py run --serial ZY227364TP`

| Stage | Seconds |
| --- | ---: |
| Native cache check | 0.15 |
| Java/DEX and resources | 0.00 (cached) |
| Package/signature cache check | 0.01 |
| Install | 6.40 |
| Launch command | 0.55 |
| Full command wall time | 9.70 |

Android reported 464 ms cold activity startup. No source needed recompilation,
so this measures a cached rebuild/install/launch, not a clean build. Differences
between stage totals and wall time include tool startup and device checks.
APK: 4.4 MiB, SHA-256
`b88945f48ba57671e5541a1e53cd770ac6ef7f906e8b5e5e88ba6ce572da9db2`.

Logs for this session: `/tmp/text-host-tests.log`, `/tmp/text-ios-build.log`,
`/tmp/text-apk-build.log`, `/tmp/text-ios-phone.log`,
`/tmp/text-ios-phone-current.log`, `/tmp/text-android-phone.log`,
`/tmp/text-android-timed.log`.

## Server update — October 21

Built the current working-tree C99 server on the x86-64 Ubuntu droplet, with one
compiler process and development headers extracted into an isolated staging
directory. The existing runtime packages were used unchanged. The C99 model /
request-lifetime suite and full signed TCP protocol test passed before activation.
The binary links libc, liburing and libsodium, with no C++ runtime dependency.

Activated release `74e4a01e55d7a8dde43ae94f4697f7a5f1d61400-20261021-1` through
`deploy/deploy.sh`. Systemd reported active/running, port 7777 listening, release
SHA-256 checks passed, and both local and public LBS5 heartbeat checks succeeded.
Server SHA-256:
`a4f02dd3086fa333f2f28d0613695da2bc32a177806e26e7f55548178e7da655`.

This is a working-tree deployment, not a push or a GitHub Actions build. The
release includes `SOURCE_MANIFEST.json` and `BUILD_ORIGIN` to record that
distinction. Temporary source/build files were removed; the previous release
was retained. Logs: `/tmp/text-server-build.log`, `/tmp/text-server-deploy.log`,
`/tmp/text-server-status.log`.

## Inline layout and recovered connection status

Reproduced on Moto G7: the editor window stayed at its requested origin, but
opening it changed the document's line breaks. Passive rendering broke lines
every 32 code points; EditText used native word wrapping and different font
padding. Passive paragraphs now use the native wrapping/spacing/padding rules
and the same pixel origin. Raster allocation is clipped to the viewport, whose
width and height participate in the texture-cache key. Android safe-area insets
exclude keyboard occlusion.

The footer also retained a prior `network disconnected` string after the client
had reconnected (the header already said Connected). Connection errors are now
read from the network's current state instead of copied permanently into the
document error. Document failures remain separate and are not cleared by a
successful reconnect. Saved diagnostic reports use the same current error.

Validation:

- Seven host suites passed after rebuilding. The document model test opens a
  loopback TCP listener, disconnects the client, accepts its reconnect, and checks
  that the stale error disappears while an independent document failure survives.
- Cache regression distinguishes label/paragraph entries and viewport heights.
- Android build/install/launch passed. Before/after keyboard screenshots confirm
  matching visible text position and line breaks, with Connected status and no
  stale disconnect footer. No document text was entered or changed during checks.
- iOS unsigned build and signed build/install/launch passed. iOS keyboard layout
  was not visually compared in this check.
- Public Littlebear heartbeat passed; no server change was needed.

Session evidence: `/tmp/inline-host-tests.log`, `/tmp/inline-android-build.log`,
`/tmp/inline-ios-phone.log`, `/tmp/inline-fixed-idle.png`,
`/tmp/inline-fixed-edit.png`, and `/tmp/inline-fixed-window.txt`.

## Physical iPhone verification and Android transition

The first iPhone UI test confirmed the editor frame stayed in place, but visual
inspection caught different line breaks: the passive rasterizer created a
pixel-sized UIFont while UITextView used a point-sized UIFont. System font optical
metrics depend on that size. Paragraph rasterization now creates the same
point-sized font and scales the drawing context to physical pixels. The editor
and canvas also round their origin and bounds to the same pixel grid.

The revised on-device XCTest passed on the iPhone 13. It opens the editor,
checks keyboard visibility and frame position, compares screenshot glyph bounds,
then closes with Done. All five visible lines had identical horizontal bounds;
four matched vertical bounds exactly, and one differed by one physical pixel
(assertion tolerance: two pixels). No document text was changed.

Screenshots: [before](text-editing/ios-before-keyboard.png),
[keyboard open](text-editing/ios-keyboard-open.png), and
[closed](text-editing/ios-keyboard-closed.png). Test source and XcodeGen spec are
in `demo-app/tests/ios/`. The test expects this configured iPhone 13 document
screen, with nonempty text; it is not a general device-layout test.

```sh
xcodegen generate --spec demo-app/tests/ios/project.yml
xcodebuild test -project demo-app/tests/ios/EditorVerification.xcodeproj \
  -scheme EditorVerification -destination 'platform=iOS,name=Phone' \
  -derivedDataPath /tmp/drawdo-editor-tests \
  -allowProvisioningUpdates DEVELOPMENT_TEAM=your_team
```

Android previously hid the canvas text immediately when an editor was requested,
even though creation/drawing of its native window happens asynchronously. The
canvas now remains drawn behind the opaque editor throughout opening/closing;
popup animations are disabled. This also applies to task titles and the add
control. A recorded three-cycle check sampled 718 times (498 distinct decoded
frames), with no missing-text samples. Bright text-pixel counts in the document
region ranged from 19,996 to 20,166, consistent across the transitions.
[Measurement record](text-editing/android-transition.json). This recording does
not prove every hardware presentation frame or every possible device/timing.

Both updated apps were built, installed and launched. Seven host suites passed.
Session results: `/tmp/drawdo-ios-verify/second.xcresult`,
`/tmp/drawdo-ios-verify/second.log`, `/tmp/flash-host-tests.log`,
`/tmp/flash-android-final.log`, `/tmp/flash-ios-build.log`, and
`/tmp/inline-flash-fixed.mp4`.

## Initial tap placement

Both installed apps now forward the opening tap to the native editor's text
layout. Task title taps preserve list scroll; explicit Edit buttons retain the
end-of-text behavior. Seven host suites pass, including a task-screen regression
for tap coordinates, unchanged row position, and the separate Edit-button path.

On the Moto G7, initial taps at screen pixels (330, 752) and (200, 907)
placed the cursor at UTF-16 offsets 14 and 110 respectively, confirmed through
Android's input-method selection state. The latter is on a wrapped line.
The explicit Edit button selected offset 120, the document end at test time.
Screenshots: [first line](text-editing/android-tap-first.png),
[wrapped line](text-editing/android-tap-wrap.png).

On the iPhone 13, XCTest tapped the first two visible lines and measured the
native blue caret in screenshots. Its horizontal center was 10 and 7.5 physical
pixels from each target, respectively (within half a typical glyph width).
The keyboard-layout regression also passed. The tap test asserts unchanged
editor text and requires a document with at least two visible lines.
Screenshots: [first line](text-editing/ios-tap-first.png),
[wrapped line](text-editing/ios-tap-wrap.png).
Results: `/tmp/drawdo-ios-verify/tap-confirm.xcresult` and
`/tmp/drawdo-ios-verify/tap-confirm.log`. Tests used taps and keyboard dismissal;
they did not enter or delete document text.

### iPhone cursor opening order

The initial implementation focused the editor before temporarily selecting the
start and then the tap target. That allowed UIKit to display/animate an
intermediate caret. Opening now lays out and selects the final position without
animation before calling `becomeFirstResponder`; the temporary start selection
has been removed. The signed build was installed on Phone. Both on-device tests
(initial placement on two lines and keyboard layout) passed in
`/tmp/drawdo-ios-verify/caret-focus.xcresult`. These screenshot tests check settled
positions and layout; they do not sample every frame of the opening transition.
