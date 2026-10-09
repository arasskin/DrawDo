# Datafriend

Datafriend is the C++ store used directly by the [Sokol demo](../demo-app/README.md).
Dart/Flutter clients are no longer supported. It owns local facts, query indexes,
shared live results, reconciliation state and owner identities. Local edits block
until affected results are published; disk flushing is asynchronous by default.
The Sokol demo exercises local storage, queries and signed document sync through
the native nonblocking TCP client. The earlier Dart adapter is unsupported.

## Execution and subscriptions

Datafriend executes directly in the application's serialized calling context.
There is no private engine thread, job queue or future per call. Edits commit and
update affected live results before returning; callers then dispatch their UI
notifications. Serialize all engine calls, result reads, network imports and
close operations. Network I/O may run asynchronously, but its received updates
must enter through that same application context. The disk-flush worker only
syncs the file descriptor and never touches queries, indexes or result maps.

```cpp
auto [id, result] = db.subscribe(query);
// result is already evaluated; copies share the live map without copying facts.
db.modify(changes);
// result now reflects the published changes.
db.unsubscribe(id);
// A retained result keeps its final contents after the last owner unsubscribes.
```

`subscribe` returns `std::pair<uint64_t, query_result>`. Independent subscriptions
have distinct IDs; identical queries share their result storage. `result(id)`
remains available for handle-based clients. The retained legacy C wire entry points still return an ID. They are not the
supported application interface, and Dart/Flutter compatibility is not a design
constraint.

## Source layout

| Path | Responsibility |
| --- | --- |
| `include/datafriend/engine.h` | C++ store, query and live-result API |
| `include/datafriend/network.h`, `identity.h` | Native network and secure owner identity APIs |
| `src/network.cpp` | Nonblocking sockets, protocol state, scheduling and reconnect |
| `include/datafriend/c_api.h` | Legacy C ABI entry points |
| `include/datafriend/wire_codec.h` | Shared Littlebear binary codec names |
| `include/datafriend/attributes.h` | Generated shared attribute IDs |
| `src/engine.cpp` | Store ownership, transactions, indexes, queries and reconciliation |
| `src/async_flush.h` | Background disk-flush worker |
| `src/reconciliation_snapshot.h` | AELMDB read snapshot used during a sync exchange |
| `src/owner_identity.cpp` | Private signing state and validated owner-write signing |
| `src/c_api.cpp` | All C ABI entry points, including identity operations |
| `platform/android/` | Keystore bridge, Java implementation and packaging rules |
| `platform/apple/` | Keychain bridge and CocoaPods-only vendor compilation adapters |
| `include/datafriend/wide_tree.h`, `tree_allocator.h` | Header-only index implementation reused by Littlebear |
| `schema/`, `tool/` | Shared attribute schema and generator |
| `tests/` | Engine, flushing, identity, allocator and reconciliation correctness |

The core does not depend on an app, renderer, Dart runtime or benchmark worktree.
AELMDB and Monocypher compile as explicit CMake dependency targets. Platform
identity storage is selected at build time. Reconciliation uses the transport-
independent headers in `littlebear/src`; the server in turn reuses the standalone
wide-tree headers, not the Datafriend engine/library. Mutable views use dense maps,
not that wide tree.

The small Apple vendor files exist because CocoaPods collects sources beneath
its pod root. CMake compiles upstream C files directly. Monocypher's core and
Ed25519 extension must remain separate translation units because they contain
conflicting private function names. The C ABI previously split identity into a
second wrapper file; it is now one implementation with shared buffer handling.

See the [repository dependency map](../architecture.visualize),
[network contract](../docs/datafriend-network.md),
[C ABI contract](../docs/datafriend-native.md), and [source notices](NOTICE.md).

## Build and check

```sh
make -C datafriend native
make -C datafriend test
# Linux ASan/UBSan:
make -C datafriend sanitize
```

`BUILD=build/native` and `JOBS=2` are configurable. Direct CMake:

```sh
cmake -S datafriend -B datafriend/build/native -DCMAKE_BUILD_TYPE=Release
cmake --build datafriend/build/native -j 4
ctest --test-dir datafriend/build/native --output-on-failure
```

Set `DATAFRIEND_TESTS=OFF` for library-only builds. The target
`datafriend::datafriend` supplies public includes; the shared library remains
`libdatafriend` and the C ABI is unchanged. `Datafriend.podspec` packages Apple
clients. Performance experiments remain in the separate
[datafriend-benchmarks](../datafriend-benchmarks/README.md) and
[littlebear-benchmarks](../littlebear-benchmarks/README.md) worktrees.

CMake defaults to a shared library. The iOS Sokol client sets
`DATAFRIEND_SHARED=OFF` for static linkage and selects `MDB_USE_POSIX_SEM=1` for
AELMDB, since iOS does not support its Darwin-default System V semaphore path.
The CocoaPods integration selects the same semaphore backend.


The native network client is `datafriend/network.h`: attach owner/replica stores,
then call `pump()` on the application thread. It detects local edits by revision,
handles signed writes and server-initiated reconciliation, and calls publication
callbacks after remote commits. `datafriend/identity.h` provides the secure native
owner identity. See [API and protocol](../docs/datafriend-network.md).

`build/native/transport_test HOST PORT` exercises the production TCP server with
ephemeral identities and synthetic data. It is separate from CTest because it
requires a running endpoint. The app-level CRDT is intentionally outside this library.
