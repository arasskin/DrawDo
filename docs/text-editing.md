# Shared text editing

`text_buffers/text.h` is the C99 text buffer. `text_buffers/editor.h` connects it to
the native inline editor. Neither module knows about Datafriend or Yrs.
`main.c` owns one `editor_session` in its interaction struct; screens borrow it
and supply bounds and callbacks. Cursor, selection and composition stay out of
the database.

The buffer owns contiguous, NUL-terminated UTF-8 storage with reusable capacity.
It records selection anchor/caret, insertion affinities, a marked composition
range and a revision. Empty text is valid; embedded NUL and malformed UTF-8 are
rejected. Positions are UTF-8 byte boundaries, not character counts or screen
coordinates. Native bridges convert explicitly to/from UTF-16 and reject
positions inside surrogate pairs. Native controls handle grapheme/word
navigation, hit testing, caret drawing, selection handles, clipboard and IME.

## Buffer and cursor API

Zero-initialize buffers and sessions; free them explicitly. Standalone buffer
operations are synchronous:

```c
text_buffer text = {0};
text_buffer_set(&text, "hello", 5);
text_buffer_move_to(&text, 2, false);  /* Collapse selection at byte 2. */
text_buffer_move_to(&text, 4, true);   /* Extend from the existing anchor. */
text_buffer_insert(&text, "y", 1);    /* Replace selection; caret follows y. */
text_buffer_free(&text);
```

Check boolean results in application code. `text_buffer_replace` edits a byte
range and remaps both selection endpoints. Endpoint affinity decides which side
of an insertion retains the cursor. Buffer set/replace input must not alias its
owned bytes. User-visible grapheme movement should use the native editor rather
than incrementing byte offsets. UIKit reports ordered selections; the shared
buffer and Android also retain reverse selection direction.

For an active native editor, use `editor_move_to`, `editor_select`,
`editor_replace` or `editor_set`. Directly mutating `session.buffer` does not
update the native control. Commands return whether they were queued, and take
effect when `editor_poll` receives `EDITOR_ACK`. Only one command may be pending.
The native revision check rejects a command if typing or selection changed
after the app's last snapshot. Poll then reports `EDITOR_REJECTED` and adopts the
latest native state; callers can recompute their command from that state.
Session generations discard late events from closed editors.

All session calls and callbacks run on the app thread. Android transports native
snapshots through a mutex-protected mailbox. Poll events last until the next
poll. Callback text is borrowed for the callback, and callbacks must not
recursively reopen, poll or destroy their session. Options' label and callback
context remain caller-owned. A snapshot validation/allocation error stops the
session with `EDITOR_ERROR`, retaining the last accepted buffer; the app reports
the failure. Delivery is not guaranteed under allocation failure.

## Inline editing and composition

Task titles edit in their row and commit on submission. The add control becomes
an inline editor. Tapping the document body or its edit button opens a multiline
editor over that body; stable edits publish live. `live` and `multiline` are
independent options. Screens keep their text drawn behind the opaque native
editor, so asynchronous opening/closing cannot leave a blank canvas frame.
Android disables popup presentation animations for this inline overlay. Screens
maintain native bounds through `editor_layout`. Tapping a document or task title
passes the tap to native text hit testing, placing the initial cursor at the
nearest insertion position. Explicit Edit buttons still start at the end.
`editor_options.place_at_point` enables this; `point_x`/`point_y` are pixels
relative to the editor bounds. Android waits for the first native layout before
hit testing; iOS converts those pixels to UIKit points and sets the final selection
without animation before focusing the control, so no temporary start position is
shown. Only the initial tap is
forwarded; subsequent taps are handled directly by the native control.
Document paragraphs
use native word wrapping, font metrics and zero padding in both passive rendering
and the editor, so entering edit mode does not reflow fixed-character lines.
Paragraph textures are bounded by the viewport; width and height are part of
the cache key. On iOS, fonts are created at UIKit point sizes and rasterized at
screen scale; creating a pixel-sized font instead changes its optical metrics
and line breaks. Native and canvas origins/sizes use the same pixel rounding.
Android safe-area insets describe system bars/cutouts, excluding
keyboard occlusion, so opening the keyboard does not resize the app layout.

Android's [window inset API](https://developer.android.com/reference/android/view/WindowInsets)
distinguishes system bars from the IME. The native editor retains
`SOFT_INPUT_ADJUST_NOTHING`; its text layout matches the paragraph rasterizer.

Composition updates remain visible in the session but do not invoke live model
callbacks. Finishing composition publishes even if the bytes are unchanged.
Programmatic commands are rejected during composition. Cancel discards pending
unsubmitted changes; it does not undo live edits already delivered. Accepted
programmatic content changes also invoke live callbacks. Selection-only changes
do not write the model. Observe `session.events` after polling to react to
selection or composition changes.

The document keeps its existing CRDT draft behavior, preserving remote edits
the editor has not seen. This change does **not** inject remote changes into an
active editor or store CRDT-relative cursor anchors. That integration should
resolve anchors after merge, update text and selection together, distinguish
remote refresh from local edits, and retry/rebase if native input wins the race.

See [validation](validation/text-editing.md).
