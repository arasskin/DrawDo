# CRDT demo app validation

Validated on 2026-10-21 using the Moto G7 (`ZY227364TP`) and iPhone 13 (`Phone`).
The separate `dev.drawdo.crdt` installations were paired through Littlebear at
`147.182.181.182:7777`. The original `dev.drawdo.sokol` installations were not changed.

- Both new host suites passed: document convergence/persistence and native editor
  refresh races. All eight existing demo host suites also passed after extraction.
- Android's native editor supplied `Android`. The iPhone received it, opened the
  keyboard without moving the top of the text buffer, and typed ` iPhone `.
- Android received that edit and appended `synced`. The iPhone received the update
  while its native editor still had keyboard focus.
- The iPhone app was terminated and relaunched; the text survived. Deleting the
  text propagated back to Android. Both device test runners passed.
- A final Android touch/keyboard check typed `Hello`, confirmed readable black
  text on white with the keyboard open, then deleted it and dismissed the keyboard.

The tests exposed an Android NativeActivity integration requirement: its renderer
owns the main window surface, so native text uses the text buffer library's separate
window. The host keeps Sokol's loop active during that editor focus handoff and
still suspends on activity pause. No changes to Sokol's source were needed.

Evidence: [Android test result](crdt-demo-app/android-test.txt),
[binary hashes](crdt-demo-app/builds.json),
[Android keyboard screenshot](crdt-demo-app/android.png), and
[iPhone live-sync screenshot](crdt-demo-app/iphone.png).
The full iPhone result bundle is `/tmp/crdt-sync-final.xcresult` (one test, zero failures).

CRDT-relative cursor rebasing remains deferred. IME composition temporarily defers
incoming editor refreshes. The app synchronizes while foregrounded and reconnects
on return; it does not request background execution.
