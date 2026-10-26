# DrawDo save handoff — 2026-10-22

The native submit path used to dismiss its window immediately after queuing the
saved text. The canvas still showed the previous value until the model callback
and next render, exposing an old title or blank add row.

DrawDo now opts into `text_buffers`' retained-submit mode. Done publishes once,
hides the keyboard/caret, and freezes the native text surface. The C app commits
the task, renders its replacement, then releases the overlay after presentation.
Android uses `EGL_DISPLAY_PRESENT_TIME_ANDROID` on the tested Moto G7. A dropped
frame is retried; unsupported drivers use a conservative three-frame swapchain
drain plus GPU completion, which is not a measured display acknowledgment.
The Apple source uses Metal's presented callback; iOS was not built or tested.

Verified on the Android phone:

- The native editor still exists immediately after Done and is frozen with its
  cursor hidden, instead of exposing the old canvas.
- Presentation logs confirm actual display timestamps before release for task
  creation, edits, and empty-title deletion; this device did not use the fallback.
- The complete task interaction test passed, including Unicode, wrapping,
  accessibility, tap cursor positioning, and stable keyboard origin.
- Temporary test data was deleted and DrawDo was left open.

Host editor tests cover deferred release, duplicate/late submit events, repeated
release, cancellation, and normalization for live editors. The CRDT editor
regression test also passed. Live/embedded editors retain their previous behavior.

Evidence: [Android test](drawdo-save/android.txt),
[presentation acknowledgments](drawdo-save/presentation.txt).
