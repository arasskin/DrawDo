# DrawDo first screen — 2026-10-21

Built and installed `dev.drawdo.app` on the Moto G7 (`ZY227364TP`, Android 10), using the Android 29 arm64 target. APK size: about 0.7 MiB. The shared C99 application and Android build passed; iOS sources/build configuration were added but no iOS build, test, or deployment was performed.

Verified:

- Model: default seeding, settings identified by name rather than fixed entity IDs, add/edit/toggle/delete, stale-editor deletion protection, read-only ownership, numeric-style fallback/clamping, settings persistence, and repairing a missing setting value.
- Query behavior: title/completion edits do not republish the ordered list; offscreen title edits do not publish mounted-row watches; style changes do not republish task watches.
- Android: native text editing, Unicode, multiline wrapping, accessible checkbox/title/add actions, completion state, empty-title deletion, cursor placement near a physical tap, unchanged text origin with the keyboard open, and a reachable Done key for multiline submitted editors. A cold restart retained the task and completion state.
- Network: a local owner and read-only subscriber exchanged task creation, rename, completion, deletion, checkbox size, and checkbox color through the isolated Linux/io_uring server. An independent host subscriber also received the Android phone's task and style facts through the deployed server at `147.182.181.182:7777`.
- Shared regressions: accessibility event notifications and queue backpressure passed. The CRDT demo's Android native target compiled after moving its unchanged loop adapters to `native/`.

The wire schema was left unchanged. UI configuration uses existing `name`/`value` facts, so no server upgrade was necessary. The app publishes only its own namespace; a subscription screen and cross-device identity setup remain future increments.

Evidence: [Android tests](drawdo/android.txt), [restart test](drawdo/persistence.txt), [network test](drawdo/network.json), [deployed server](drawdo/deployed-sync.txt), [todo screen](drawdo/todos.png), [keyboard](drawdo/keyboard.png).

The temporary task used for validation was deleted afterward. Scrolling and drag reordering are intentionally absent from this first screen.
