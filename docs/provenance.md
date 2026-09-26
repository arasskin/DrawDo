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

## Negentropy

- Upstream: https://github.com/hoytech/negentropy
- Revision: `b3687652922785c6f3b6c00684f51c6977965784`
- Imported: `c/`, `cpp/`, `docs/`, `LICENSE`, and `README.md` from that revision.
- The upstream MIT license is retained at `third_party/negentropy/LICENSE`.
- The C interface wraps the C++ engine. It is marked experimental upstream.
- Optional Git submodule dependencies, including `lmdbxx` for the LMDB storage
  implementation, are not vendored. Upstream test harnesses are not imported.
- The upstream README and build instructions describe the full upstream
  repository; this source subset will need DrawDo-specific build integration.
