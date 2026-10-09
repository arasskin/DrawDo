# Direct view edits and Flutter publication

Current implementation: [live results and query callbacks](datafriend-live-results.md).
The retained-root/publication-stamp details below describe the previous implementation.

Implemented 2026-09-27. UI callbacks now submit explicit changes to Datafriend
views. The production controller no longer constructs a new AppDocument, diffs
it into triples, or reconstructs a complete document before returning.

## Built-in mechanism chosen

Use **ChangeNotifier + ListenableBuilder**, both included in the Flutter SDK.
The app already had a root ListenableBuilder; it now listens to publication of
native state and reads lazy projections during the resulting build.

| Mechanism | Fit here |
| --- | --- |
| `State.setState` | Smallest choice for state owned by one widget. Shared callbacks and view state would need to carry the owning widget's notification callback. |
| `ChangeNotifier` + `ListenableBuilder` | Fits one stable controller/view owner. Notify after publication; Flutter schedules a subtree rebuild. No new package, stream, or copied state value is needed. |
| `ValueNotifier` + `ValueListenableBuilder` | Fits replacement of one immutable value. A stable native view handle changes contents without being replaced; explicit change notification is the more direct fit. |

Flutter documents [setState's synchronous callback and rebuild scheduling](https://api.flutter.dev/flutter/widgets/State/setState.html),
[ChangeNotifier's change-notification interface](https://api.flutter.dev/flutter/foundation/ChangeNotifier-class.html),
[ListenableBuilder's subscription and subtree rebuild behavior](https://api.flutter.dev/flutter/widgets/ListenableBuilder-class.html),
and [ValueNotifier's equality-based notification limitation](https://api.flutter.dev/flutter/foundation/ValueNotifier-class.html).
These are Flutter framework facilities; Dart alone does not observe arbitrary
object changes automatically.

## Flow and responsibilities

1. The widget callback invokes a direct operation, such as toggling a task or
   replacing its title. AppViews resolves its native entity identity. A toggle
   reads the current `done` pair through the new `view.get(entity, attribute)`
   operation, so two callbacks before a build still use current state.
2. `view.edit` blocks through native reconciliation, entity/index changes, query
   evaluation and publication of every affected root.
3. Successful Dart edit/import calls advance a local publication stamp and invoke
   the owning UI bridge. Failed calls do neither. Controller-managed actions
   suppress the intermediate hook and notify once after their error status has
   also settled. Direct view edits and imports use the same bridge automatically.
4. ChangeNotifier tells ListenableBuilder to schedule a build. The mutation
   callback can finish without reading the new task collection.
5. During the build, widgets read `controller.tasks`, `.stickers` or `.strokes`.
   AppViews checks retained root identity after a publication and decodes only
   changed projections. Repeated reads at the same publication reuse their cached
   projection. A read-only `.document` getter remains for export and tests.

The native library and its Dart FFI facade remain independent of Flutter. The
facade exposes one plain `onPublished` callback for the owning bridge, plus a
read-only invalidation stamp. The callback must not throw; Flutter's notifier
handles errors from registered UI listeners. Dispose disconnects the bridge
before releasing the query handles, retained roots and native engine.

A successful no-op may notify; native root identity still prevents unnecessary
projection decoding. Initial bootstrap is one explicit batch. Adds, reorders,
deletes and drawing operations read the relevant view when membership/order is
needed to construct the requested changes. Drawing inputs are retained for retry
and encoded only for the new stroke, in bounded chunks.

The old whole-document adapter moved to `src/app/tool/support/legacy_document_store.dart`
solely to preserve historical benchmark comparisons. JSON/memory stores remain
standalone fixtures and are not part of the production controller.

This initial implementation deliberately uses one app-level notifier. Finer
per-view widget subscriptions can be introduced later if frame measurements
justify them. No optimistic document cache or asynchronous UI edit queue was added.
Future native background network events still need delivery onto the Dart isolate;
they must enter this same publication bridge after updating the views.

## Comparison

Three rotated AOT rounds, each with 100 warmups and 1,000 samples, file-backed
MDB_NOSYNC. Both variants use the current native library, identical task schemas,
three stickers and an unchanged 5,000-point drawing. Median of run medians:

| Operation | 100 tasks | 1,000 tasks |
| --- | ---: | ---: |
| Historical document adapter, mutation + document readback | 416 µs | 3,984 µs |
| Direct AppViews mutation body, no collection readback | **59 µs** | **389 µs** |
| Direct mutation + complete read-only document projection | **277 µs** | **2,635 µs** |

The direct mutation includes current-value lookup, edit encoding, native commit
and publication, and one publication listener. This is the same AppViews method
called by the controller; standalone AOT timings exclude Flutter's notifier,
widget scheduling and rendering. The readback comparison keeps deferred work
visible: total mutation-plus-readback drops about **33%**, while callbacks return
much earlier. Full changed-view decoding is still a read cost; it was not made free.

All 24 benchmark processes passed state, retained-drawing and notification-count
checks, including odd-parity short runs. Raw [results](benchmarks/datafriend-direct-views.csv)
and [run metadata](benchmarks/datafriend-direct-views-metadata.json) are retained.

```sh
cmake --build datafriend/build/native -j 4
python3 datafriend-benchmarks/benchmarks/run_direct_views.py --dart /path/to/dart
```

## Validation

Native engine/tree tests pass, including the new C ABI pair lookup and truncated
lookup requests. All 17 Flutter tests pass with instrumentation enabled and
disabled; Flutter analysis is clean. Android arm64 and iOS simulator app builds
succeed. Tests
cover direct edits, multiple toggles before a build, rollback, no eager snapshot
export on the toggle path, atomic cross-view visibility, notifications after
imports, persisted reopen, order changes, drawing chunks and unchanged projection
reuse. Widget tests now use real native storage. A checkbox test verifies that
native state changes before pumping, while the rendered checkbox updates only
after Flutter builds the next frame.
