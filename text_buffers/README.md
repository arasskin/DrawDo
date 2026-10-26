# Text buffers

C99 UTF-8 buffers, selection/composition state, and native inline editor sessions.
Extracted from the demo app; independent of Sokol, Datafriend and Yrs.

- `text.h`: synchronous owned buffer operations, UTF-8/UTF-16 conversion.
- `editor.h`: app-owned editor session, native commands and polling.
- `native.h`: adapter contract and native snapshot adoption.
- `apple/` and `android/`: UIKit and Java/JNI editors.

```c
#include "text_buffers/text.h"
text_buffer text = {0};
if (text_buffer_set(&text, "Hello", 5)) {
  text_buffer_move_to(&text, 5, false);
  text_buffer_insert(&text, " 🌱", 5);
}
text_buffer_free(&text);
```

Positions are UTF-8 byte offsets. Native controls own grapheme navigation,
composition, hit testing and caret rendering. The core uses only the standard C
library and `containers/bytes.h`; native editing is a separate target.

Link `text_buffers` for the core and `text_buffers_native` for an editor adapter.
Only one native editor is active per app. See [platform setup](../native/README.md)
and [the editor interface guide](../docs/text-editing.md) for session lifecycle,
threading, asynchronous commands and examples.

An event-driven host can install `editor_native_notify(wake)` from `native.h`.
The native producer calls `wake` after publishing a mailbox snapshot. The callback
must only signal the owning app thread; that thread calls `editor_poll` normally.
Notifications may coalesce. Unregister with `editor_native_notify(NULL)` before
closing the host's wake mechanism. The library does not create an event loop.

Set `editor_options.embedded = true` for a persistent native text view. It starts
without opening the keyboard, focuses on tap, and remains visible on blur. The
host still owns its bounds and calls `editor_layout` when the viewport changes.
The default overlay mode keeps its existing focus/submit/close behavior.

For a canvas-backed screen, set `retain_on_submit = true` on a non-live overlay.
Done delivers the saved text once and freezes the native view in place. The session
becomes inactive with `retained = true`; after presenting the replacement canvas
frame, call `editor_release()`. Cancel/free also remove a retained view, and late
events cannot submit it twice. Live and embedded editors ignore this option.

On Android `NativeActivity`, even a persistent editor needs its own window:
NativeActivity gives the main surface to its renderer, so child views there cannot
draw. `TextEditor.hasWindow()` lets the host distinguish that focus handoff from
leaving the app. Keep the app loop active while its editor window has focus, and
suspend on the activity's `onPause`; see
[`crdt-demo-app`'s host](../crdt-demo-app/android/java/dev/drawdo/crdt/MainActivity.java).

Native refreshes preserve the current text object and replace its changed range,
with UTF-16 boundaries adjusted to avoid splitting surrogate pairs. Unchanged
selection commands are skipped. The public `editor_set` API still accepts a full
UTF-8 snapshot; composition and stale-revision rejection remain unchanged.
