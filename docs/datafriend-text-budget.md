# Text cache memory budget — 2026-10-08

The native text cache now has a **100 MB (100,000,000 byte)** budget instead of
a 96-texture capacity. The setting is `labels::texture_budget_bytes` in
`demo-app/src/render/text.h`. Texture storage grows on demand; 100 MB is not allocated
at startup.

Accounting charges `width × height × 4` for each resident RGBA8 bitmap. This
limits cached texel data, not total application/GPU memory: driver allocation
overhead, handle metadata, cache keys and transient CPU raster/upload buffers
are additional. Textures are shared by text, pixel size and clipping width.

The cache uses a hash index over stable LRU nodes. Hits do not allocate keys or
scan the whole cache. Before uploading a miss, it evicts enough least-recently
used textures to fit the incoming bytes. Destruction releases both image views
and images. Entries used by the current frame stay pinned until submission.
If one frame's required textures exceed the budget, the upload is rejected
instead of evicting something that a queued draw still references.

Sokol's separate image/view handle pools are raised to their 16-bit ceiling,
65,535, with 16 handles reserved for other renderer resources. Their bookkeeping
is allocated at setup; texture pixels remain on demand. Handle pressure can also
evict old unpinned textures. The existing 96-slot drawing batch is separate from
cache residency and does not limit how many labels can remain cached over time.

## Validation

- Three native test suites pass. Cache tests cover mixed-size eviction, exact
  fits, duplicate accounting, pinned frames, oversized uploads, failure cleanup,
  more than 1,000 resident entries, stable draw pointers through hash growth,
  raster-key differences and backend handle pressure.
- A fresh-title walk on the Moto G7 retained **223 textures / 14,696,136 bytes**,
  exceeding both the old 96-entry cache and Sokol's old default 128-image pool.
  Across 900 measured frames it presented at **60 Hz**, with **zero display gaps
  over 25 ms** and full EGL timestamp coverage.
- Focused 1,000-task idle, filter-switching and scrolling runs also maintained
  60 Hz with no display gaps >25 ms across 1,080 measured frames.
- The walk traverses about 198 task rows at 12 logical pixels per frame. Its
  p95 text preparation is 8.19 ms because it encounters new titles, rather than
  repeatedly visiting only already-cached rows. This validates growth, not a
  full 100 MB GPU saturation test; budget-pressure cases are covered by tests.

```sh
make -C demo-app android-run
python3 demo-app/tools/profile_tasks.py \
  --prefix datafriend-native-text-budget-walk --counts 1000 \
  --cases cache_walk --rounds 1 --frames 900
python3 demo-app/tools/profile_tasks.py \
  --prefix datafriend-native-text-budget --counts 1000 \
  --cases idle filter scroll --rounds 1
```

Profiling records resident texture bytes and entry counts, uses disposable task
data, and restores the normal app. [Walk results](benchmarks/datafriend-native-text-budget-walk.json),
[raw walk frames](benchmarks/datafriend-native-text-budget-walk-runs),
[walk metadata](benchmarks/datafriend-native-text-budget-walk-metadata.json),
[focused profiles](benchmarks/datafriend-native-text-budget.json).
