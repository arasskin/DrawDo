# Native task list

The standalone Android **Datafriend demo** now opens a persistent task list.
The particle simulation is available through the **Particles** button.
Build/install/launch with `make -C demo-app android-run`.

## Ergonomics

The reference is `src/todo_demo/lib/todo_screen.dart`: inline pure-data queries,
include flags, entity field access, direct edits and `attempt` error handling.
Native components use ordinary C++ ownership. There is no widget/element tree,
State subclass, build context or framework lifecycle to implement.

A row owns a scoped subscription:

```cpp
view = db.watch(ui::select({
  {{id, A::title, ui::var{0}}, true},
  {{id, A::done,  ui::var{1}}, true},
}), [this](const ui::rows& rows) {
  exists = !rows.empty();
  if (exists) {
    title = rows.text(id, A::title);
    done = rows.flag(id, A::done);
  }
});
```

An action edits the store directly:

```cpp
ui::attempt(error, [&] {
  db.edit({{id, A::done, !done}});
});
```

The edit publishes affected views and executes their callbacks before returning.
Byte encoding and the shared attribute-name mapping live in `demo-app/src/datafriend_ui.h`.
`ui::var` names query-local variables; a numeric entity ID is a constant entity.
The boolean in each clause pair controls projection, matching the Dart API.
`db.remove(id)` removes the complete entity, including unprojected fields.

`demo-app/src/task_model.h` contains query-owning task rows and the screen model; `demo-app/src/task_screen.h`
contains drawing/layout and adjacent action callbacks. A small drawing surface
provides shapes, text and clipping. C++ functions compose the UI. GPU resources,
scroll offsets, gesture capture and keyboard composition are transient platform/
rendering state; task facts, filter and selection live in Datafriend.

## Subscription boundaries

- The list projects only `order`, using `kind` and optionally `done` as filters.
- Each visible row projects its own `title` and `done`. Scrolling mounts/unmounts
  rows by entity ID; reordering preserves components that remain visible.
- The completion counter independently queries task `done` values.
- The selected-task panel owns another row query, sharing the engine's result
  when it matches a mounted row's query.
- A title edit leaves the list, counter and unrelated rows untouched.
- Completion changes update the counter and affected row owners. In All mode,
  the list does not rebuild; filtered lists update when membership changes.
- Reorder writes changed integer positions together in one transaction. Under a
  filter, it reorders visible tasks within their existing order slots.

Destroying a subscription unsubscribes immediately. Publication tolerates a list
callback removing row subscriptions: removed handles are skipped, and the current
callback/result stay alive while the callback executes. Callbacks read views and
update presentation; event handlers perform edits.

## Phone interactions and persistence

- Tap the composer to add; submit using the keyboard's Done action.
- Tap a title to select and edit it. Editing uses the row position where practical;
  low rows use the composer area to stay above the keyboard.
- Android Back cancels editing. Blank adds/renames are ignored.
- Tap a checkbox to complete; use All/Active/Completed to filter.
- Drag the list to scroll. Drag a row's `≡` grip to reorder; the list scrolls near
  the viewport edges during dragging. The detail panel also has up/down controls.
- Tap `×` or Delete to remove. A late editor submission rechecks existence.

Task data lives in private `tasks-v1`, seeds twelve examples only once, and survives
restarts. Datafriend flushes asynchronously. The particle demo still uses its
separate disposable simulation directory. No network adapter is wired here yet.

Android supplies an EditText in a separate PopupWindow surface; NativeActivity's
EGL renderer owns the main window surface. This preserves system text composition,
selection and paste without implementing an IME. Completion crosses a small JNI
mailbox and executes on the C++ application thread; Java never edits Datafriend.
The keyboard action uses Android's
[editor action interface](https://developer.android.com/reference/android/widget/TextView.OnEditorActionListener).
The window uses
[SOFT_INPUT_ADJUST_NOTHING](https://developer.android.com/reference/android/view/WindowManager.LayoutParams#SOFT_INPUT_ADJUST_NOTHING)
to keep native layout coordinates stable while editing.

## Checks and current boundaries

```sh
cmake -S demo-app -B demo-app/build/tests -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build demo-app/build/tests -j 4
ctest --test-dir demo-app/build/tests --output-on-failure
python3 datafriend/tool/generate_attributes.py --check
```

The task integration test covers narrow invalidation, stable row identity on
reorder, filtered membership, bounded mounted subscriptions, removal during
notification dispatch, stale editing, typed Unicode strings, error handling,
store reopen, clipped hit testing and scroll-versus-tap behavior. The existing
particle model regression also passes. On-phone checks cover keyboard add/rename/
cancel, completion, filters, drag reorder, scrolling, restart persistence and
switching to/from particles. Two reorder actions increased list builds 1→3 while
row builds remained 8 and counter builds remained 1. See
[device log](validation/sokol-tasks-device.txt),
[check summary](validation/sokol-tasks-device.json),
[screen](benchmarks/datafriend-native-tasks.png), and
[editor](benchmarks/datafriend-native-tasks-editor.png).

This is a usable native UI slice. Layout uses explicit rectangles, scrolling is
direct manipulation without momentum, and the Sokol canvas does not yet expose
an accessibility tree or general keyboard navigation. GPU drawing still happens
each frame; query callbacks only rebuild affected presentation caches. There is
no new matched Flutter/native UI performance comparison in this change.
