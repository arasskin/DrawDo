# Littlebear

Littlebear is a disposable namespace cache. Owners push their authoritative state;
public subscribers receive fact batches and reconcile against the cache when
their full fingerprints disagree. A missing namespace preserves a
subscriber's last known state, while an existing empty namespace clears it.

One Linux io_uring loop drives TCP and file I/O. Namespaces share persistent
connections; the server starts subscriber reconciliation after updates. Record
IDs and fingerprints are separate 8-byte values. The target is one CPU, roughly
500 MB RAM and 10 GB disk. See the [wire protocol and client API](../docs/datafriend-network.md).

## Boundaries

`server.c` is the orchestration point: parse an envelope, select a channel,
verify an owner write, dispatch sync work, and submit I/O. Auth cannot call sync
or storage. Sync cannot access sockets, signing keys, or io_uring.

| File | Responsibility |
| --- | --- |
| `src/server.c` | Connections, channels, request dispatch, subscription scheduling, io_uring completions |
| `src/protocol.h` | LBS5 envelope parsing, field validation and limits; input views borrow the frame |
| `src/auth.h` | Namespace digest, Ed25519 verification, challenge and sequence state |
| `src/sync.h` | Namespace revisions, reconciliation and record transfer through explicit `lb_service` and `lb_task` state |
| `src/exchange.h` | Bounded packet/difference collection around the reconciliation algorithm |
| `src/reconcile.h` | Allocation-free reconciliation using source callbacks |
| `src/index_sync.h` | Adapter from the record index to reconciliation callbacks |
| `src/storage.h` | Namespace/record lifecycle, value references and asynchronous I/O state |
| `src/storage_uring.h` | Translate prepared storage requests into kernel submissions |
| `src/namespaces.h` | Compact namespace directory; owns relocatable index headers |
| `src/index.h` | Relocatable 24-byte tree header and explicit-lifetime index API |
| `../containers/tree.h`, `../containers/pool.h` | Shared C99 wide tree and slab node allocator |
| `src/cpp/` | C++ adapters for Datafriend and existing correctness tests |
| `src/heap.h`, `../containers/arena.h` | Size-class value-file allocator and its metadata pool |
| `src/codec.h` | Shared byte reader/writer, also used by Datafriend's native boundary |

The main dependency paths are:

```text
server -> protocol, auth, sync, storage_uring
sync -> exchange, index_sync, storage
exchange -> reconcile, protocol
index_sync -> reconcile, index
storage -> namespaces -> index
storage -> heap -> containers/arena
index -> containers/tree, containers/pool
```

The runtime is C99: structs, prefixed functions, explicit initialization and
cleanup, and error returns. `lb_task` owns the batch, response and file buffers;
its state machine suspends on one prepared file request at a time. Disconnects
retain submitted buffers until their completions arrive. No coroutines, exceptions,
STL containers or C++ runtime are used by the server.

The shared tree and reconciliation implementations are C99 headers. Datafriend's
C++ interfaces adapt those implementations rather than maintaining another tree.
Tree records remain 24 bytes, node headers 24 bytes, and slots 8 bytes. Mutations
retain allocation-failure rollback and cached count/XOR summaries.

The namespace directory uses dense entries and a linear-probing array of 32-bit
indices. Entries also own revisions, writer state and bounded delta buffers,
replacing the separate C++ metadata maps. Insertions and removals can move entries;
resumed requests always look them up again by namespace ID.

The entry point supplies the generated `datafriend/schema.h` fingerprint to the
protocol parser. xxHash remains the hash dependency; licensing is recorded in
[NOTICE.md](NOTICE.md). The LBS5/LBR3 formats, signed challenges, optimistic updates,
heartbeat and empty-versus-absent namespace behavior are preserved.

## Build and verify

On Linux, install a C99 compiler, liburing and libsodium development packages.
The existing model tests also require a C++23 compiler:

```sh
python3 datafriend/tool/generate_attributes.py --check
make -C littlebear
littlebear/build/server --values /tmp/littlebear.values --port 7778
make -C littlebear test
make -C littlebear sanitize
make -C littlebear debug
python3 scripts/test_littlebear_tcp.py littlebear/build/server
```

The default build produces the real `build/server`; `debug` produces
`build/server-debug` with ASan/UBSan. `BUILD=build/linux` selects a separate output
directory. Compiler-generated dependency files track the headers each target uses.
The server and the direct C API test compile with `-std=c99 -pedantic-errors`.
The other tests use C++ containers as independent models through adapters.
Portable component tests also run on macOS with `CXX=clang++`; real io_uring tests
run on Linux. The [deployment unit](../deploy/littlebear.service) uses the same
server, with an explicit port of 7777. [Deployment setup](../docs/deployment.md).

Performance experiments, historical trees/arenas, and reference tests live in the
sibling [littlebear-benchmarks](../littlebear-benchmarks/README.md) Git worktree on
branch `benchmarks`. This `main` checkout builds only the server and production
correctness tests. It has no dependencies on either benchmark worktree.

See the [C99 port validation](../docs/validation/littlebear-c99.md) for the tested
configurations and the narrow before/after tree comparison.

## Channel storage

Each connection grows a dense channel array on demand. A shared `ct_map` maps
opaque nonzero 32-bit wire IDs to array positions; sparse IDs do not allocate
sparse storage. Active work holds an index across I/O, not a pointer into a
relocatable array. Unsubscribe removes the mapping, swaps the final entry into
the hole and repairs its index. Disconnect frees the array and map.
Subscription delivery rotates its starting position. The fixed 64-channel cap
is gone; allocation and container/protocol integer ranges remain the bounds.
The separate eight-client connection limit and packet/queue limits are unchanged.
