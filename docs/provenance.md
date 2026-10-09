# Source provenance

Imported on 2026-09-26. The source projects in `/Users/alex/summer` were left
unchanged by this import. Nested Git repositories and generated build outputs
are not included.

## Littlebear

- Source: `/Users/alex/summer/littlebear`
- Upstream: `git@github.com:summerchildlibrary/littlebear.git`
- Revision: `b43c57cab82410a4df7090c6a3c4826ec699ba18`
- Imported: `src/`, `Makefile`, `shell.nix`, `README.md`, and `.gitignore`.
- The machine-specific `compile_commands.json` was omitted.
- No implementation fixes have been applied in this initial import.

## iCare

- Source: `/Users/alex/summer/icare`
- Base revision: `041b8268bdf403028bf786dd8b4c514a520786db`
- Imported the current working-tree `src/` into `reference/icare/src/` and
  `assets/` into the root `assets/` directory.
- The snapshot includes existing uncommitted changes in
  `src/app/src/icare/ui.cljd` and `src/app/src/icare/ui/drawing.cljd`.
- `deps.edn`, `pubspec.yaml`, and the original README are retained for context.
- Local databases, Firebase configuration/credentials, generated Dart, platform
  projects, and scratch scripts were omitted.

## Reconciliation and optional Negentropy reference

The production reconciliation header is a new, independent implementation using
separate 64-bit IDs and fingerprints, cached XOR summaries, count-balanced ranges, and a private wire format. The imported
Negentropy engine and C bindings have been removed from the checked-in source.

Historical B-tree and engine benchmarks can still fetch upstream Negentropy at
revision `b3687652922785c6f3b6c00684f51c6977965784` using
`datafriend-benchmarks/tool/fetch_reference.py`. It checks pinned file hashes and retains
the upstream MIT license in the ignored `datafriend/build/reference/negentropy/`
directory. This optional benchmark dependency is not linked into production.

## Datafriend local engine

The persistent wide-tree adaptation retains ClojureDart's EPL-1.0 notices in
`datafriend/NOTICE.md`. AELMDB 0.2.0 is pinned at
`359b89807d638e0f255e012cfe8c64dca33a0c02` in `third_party/aelmdb`, with its
license and per-file original hashes. Local changes use XOR64 accumulation and
a distinct disk format tag. The engine also uses the existing bundled xxHash
header and pinned unordered_dense map under their retained licenses.

## Document CRDT

The native demo vendors Yrs 0.24.0 from `https://github.com/y-crdt/y-crdt`, revision
`7dbc9d402533af3efa1e936342abeec499988b6b`. `third_party/yrs/import.json` records
original SHA-256 hashes; the MIT license, C API header and locked Cargo dependency
graph are retained. Upstream source is unmodified. `demo-app/src/document_crdt.h`
is our C++ ownership/text-field adapter, not a CRDT implementation. The library
links into the demo app, not Datafriend or Littlebear.

## Recovered junstodos native client (2026-10-08)

- Source: `/Users/alex/summer/junstodos` Git objects, not its modified working tree.
- Revision: `f5cd92ace74f0f112cba76743786660ee0ae9ebf`, the C++ todo application
  before the Janet rewrite. The source checkout and its staged/unstaged changes
  were left intact.
- Imported 51 files (2,162,727 bytes): `src/`, `include/sokol/`, `CMakeLists.txt`,
  `Info.plist.in`, `build.clj`, into `reference/junstodos/`.
- Each imported file matches its source Git blob exactly. SHA-256 and byte sizes
  are recorded in `reference/junstodos/import.json`; embedded Sokol licenses remain.
- Old database dependencies, compiler binaries and generated app bundles were
  omitted. This is a source reference, not a self-contained legacy build.
- Historical platform/team/network defaults are retained as source context and
  are not adopted by the new application. Current Datafriend replaces the old
  database, identity and synchronization implementation.

## Active Sokol Android app (2026-10-08)

- `third_party/sokol/` pins four unmodified headers from the recovered junstodos
  snapshot, with original embedded licenses and hashes in `import.json`.
- `demo-app/` adapts the recovered lifecycle and textured-quad approach;
  Android NativeActivity, cached StaticLayout labels, GLES3 particle rendering
  and direct SDK packaging are new integration code.
- Generated shaders include GLES3 and Metal variants. Source/header/compiler
  hashes are recorded in `demo-app/src/render/shaders.json`.
- The simulation is shared with the existing native particle benchmark; the
  production Datafriend engine was not changed for this app port.
