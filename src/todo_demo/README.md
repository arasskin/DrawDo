# Datafriend todo demo

A Flutter macOS demo using inline Datafriend queries and direct triple edits.
It runs locally without accounts or network sync.

Add, complete, rename and delete tasks; filter active/completed tasks. The UI in
[`todo_screen.dart`](lib/todo_screen.dart) uses `DatafriendBuilder` for its list
and completion count. Each builder owns its subscription, reads lazy entity
accessors, and schedules rebuilding when its result changes. Callbacks call
`database.edit` directly. There is no app controller or task-model conversion in
the interactive UI. The attribute-level storage format uses a fresh `datafriend-v3`
directory; existing demo directories are retained without automatic migration.

The reusable wrapper is in
[`datafriend_builder.dart`](../app/lib/data/datafriend_builder.dart); usage and
lifetime rules are documented [here](../../docs/datafriend-widget-builder.md).

The list now queries only order fields, while each mounted task row queries its
own title and completion state. Title edits rebuild one row; completion edits
also rebuild the counter. Filtered lists rebuild when completion changes membership.
Fixed-entity subscriptions now route by entity and attribute; the
[matched engine comparison](../../docs/datafriend-fixed-routing.md) records the
latest timings and verifies that these rebuild boundaries remain unchanged.

For the [plain Flutter comparison](../../docs/datafriend-pure-flutter.md):

```sh
python3 src/todo_demo/tool/run_widget_benchmarks.py \
  --samples 20 --uis flutter rows --output-prefix datafriend-pure-flutter
```

`pure_flutter_screen.dart` owns mutable task objects in a `StatefulWidget` and
uses screen-level `setState`. It derives its filtered/sorted list and completion
count during build, with the same lazy rows and layout. It is in-memory only;
Datafriend retains its database and async flushing. Both callback and frame
times are reported, and each rendered edit and rebuild scope is checked.

For the current [per-row query benchmark](../../docs/datafriend-row-queries.md):

```sh
python3 src/todo_demo/tool/run_widget_benchmarks.py --samples 20
```

This compares the previous full-list query (`bulk_query_screen.dart`) with the
current per-row queries using the same layout. It measures completion, title and
order edits at 100 and 1,000 tasks, checks rebuild counts for every action,
validates rendered updates and persistence, and records edited-frame timings.
The earlier [inline-vs-controller measurements](../../docs/datafriend-widget-benchmark.md)
describe the previous UI.

Datafriend now flushes disk writes asynchronously by default. The latest
[Datafriend-only benchmark](../../docs/datafriend-async-flush.md) compares async,
synchronous and unflushed diagnostic modes, including per-edit frame timing:

```sh
python3 src/todo_demo/tool/run_flush_benchmarks.py
```

The InstantDB comparison below is historical; its measurements preceded the
asynchronous-flush default. Its old UI remains in `comparison_screen.dart` and
its adapters remain in `stores.dart` for reproduction. The interactive demo no
longer exposes that backend. Datafriend frame benchmarks render the new wrapper
UI, while store workloads and the benchmark's edit driver retain their adapters;
old frame measurements therefore describe a different UI.

## Build and open

Requires Flutter with macOS support, Xcode, CocoaPods, CMake and a C++ compiler.
From the repository root, with Flutter on PATH:

```sh
python3 src/todo_demo/tool/build_demo.py
open src/todo_demo/build/macos/Build/Products/Release/datafriend_todo_lab.app
```

Use `--flutter /path/to/flutter` if needed. The script builds and bundles the
Datafriend native library and signs the app locally. This is a local demo bundle,
with the macOS app sandbox disabled, not a distribution build. Only macOS is
scaffolded and tested here.

Interactive data lives under the app's application-support directory in
`todo-lab/datafriend-v3` and `todo-lab/instantdb`. Benchmarks use fresh temporary
folders and do not touch those lists or the main DrawDo app.

## Reproduce the comparison

```sh
python3 src/todo_demo/tool/run_benchmarks.py
# Reuse an already built Release app:
python3 src/todo_demo/tool/run_benchmarks.py --skip-build
# Short end-to-end check:
python3 src/todo_demo/tool/run_benchmarks.py --skip-build --smoke --output-prefix datafriend-instantdb-smoke
```

The full run uses 100 and 1,000 tasks, three interleaved rounds, 30 measured
operations after five warmups per workload, and buffered/default-durable writes.
It separately measures function return, transaction completion, reactive query
publication, and reading 15/all rows. It also renders the actual shared UI and
records Flutter frame timings. Each process checks CRUD, read checksums and reopen
persistence before reporting success. Raw samples, a CSV summary, source/binary
hashes and environment metadata go into `docs/benchmarks/`; screenshots go into
`docs/validation/todo-demo/`.

The adapter explicitly sorts published rows to work around the package's ordering
bug; that sorting is included in the timings. The package's built-in 200 ms query
debounce remains unchanged. Local write
completion and query publication are deliberately measured separately. The
buffered SQLite profile disables synchronous flushing; the normal app retains
backend defaults. Neither profile benchmarks networking or a crash-recovery
durability guarantee. See [results and limitations](../../docs/datafriend-instantdb.md).

## Structure and tests

- `lib/stores.dart`: real Datafriend and InstantDB adapters, lazy task projections.
- `lib/todo_screen.dart`: inline Datafriend queries and per-row subscriptions.
- `lib/pure_flutter_screen.dart`: package-free, in-memory `setState` baseline.
- `lib/change.dart`: common transaction/publication measurement boundary.
- `lib/benchmark.dart`: Release-app benchmarks and correctness checks.
- `test/`: real store CRUD/reopen tests and shared UI interaction test.

```sh
cd src/todo_demo
flutter analyze
flutter test
```

Store tests use `../../datafriend/build/native/libdatafriend.dylib` (build that native
CMake target first). InstantDB store tests use SQLite FFI; the Release benchmark
uses the real `sqflite_darwin` platform plugin. The demo imports existing Datafriend
bindings and projections from `src/app`; it does not replace the main app.
