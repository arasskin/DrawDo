# Direct Datafriend C API — 2026-10-18

Implemented `datafriend/db.h` and migrated the active C app. Native edits accept
numeric keys directly; native queries compile numeric clauses and retain a
normalized sharing key without decoding a request. The binary API and C++ adapters
remain compatible. Stored facts, schema fingerprints and network protocol are
unchanged.

The direct interface provides typed store/view handles, options, scalar codecs,
set/unset/conditional retract/entity delete batches, transactional entity IDs,
automatic watches for local and remote publication, fixed-entity watches,
one-time detached queries and reusable ordered entity enumeration. Generated C
attribute constants replace the task model's duplicate numeric definitions.
The application binding contains only profiling, errors and text conversion.

Networks can follow a store's existing namespace and detach individual channels.
The client uses a dense channel array plus ID-to-position map. Removal repairs the
moved entry; IDs are never reused, so late replies cannot reach another store.
Detach is rejected during pump/callbacks and leaves the channel intact on queue
allocation failure. A store can attach to only one network. Native close rejects
an attached store. Native query callbacks can grow channel storage safely.

## Validation

- Seven Datafriend suites pass on macOS and Linux, including the new direct C
  test: owned edit input, atomic field/counter commit and rollback, no-op retract,
  ordered mutations, ID conflicts, immutable one-time results, shared live
  results, callback watch removal/addition, same-store reentrancy rejection,
  allocation failure sweep, native joins, retained results and reopening.
- Five rebuilt app suites pass: tasks, particles, text cache, document CRDT,
  document model. All app model source builds with strict C99 warnings as errors.
- Linux ASan/UBSan with leak detection passes all seven Datafriend suites and
  real TCP transport/channel tests against the sanitized Littlebear server.
- The channel test uses native watch dispatch for runtime growth to 140 channels,
  tests failed growth, stable IDs, reconnect, failed detach, middle removal,
  updates to the moved channel, fresh IDs and immediate close after an in-flight
  subscription is detached.
- The rebuilt macOS document app passes real TCP integration against a temporary
  Linux server: seed, Unicode, concurrent offline replacements and a stale live
  editor draft. Its CRDT merge is now driven by native store watches.
- Android native application rebuild passes, including the renderer, platform
  sources and profiling sources. No installation on a phone was performed.
- Attribute generator check and `git diff --check` pass.

Linux sanitizer runtime used one CPU and 512 MiB RAM. Sanitizer compilation used
one CPU and 1 GiB. The validation image lacks the Rust compiler needed to
reconfigure the full Linux app; that extra build was unavailable. The app was
instead rebuilt/tested on macOS, rebuilt for Android, and exercised against the
real Linux server. No tests were claimed for an unbuilt Linux app.

No deployments, commits or pushes were performed. No before/after performance
claim is made. The native path removes binary operation requests, attribute-name
round trips and the app's owned-ID-buffer copy. Reusable builders/enumeration
retain capacity; stored immutable values still require ownership allocations.
App profile `engine` time now includes watch dispatch; compare total actions and
frames rather than the old encode/engine/publication split.

## Reproduce

```sh
make -C datafriend test
make -C demo-app test
python3 datafriend/tool/generate_attributes.py --check
# Linux, after building the production server and test clients:
python3 scripts/test_native_sync.py littlebear/build/server \
  datafriend/build/native/transport_test datafriend/build/native/channel_test
# Existing platform-specific build directories:
cmake --build demo-app/build/android/native
```

See [API and examples](../datafriend-c-api.md).
