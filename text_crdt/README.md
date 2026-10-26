# Text CRDT

Small C99 wrapper around the pinned Yrs C API in `third_party/yrs`. Both demo apps
link the `text_crdt` CMake target. Datafriend remains independent of this library.

`crdt.h` exposes document lifetime, UTF-8 text replacement, state snapshots,
idempotent merging, and independent drafts. `sequence_replace` creates a minimal
common-prefix/suffix edit rather than replacing the entire Yrs text object.
Snapshots are Yrs v2 updates. Buffers use `containers/bytes.h`.

Keep each open `document_sequence` at a stable address: its Yrs observer refers
to that struct. Use `sequence_fork` to create an independent editing draft and
`sequence_close` to release it. Serialize all operations on the application thread.
Yrs remains the existing Rust dependency; the wrapper is C99.

`yrs.cmake` shares the dependency build/cache across consumers in `build/`.
