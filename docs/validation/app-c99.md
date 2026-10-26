# C99 app port — 2026-10-14

The supported Sokol app now uses C99 for its models, screens, rendering, query
bindings, document wrapper, network orchestration and native tests. Android JNI
adapters are C. Apple adapters and the Metal/Sokol implementation use Objective-C.
The small Java layer, Rust Yrs implementation, Python tooling, archived clients
and independent benchmark branches remain in their existing languages.

## Design and compatibility

`main.c` orchestrates independent model, rendering, platform and profiling
modules. Public headers declare structs and prefixed functions; implementations
live in `.c` files. Lifetimes use explicit initialization/cleanup and callbacks
with context pointers. The app calls Datafriend's C interface directly; it no
longer traverses the C++ compatibility layer. CMake needs no C++ compiler.

Task row subscriptions remain narrow and are explicitly removed when unmounted.
Publication retains the current result and tolerates callbacks removing other
subscriptions. Changed handles are decoded directly from the owned reply, without
an additional allocation after the commit. Particle neighbor/update buffers and
UI hit-target arrays retain capacity between frames. Text-cache entries combine
keys and resource metadata in one allocation, with a hash index and explicit LRU
links. Current-frame textures remain pinned until submission.

Database paths, identity alias, attribute schema, wire protocols, YDF2 records,
Yrs snapshots, task behavior, controls and stale-editor merge semantics are
preserved. No saved-data migration is required. The C RNG reproduces the C++
mt19937_64 sequence. A differential check against the saved C++ app matched all
1,000 particle coordinates exactly, both initially and after 100 ticks.

Yrs remains Rust behind its C API. The only vendored-header patch replaces empty
opaque structs with incomplete declarations and removes redundant self-typedefs
for strict C99; ABI and Rust sources are unchanged. Leak checking caught an
inherited wrapper issue: `yoptions()` allocates strings that construction copies
but does not consume. The C wrapper now frees those strings after construction.

## Validation

- All five C99 suites pass: tasks, particles, text cache, Yrs sequence and document
  model. They cover selective publication, unmounting during dispatch, stable
  view handles, filters/reordering, stale editors, persistence, atomic particle
  replacement, pause, cache growth/eviction/pinning, Unicode, snapshot permutations
  and duplicate delivery.
- Linux ASan, UBSan and leak detection pass all five suites after the ownership
  fix. C code is instrumented; the pinned Rust dependency is release-built, with
  its allocator activity visible to leak detection. macOS ASan still stalls at
  runtime startup on this machine, so memory validation uses Linux.
- Real TCP document integration passes on macOS and under Linux sanitizers:
  signed owner pushes, a 1,000-character seed, concurrent offline replacements,
  convergence and repeated stale-editor edits. The Linux integration client and
  server each run with one CPU and 512 MB.
- Android ARM64 Release build installed successfully on the connected Moto G7.
  Navigation, all particle counts, local/all modes, pause/resume, patch dragging,
  native keyboard opening, blank submission and suspend/resume were checked on
  the phone. Existing tasks stayed at revision 0 during the smoke check; the
  shared document was viewed without editing. See [device log](app-c99-android-smoke.txt)
  and [C99 particle screen](app-c99-android.png).
  iOS ARM64 unsigned Release build passes; iOS hardware was not exercised here.
- APK contents and ELF dependencies contain no libc++ or libstdc++. The iOS app
  also has no direct C++ runtime dependency. Platform frameworks and Yrs remain.
- Schema generation check, Python build-tool syntax checks and `git diff --check`
  pass. Existing unrelated workspace changes were retained; nothing was pushed.

See [sanitizer output](app-c99-sanitizers.txt) and
[validation/APK provenance](app-c99.json). The saved C++ source archive, APK and
particle comparison executables/results are under the ignored
`demo-app/build/c99-comparison/` directory.

## Matched Android measurements

The comparison uses the same Moto G7 and C99 Datafriend engine on both sides;
the baseline is the saved C++ app, rebuilt immediately before this port. Each
version ran two rounds of seven cases at 12 and 1,000 tasks: 28 runs, 120 measured
frames plus 30 warmup frames each. Case order was shuffled with the same seed.
The table shows the median of the two per-run median action durations, in µs.
These automatic actions run on the app thread; physical input and IME latency
are outside the benchmark.

| Tasks | Action | C++ app (µs) | C99 app (µs) | Change |
| ---: | --- | ---: | ---: | ---: |
| 12 | toggle | 190.4 | 178.4 | -6.3% |
| 12 | rename | 202.5 | 167.8 | -17.1% |
| 12 | filter | 315.4 | 258.6 | -18.0% |
| 12 | reorder | 191.0 | 171.8 | -10.1% |
| 12 | add_remove | 225.0 | 214.4 | -4.7% |
| 1,000 | toggle | 385.4 | 373.1 | -3.2% |
| 1,000 | rename | 210.1 | 176.0 | -16.2% |
| 1,000 | filter | 950.2 | 911.2 | -4.1% |
| 1,000 | reorder | 427.1 | 410.4 | -3.9% |
| 1,000 | add_remove | 759.7 | 714.6 | -5.9% |

APK size fell from **6.44 MiB to 4.31 MiB**
(33.0% smaller), chiefly from removing the packaged C++ runtime
and C++ app implementation.

Both versions maintained approximately 60 presented fps, with 100% presentation
timestamp coverage and no measured presentation gaps above 25 ms. Total frame
callback time stays around 10 ms and includes graphics submission/pacing; lower
action CPU time does not imply a higher displayed frame rate. Idle/scroll action
handlers are around 1–2 µs; their relative changes are too small in absolute terms
to explain frame behavior. The full summaries retain these cases.

This is a small, sequential before/after comparison, with normal phone frequency
and thermal variation (battery temperature was 21.3–22.4°C for the baseline
and 25.8°C for the final C99 run). It measures the complete implementations, not the effect
of the C language or an isolated allocation change. Allocation counts and total
process memory were not measured. Device conditions and exact source/APK hashes
are saved with the raw results.

- [C++ summary](../benchmarks/app-c99-before.json) and
  [metadata](../benchmarks/app-c99-before-metadata.json).
- [Delivered C99 summary](../benchmarks/app-c99-release.json) and
  [metadata](../benchmarks/app-c99-release-metadata.json).
- Per-frame JSON is gzip-compressed under `docs/benchmarks/app-c99-before-runs/`
  and `docs/benchmarks/app-c99-release-runs/`.
