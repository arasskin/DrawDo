# Recovered native iOS app

Recovered from `/Users/alex/summer/junstodos`, commit
`f5cd92ace74f0f112cba76743786660ee0ae9ebf` (`drive the todo app with direct
transact/query`). This is the C++ implementation before the Janet rewrite.
The source working tree is currently undergoing a Scheme rewrite and has most
of these files deleted; recovery reads Git objects and leaves that tree alone.

The imported files are byte-for-byte originals, recorded in `import.json`:

- `src/main.cpp`, `src/sokol.mm`: Sokol lifecycle, Metal and touch dispatch.
- `src/font/`: portable glyph cache/atlas/quad batch; Core Text shaping and
  Core Graphics rasterization, including fallback fonts and color glyphs.
- `src/app/todo_app.cpp`: C++ todo application with query/transact calls.
- `src/db/`, `src/engine/`, `src/sync/`: historical storage, Datalog, sync and tests.
- `include/sokol/`: matching vendored Sokol headers, retaining embedded licenses.
- Shader source and generated Metal shaders; original CMake, plist and build script.

This is a **reference snapshot**, not the active app or a self-contained build.
The old third-party database dependencies and shader compiler executable are not
imported. The original build script contains historical machine/team defaults;
the original plist and network code contain development settings. New app builds
must define their own platform configuration and use current Datafriend.

The todo UI supports tapping to toggle and tapping space to add canned tasks.
It is not a complete text editor or accessibility/widget framework. Its renderer
has a fixed 1024×1024 atlas, a 512-glyph shaping limit per call and a 4096-quad
batch limit; these need deliberate handling as the new application expands.

[Reuse assessment and Android plan](../../docs/sokol-client.md).
