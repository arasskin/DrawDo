# littlebear-benchmarks

This is a linked worktree of `../littlebear`, checked out on branch `benchmarks`.
It contains a complete source checkout plus the benchmarks and experimental
implementations removed from `main`. It builds against the sibling benchmark
worktree, leaving the production checkouts independent.

Run benchmark commands here (for example `make -C littlebear-benchmarks benchmark`).
Production changes can be brought over with `git cherry-pick <commit>` for the
relevant fixes. The branch split intentionally removes experiments from `main`;
avoid merging that removal commit into this branch.

Previous generated outputs and comparison baselines are preserved in
`build/archive`. Their old CMake caches contain former absolute paths: build fresh
outputs outside the archive. The archived `benchmark-working-files` directory
also preserves ignored scratch files from the original benchmark folder. The
verified upstream reference sources live at `../datafriend-benchmarks/build/reference`.
No remotes are configured and no changes are pushed.

# Littlebear

Littlebear is a disposable namespace cache. Owners push their authoritative state;
public subscribers reconcile against the cache. A missing namespace preserves a
subscriber's last known state, while an existing empty namespace clears it.

One Linux io_uring loop drives TCP and file I/O. Namespaces share persistent
connections; the server starts subscriber reconciliation after updates. Record
IDs and fingerprints are separate 8-byte values. The target is one CPU, roughly
500 MB RAM and 10 GB disk. See the [wire protocol and client API](../docs/datafriend-network.md).

## Boundaries

`server.cpp` is the orchestration point: parse an envelope, select a channel,
verify an owner write, dispatch sync work, and submit I/O. Auth cannot call sync
or storage. Sync cannot access sockets, signing keys, or io_uring.

| File | Responsibility |
| --- | --- |
| `src/server.cpp` | Connections, channels, request dispatch, subscription scheduling, io_uring completions |
| `src/protocol.h` | LBS5 envelope parsing, field validation and limits; input views borrow the frame |
| `src/auth.h` | Namespace digest, Ed25519 verification, challenge and sequence state |
| `src/sync.h` | Namespace revisions, reconciliation and record transfer over explicit `lb_sync::state` |
| `src/task.h` | Coroutine lifetime and pending file requests; submitted buffers survive cancellation |
| `src/exchange.h` | Bounded packet/difference collection around the reconciliation algorithm |
| `src/reconcile.h` | Allocation-free reconciliation using source callbacks |
| `src/index_sync.h` | Adapter from the record index to reconciliation callbacks |
| `src/storage.h` | Namespace/record lifecycle, value references and asynchronous I/O state |
| `src/storage_uring.h` | Translate prepared storage requests into kernel submissions |
| `src/namespaces.h` | Compact namespace directory; owns relocatable index headers |
| `src/index.h` | Explicit-lifetime facade over the shared wide tree |
| `src/heap.h`, `src/pool.h` | Size-class value-file allocator and its metadata pool |
| `src/codec.h` | Shared byte reader/writer, also used by Datafriend's native boundary |

The main dependency paths are:

```text
server -> protocol, auth, sync, storage_uring
sync -> exchange, index_sync, storage, task
exchange -> reconcile, protocol
index_sync -> reconcile, index
storage -> namespaces -> index
storage -> heap -> pool
```

`index.h` is the only production facade over Datafriend's shared wide tree and
node allocator. The application schema is supplied at the server entry point;
protocol parsing accepts it as a parameter. Vendor code lives under `third_party`
(xxHash, unordered_dense); licensing is recorded in [NOTICE.md](NOTICE.md).
There are no production dependencies on the benchmark/reference directory.

Code uses structs and namespace functions. Destructors are retained where they
protect owned trees, coroutine buffers, and cancellation cleanup. This cleanup
preserves the LBS5/LBR3 formats and storage layout.

## Build and verify

On Linux, install a C++23 compiler, liburing and libsodium development packages:

```sh
make -C littlebear
littlebear-benchmarks/build/server --values /tmp/littlebear.values --port 7778
make -C littlebear-benchmarks test
make -C littlebear-benchmarks sanitize
make -C littlebear-benchmarks debug
python3 scripts/test_littlebear_tcp.py littlebear-benchmarks/build/server
```

The default build produces the real `build/server`; `debug` produces
`build/server-debug` with ASan/UBSan. `BUILD=build/linux` selects a separate output
directory. Compiler-generated dependency files track the headers each target uses.
Portable component tests also run on macOS with `CXX=clang++`; real io_uring tests
run on Linux. The [deployment unit](../deploy/littlebear.service) uses the same
server, with an explicit port of 7777. [Deployment setup](../docs/deployment.md).

`benchmark`, `benchmark-table`, and `benchmark-reconciliation` run the component
benchmarks. Old binary-tree/arena/table implementations remain only in
`benchmarks/reference`, because comparison benchmarks still use them. Their
regressions are in `tests/reference`. The optional
`benchmark-reconciliation-reference` fetches its pinned comparison into the build
cache; normal builds do not fetch dependencies. The echo prototype and unused
allocator/parser stubs have been removed.

The [cleanup validation report](../docs/validation/littlebear-cleanup-linux.json)
records native, Linux transport, sanitizer, and client integration checks.
