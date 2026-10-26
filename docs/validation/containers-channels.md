# Shared containers and dynamic channels — 2026-10-18

Both networking endpoints now grow channel storage on demand. The client keeps
stable, nonzero 32-bit IDs across growth and reconnect; the server maps arbitrary
wire IDs to dense array positions. Active server requests retain an index across
I/O. Server unsubscribe repairs the index after swapping out a removed channel.
Disconnect frees channel storage. Both schedulers rotate their starting point.
Client callbacks can append channels without leaving a dangling pointer in the
publication path; queued-opening buffers retain their capacity after publication.

`containers/` contains header-only C99 array growth, binary buffers, a dense
integer map, the shared wide tree, its slab allocator, and the fixed-slot arena.
The common primitives use `ct_` names. The app reuses array growth, both codecs
reuse buffer storage, and Datafriend plus the server channel index use the map.
Tree algorithms, layouts and provenance are retained. Protocol limits, namespace
ownership, file storage and GPU resource caching remain in their own modules.
The C++ test adapters point to the shared C sources. Benchmark worktrees and
historical clients were not migrated.

## Passed checks

- Strict C99 shared-header test on macOS and Linux: growth, preserved contents,
  integer overflow, allocation-failure rollback, bounded/sticky-error buffers,
  map allocation failures and moved entries.
- Seven Littlebear suites on macOS and Linux, including tree/table models,
  allocation-failure sweeps, reconciliation, storage lifetime and real io_uring.
- Six Datafriend suites on macOS and Linux, including dense-map churn, query
  publication, allocation rollback, identities, synchronization and async flush.
- Five rebuilt native-app host suites on macOS: tasks, particles, text cache,
  document CRDT and document model.
- Production TCP protocol suite with 160 channels, sparse IDs including
  UINT32_MAX, middle removal, moved-entry lookup, ID reuse and namespace isolation.
- Real native TCP integration, including signed writes, streamed updates, mismatch
  recovery, paging, offline/reconnect, deletions and edits during synchronization.
- New C client regression with 140 channels on one connection: runtime growth
  from a subscription callback, an injected growth failure and successful retry
  without consuming an ID, late-channel updates, stable IDs and reconnect.
  The first and final channels mirror the same owner to check delivery on both
  sides of multiple growth boundaries; the remaining namespaces are distinct.
- Linux ASan/UBSan and leak detection for the shared containers, all Littlebear
  and Datafriend suites, and both TCP clients against the sanitized server.
- Attribute-schema check, changed Python tools' syntax and `git diff --check`.

Linux runtime tests used the existing Ubuntu validation image with one CPU and
512 MiB RAM. Compiling two legacy C++ sanitizer tests concurrently exceeded that
limit; compilation finished with one compiler process and 1 GiB. Runtime tests
then passed at 512 MiB. macOS LMDB tests required running outside the filesystem
sandbox; their reruns passed. No phone installation or deployment was performed.

CI now includes the container checks, all Datafriend suites, and the new TCP
channel regression. App build/profile source manifests include the shared headers.

## Reproduce

```sh
make -C containers test
make -C littlebear test
make -C datafriend test
make -C demo-app test

# Linux
make -C littlebear server
python3 scripts/test_littlebear_tcp.py littlebear/build/server
python3 scripts/test_native_sync.py littlebear/build/server \
  datafriend/build/native/transport_test datafriend/build/native/channel_test

make -C containers sanitize
make -C littlebear debug sanitize
# Build Datafriend with ASan/UBSan C, C++ and linker flags, then run its suites
# and the same two TCP clients against littlebear/build/server-debug.
```
