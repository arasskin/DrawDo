# DrawDo

DrawDo is a fresh rebuild of iCare for iOS and Android: tasks, friends, drawing,
and playful physical interactions, backed by littlebear.

The supported application is **C99 with Sokol** on iOS and Android.
Dart and Flutter clients are no longer supported; their code is historical reference. The
recovered native iOS app in [`reference/junstodos/`](reference/junstodos/README.md)
provides the starting point for platform and rendering code; see the
[Sokol client direction and Android assessment](docs/sokol-client.md).

The **Datafriend C99 library** owns local persistence, queries, mutable live views,
and native identity operations. Its Littlebear protocol supports Ed25519 owner
writes; the Sokol app now uses native nonblocking TCP orchestration. See the
[network interface](docs/datafriend-network.md) and
[two-phone document demo](docs/datafriend-document-sync.md).
Reconciliation uses a custom negentropy variant with 64-bit xxHash/XOR
fingerprints. Littlebear is a C99 server with its custom storage design.
See the [Datafriend design](docs/datafriend-design.md).

A standalone native Sokol task list and particle app
now run on Android and iOS, using Datafriend directly. Build and launch with
`make -C demo-app android-run` or `make -C demo-app ios-run`; see
[native setup](demo-app/README.md) and [phone comparison](docs/datafriend-ios.md).

## Repository

This enclosing repository contains the component sources together, including
Littlebear, Datafriend, the apps, and their shared native libraries. The local
component Git repositories are organizational tools; their source files are
tracked here directly, rather than as submodules. Clone this workspace to get
the sibling dependencies needed by the builds.

| Path | Contents |
| --- | --- |
| `DrawDo/` | C99/Sokol todo screen with Datafriend-backed tasks and appearance; Android tested, iOS target included |
| `crdt-demo-app/` | Minimal Sokol app: one shared text buffer, white background, black text |
| `text_crdt/` | Shared C99 wrapper around the pinned Yrs text CRDT |
| `demo-app/` | C99/Sokol task, particle and shared-document demo, Android/iOS shells, host tests and phone profiling |
| `src/app/` | Unsupported historical Dart/Flutter client and comparisons |
| `datafriend/` | C99 store/query engine, live results, C/C++ interfaces and platform key storage |
| `containers/` | Shared header-only C99 arrays, buffers, maps, trees and pools |
| `littlebear/` | C99 namespace cache and TCP/io_uring sync endpoint |
| `littlebear-benchmarks/` | Littlebear worktree on `benchmarks`, with experiments and comparisons |
| `datafriend-benchmarks/` | Datafriend worktree on `benchmarks`, with experiments and profiling |
| `third_party/unordered_dense/` | Historical C++ adapters/tests map and MIT license |
| `littlebear/src/reconcile.h` | Allocation-free reconciliation with separate 64-bit IDs and fingerprints |
| `assets/` | iCare illustrations, animations, and icons for the new client |
| `text_buffers/` | C99 text buffers and native inline editors |
| `accessible_ui/` | Native accessibility for custom drawn controls |
| `clipboard/`, `haptics/` | Small native service libraries with C interfaces |
| `native/` | Shared native bridge helpers and event-loop adapters |
| `reference/junstodos/` | Recovered C++/Sokol iOS app, text renderer and platform code |
| `reference/icare/` | ClojureDart client and Clojure backend source snapshot for porting behavior |
| `docs/starting-point.md` | Current state, architectural decisions, and first development milestones |
| `docs/datafriend-design.md` | Datafriend ownership, triple/entity synchronization, subscriptions, and performance plan |
| `docs/littlebear-checklist.md` | Implementation checklist for an operational backend |
| `docs/littlebear-components.md` | Component contracts, tests, and allocator measurements |
| `docs/provenance.md` | Import sources and revisions |

See [the source dependency map](architecture.visualize) and each component’s README
for file responsibilities and build commands.

## Development direction

- Build the new UI and interaction logic in C99 with Sokol, reusing the recovered
  native foundation. Keep persistence, queries, identity and sync in Datafriend.
- Retain the unsupported Flutter source as historical reference while building native
  rendering and platform services on both phones.
- Keep platform services and native-library bindings behind small interfaces.
- Develop the client locally while littlebear is being repaired, with injectable
  storage and in-memory substitutes for tests.
- Continue developing littlebear's custom data structures deliberately, with
  correctness tests before integration.
- Preserve the original projects in `summer` as reference material.

Littlebear targets Linux with C99, liburing and libsodium (C++23 is used for model tests). `make -C littlebear`
builds the TCP storage server at `littlebear/build/server`; `make -C littlebear
debug` builds it with sanitizers. One io_uring loop handles sockets and files.
See [Littlebear's module boundaries and build commands](littlebear/README.md).

The internal storage index now uses the mutable hash-ranked wide tree with a
slab pool shared across namespaces. Leaves hold packed value-file references;
nodes cache counts and XOR summaries. Namespace headers remain 24 bytes.
The namespace directory uses dense entries with a linear-probing array of
32-bit indices and no application namespace-count limit. See [the storage contract](docs/littlebear-storage.md).

`make -C littlebear test` runs storage component checks, `make -C littlebear
sanitize` runs their sanitizer builds. Benchmarks run from the dedicated
worktrees, for example `make -C littlebear-benchmarks benchmark`. Debug output is `littlebear/build/server-debug`.

The original iCare source is reference material, not a buildable Flutter project
in this repository. Generated Dart, platform projects, local databases, and
credentials have not been imported.
