# Native text texture reuse — 2026-10-08

Follow-up: the fixed 96-texture capacity described here has since been replaced
by a [100 MB memory budget](datafriend-text-budget.md).

Text textures now survive changes in drawing position. The same phone profiling
matrix that exposed scrolling/filter stalls now presents at **60 Hz in all 28
runs**, with **zero display gaps over 25 ms** across 10,080 measured frames.

## Implementation

`demo-app/src/render/text_cache.h` holds a bounded LRU keyed by exact text, physical
font size and physical clipping width. Position and tint remain draw parameters;
logical scale belongs to the drawing slot. Identical labels share a texture even
when drawn in different slots. Entries prepared for the current frame are pinned
until submission, and queued draws retain texture indices rather than mutable
drawing-slot indices. Offscreen textures remain available until evicted.

The limit remains 96 resident textures and 96 draws per batch. Residency can be
higher than before because offscreen text is retained; this is a texture-count
bound, not a byte budget. The small fixed array uses linear lookup and allocates
no cache keys on hits. Saturating all current-frame entries reports capacity
failure rather than replacing a texture still referenced by a draw.

The UI interface, queries, edit path, JNI attachment strategy and frame pacing
are unchanged. Texture misses still use Android StaticLayout and bitmap upload.

## Phone comparison

Same Moto G7, optimized build, 12/1,000 tasks, seven workflows, two shuffled
rounds, 60 warmup and 360 measured frames per run. See the
[baseline methodology](datafriend-native-task-profile.md). Both rounds agree.

| 1,000 tasks | Before | After |
|---|---:|---:|
| Continuous scrolling, presented FPS | 56.4 | 60.0 |
| Rapid filter switching, presented FPS | 50.7–51.4 | 60.0 |
| Scroll display gaps >25 ms, both rounds | 25 | 0 |
| Filter display gaps >25 ms, both rounds | 81 | 0 |
| Worst scrolling callback | 61.45 ms | 16.37 ms |
| Reorder text preparation, median | 20.23 ms | 0.061 ms |
| Filter text preparation, median | 39.88 ms | 0.062 ms |
| Reorder app-thread CPU, median action frame | 23.10 ms | 3.81 ms |
| Filter app-thread CPU, median action frame | 43.63 ms | 5.04 ms |

The 12-task runs also reach 60 Hz: scrolling previously averaged 57.4 Hz and
rapid filter switching 50.1–51.1 Hz. Completion, rename, reorder and add/remove
remain at 60 Hz for both dataset sizes.

Across the two 1,000-task scroll runs, rasterizations fall **608 → 8**. The eight
remaining misses are newly encountered labels. Reorder falls **480 → 0**, and
filter switches **1,440 → 0**. These workloads revisit the same content; the
rename benchmark alternates two titles and consequently also becomes fully
cached after warmup. This does not claim a novel title is free to render.

The median cached-label path is about 0.06 ms per frame, versus 0.05 ms before.
Idle app-thread CPU stays around 2.6–2.7 ms. Render-to-display latency remains
about 54 ms on ordinary frames. Filter actions improve from about 71 ms to
54 ms by eliminating their text work, but graphics buffering still remains.
Wall callback time includes driver waiting; app-thread CPU excludes the engine
worker. No new GPU execution or power measurement was made.

## Validation and limits

- All 28 runs passed model/count checks with 100% distinct EGL presentation
  timestamp coverage; one database edit occurred per synthetic edit action.
- Three native suites pass: task model, particle/control model and text cache.
  Cache tests cover sharing, reorder, scroll eviction, current-frame pinning,
  font/width distinctions and failed-build recovery.
- Before/after visual inspection preserves glyphs, colors and clipping. Header
  and detail pixels match exactly; list position resets slightly on restart.
- The particle screen also renders correctly through over a minute of changing
  labels and switching back to tasks. No native rendering errors appeared.
- Profiling uses disposable data. The normal app and saved tasks are restored.

First-time text, content evicted from the cache and font/width changes still
require rasterization. Cold screens with many unseen titles can still stall;
JNI setup and text rendering on misses are separate follow-up optimizations.
The short scrolling scenario revisits a small region of the 1,000-task list,
so these numbers are not a full-list traversal or a sustained cache-thrash test.

```sh
make -C demo-app android-run
python3 demo-app/tools/profile_tasks.py \
  --prefix datafriend-native-task-text-reuse
```

Artifacts: [pooled before/after comparison](benchmarks/datafriend-native-task-text-reuse-comparison.json),
[run summaries](benchmarks/datafriend-native-task-text-reuse.json),
[raw frames](benchmarks/datafriend-native-task-text-reuse-runs),
[device state and source/APK hashes](benchmarks/datafriend-native-task-text-reuse-metadata.json).
