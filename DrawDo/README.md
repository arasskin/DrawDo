# DrawDo

The first C99 DrawDo screen follows the [CLJD own-list screen](../reference/icare/src/app/src/icare/ui/screens/pages/list/base.cljd): white background, red 26-point checkboxes, 20-point text, grey completed titles, and a grey add row. Tap the add row to enter a task, tap a title to edit it, or tap a checkbox to toggle it. Done saves; submitting an empty title deletes the task. Back cancels an unsubmitted edit.

This increment has no scrolling or drag reordering. Text wraps within each row; rows beyond the viewport remain in the database. There is no example-data seed. Android package `dev.drawdo.app` is separate from both demos and the previous Flutter app.

## State and sync

Tasks are ordinary Datafriend entities with `kind = "task"`, `title`, `done`, and `order` facts. Each visual setting is a named entity, such as `{name: "ui/checkbox_size", value: 26}`. Settings use the existing `name` and `value` attributes, so the shared wire schema and deployed server remain compatible. Entity IDs are allocated normally; setting names identify records across restarts and subscriptions.

The database holds background/text/checkbox/checkmark colors, checkbox size and stroke widths, font size, margins, row spacing, minimum row height, hit size, text width/inset/gap, and the add-row label. Defaults are inserted only when missing. Geometry uses logical points; platform density and safe-area insets are device properties. Invalid numeric values fall back or clamp at the rendering boundary.

The app opens its own platform-backed identity (`drawdo-app-v1`), retains its local database, and pushes that namespace to `147.182.181.182:7777`. `public-key.txt` in the app's private files directory identifies it. Subscribers receive task and visual-setting facts through the same sync. A server outage leaves local editing available and the network retries automatically. The server is a cache; this app does not adopt a friend's namespace as its writable store.

This screen edits the owner's list. A friend picker, subscription UI, shared-device identity setup, and CRDT task-title editing are later increments. The model can already open a subscribed store read-only; the network test exercises that path.

## Structure

- `src/main.c` owns the database/network lifetime and the top-level interaction struct, and dispatches native editor/accessibility events.
- `src/model.c` projects Datafriend watches into reusable style and visible-row caches. Its ordered-list watch excludes titles and completion flags, so those edits do not rebuild the ID list. Settings are watched separately. Only mounted rows get title/done watches.
- `src/screen.c` derives layout, checkbox geometry, labels, hit targets, and accessibility nodes from the model. Dirty changes rebuild the scene; unchanged frames reuse geometry, textures, and GPU buffers without database queries or text rasterization.
- `src/render/` adapts the demo's bounded text cache and shaders. Native Android/iOS text rasterization matches the text-buffer editor's font and wrapping metrics. White glyph textures are tinted at draw time, so color and completion changes reuse them. The texture budget is 8 MiB.
- `../native/loop_*` supplies the shared event-driven loop used by this app and `crdt-demo-app`. Socket readiness, network deadlines, editor mailboxes, and accessibility actions wake the owning thread independently of rendering. Sokol schedules frames (Choreographer on Android API 29+).
- `../text_buffers` and `../accessible_ui` provide native editing and semantic controls. Cursor, selection, composition, and unsubmitted drafts remain local interaction state. Keyboard appearance does not move the list.

On save, the submitted editor remains visible until the replacement canvas frame
is presented. Android uses EGL presentation timestamps when supported (verified on
the Moto G7); drivers without them use a conservative swapchain/GPU-drain fallback.
The iOS adapter uses Metal's presented callback. Presentation tracking runs only
during the handoff. No network acknowledgment is needed to dismiss the editor.

For example, changing the checkbox size is a normal persisted edit:

```c
todo_set_style(&model, TODO_UI_CHECKBOX_SIZE, df_real(30));
todo_set_style(&model, TODO_UI_CHECKBOX_COLOR, df_int(0x2266ccff));
```

## Build and validation

From the workspace root:

```sh
python3 DrawDo/tools/build_android.py run --serial ZY227364TP
cmake -S DrawDo -B DrawDo/build/host -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build DrawDo/build/host
ctest --test-dir DrawDo/build/host --output-on-failure
python3 DrawDo/tests/android/run.py --serial ZY227364TP
```

The Android runner exercises native editing, Unicode, checkboxes, deletion, wrapping, accessibility activation, tap cursor placement, and stable text origin with the keyboard open. It leaves one `DrawDo phone test` task for the optional server subscription check. Screenshots are saved in the app's private files directory. Run the runner with `--mode verify` to check it after a cold restart, then `--mode cleanup` to remove that test task; tests never clear the application's database.

`network_test HOST PORT` creates an ephemeral owner and subscriber and checks task edits/deletion plus checkbox size/color replication. `network_test HOST PORT PUBLIC_KEY TITLE` checks the phone's namespace, expecting one unchecked task with that title and default checkbox styling.

The CMake project also includes the iOS Metal/UIKit target and `tools/build_ios.py`. **Only Android was built and tested for this increment.** No iOS build or device deployment was performed.
