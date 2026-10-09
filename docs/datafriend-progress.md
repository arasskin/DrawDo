# Datafriend implementation progress

## Remove edit acknowledgments (2026-10-08)

- Removed document receipt facts, subscriptions, pending receipt tracking and
  round-trip benchmark metrics. Existing demo receipt facts are deleted on open;
  schema ID 36 remains reserved so stored attribute IDs do not change.
- Successful optimistic uploads are silent. Owners stream subsequent frame edits
  without waiting; send timestamps are recorded at socket handoff.
- A mismatch sends one recovery event, discards later queued deltas until a fresh
  start, then reconciles the latest local snapshot. Authentication, reconnect,
  reconciliation and connection-liveness exchanges remain.
- Core/demo tests and real TCP convergence tests pass, including pipelined owner
  recovery, replica recovery, replay rejection and offline document merging.
  Server protocol tests also pass under ASan/UBSan.
- Phone validation found that silent writes could leave earlier subscriber slots
  waiting for the server idle timeout. The loop now immediately services
  subscribers after namespace changes; a regression test covers slot ordering.
- Final public-server medians: send → receive **105/108 ms**; edit → display
  **179/179 ms**, previously 213/215 ms. Edit → send **24/17 ms**, previously
  52/51 ms. All 134 edits converge in both concurrent and five-second-offline
  runs, with full presentation coverage and no >25 ms gaps.
- Server deployed and both phone apps updated. See
  [implementation and rendering/presentation breakdown](datafriend-noack.md).

## One-way delivery and presentation timing (2026-10-08)

- Added optional cross-phone clock calibration and correlated edit, upload,
  receive, UI-publication and presentation timestamps. No protocol/server change.
- Public-server median send → receive: **108 ms Android→iPhone, 107 ms reverse**,
  with estimated clock uncertainty ±11.2 ms. Edit → peer displayed frame:
  **213/215 ms**. About 51 ms precedes upload and 49–53 ms follows receipt.
- All 134 edits converged; no fallbacks/errors; full presentation coverage at
  ~60 Hz. Five model and four timing-analysis tests pass. Fixed frame recorder
  lead-in after identifying different wall-clock start times on the phones.
- Phones restored to interactive mode with probes disabled. See
  [definitions, calibration limits and raw results](datafriend-one-way.md).

## Optimistic owner/subscriber synchronization (2026-10-08)

- Implemented signed committed edit batches plus full namespace fingerprints.
  Matching owner updates take one request; server pushes the same records to
  subscribers, which validate the resulting fingerprint before publishing.
- Bounded journals/caches fall back to existing negentropy on missed changes,
  mismatches or reconnect. Connection challenges and ordered signature sequences
  are reused. Local transaction and asynchronous flush behavior is unchanged.
- Ten native component/model tests pass. Real TCP checks cover one-trip updates,
  rollback/fallback, replay/tamper rejection, large batches, reconnect and CRDT
  merging. Server integration also passes ASan/UBSan. Public server deployed;
  Android and iPhone updated and left in the interactive demo.
- Normal concurrent phone medians: acknowledgment **330/317 ms**, down from
  **1166/969 ms** (Android/iPhone). Local edits **0.837/0.803 ms**. All 134 edits
  converge; zero normal-run fallbacks; full presentation coverage with no >25 ms
  gaps. Separate offline run exercises recovery and also converges.
- Remaining limitations: acknowledgment p95 **768/718 ms**; more frequent full
  state transfers increase traffic about 2.4x. No owner-request pipelining yet.
  See [implementation, tests and raw measurements](datafriend-optimistic-sync.md).

## Request latency attribution (2026-10-08)

- Timed the existing no-storage heartbeat: public path about 100 ms on both
  phones, 110 ms on the Mac; server loopback 0.064 ms median. Independent Android
  ICMP ping averages 98 ms. The public path accounts for the baseline latency.
- Added opt-in bounded request/queue timing and same-connection heartbeat probes.
  Sync operation medians are also about 100 ms; typical client queue time is
  0.2–0.3 ms. No server/protocol changes. Five native demo tests pass; both phones
  converge and acknowledge all 134 edits with full presentation coverage.
- Restored the interactive demo with probes disabled. See
  [measurements and remaining attribution limits](datafriend-request-latency.md).

## Yrs document replacement and deployment workflow (2026-10-08)

- Replaced the custom RGA with pinned Yrs 0.24.0, through its C header and compiled
  Rust library. The dependency is app-only; Datafriend remains C++ and CRDT-agnostic.
- Removed all document all-facts subscriptions and per-character records. UI reads
  one `document/text` fact; each owner publishes one mergeable document-state fact.
- One local transaction publishes state and text, with asynchronous disk flushing.
  Native editor drafts use Yrs forks to preserve unobserved concurrent edits.
- Five core and five demo tests pass, including one-fact UI, restart/deletion,
  concurrent/reordered/duplicate snapshots and stale drafts. Real local/public TCP
  integration passes. Both phone builds are installed. Public concurrent median
  local edits: Android 0.883 ms (was 7.14), iPhone 0.744 ms (was 2.43). All 134
  edits converge/acknowledge; final state 1,222 bytes and exactly one UI fact.
- GitHub's deploy-on-main workflow is active and recent push deployments succeeded.
  Local workflow changes deploy the tested binary artifact and handle the installed
  service's legacy path. Those workflow changes await commit/merge.
- Updated public server deployed and healthy. Added a pre-switch ELF architecture
  check after an ARM64 local build failed on the x86-64 host; the previous service
  was restored before deploying the correct executable.
- See [design, dependency constraints and measurements](datafriend-document-yrs.md).

## Native two-phone document sync (2026-10-08)

- Native nonblocking multiplexed transport implemented; engine calls remain direct.
- Sokol document CRDT demo installed on Android and iPhone, with secure native
  identities, live typing, offline edits and remote publication on the app thread.
- Public cache updated on port 7777; signed native interoperability checks pass.
- LAN/public concurrent edits converge; LAN offline/reconnect converges.
- Final public concurrent run: Android local edit 7.14 ms, iPhone 2.43 ms
  medians; both ~60 Hz, full presentation coverage, no >25 ms gaps. Return
  acknowledgments take 1.68 / 1.64 seconds; LAN takes 0.50 / 0.54 seconds.
  This is not yet Google Docs-class sync latency. See [measurements](datafriend-document-sync.md).
- All nine native correctness tests, real TCP/CRDT integration, production server
  auth tests and Linux ASan/UBSan transport checks pass. Both phones are left paired
  to an interactive public-server document. Automated iPhone relaunch occasionally
  loses drawable presentation; rejected runs are retained and documented.

## Direct library calls and subscription results (2026-10-08)

- Step 1: removed the engine worker, dispatch queue, futures and handoff mutex.
  Calls execute on the application context and publish before returning. Disk
  flushing remains asynchronous; network I/O stays in the existing adapter.
- The caller must serialize all engine operations, result reads and destruction.
  Updated the old concurrent-call test to check immediate serial publication.
- Step 1 passed all five core and three demo tests. Android's 1,000-task filter
  median measured 4.07 ms (previous threaded comparison: 7.38 ms), at 60 Hz.
- Step 2: subscriptions return `{id, shared_result}` in one call; native UI,
  particle and profiler callers use both directly. Tests cover returned-result
  sharing, live updates and retention.
- Dart/Flutter support is retired per the user's direction. Existing sources are
  historical reference; compatibility is not a constraint on the native API.
- Final validation: all eight native tests pass; Android installs and the iOS
  Release build signs successfully. Android filter median: 7.38 ms prior,
  4.07 ms direct-call checkpoint, 4.26 ms with tuple returns. Toggle: 1.84 →
  1.35 → 1.30 ms. Both checkpoints present at 60 Hz with no >25 ms gaps.
  The tuple change's separate speed effect is below these runs' variation.
- No blocker for these changes. [Measurements and API contract](datafriend-direct-calls.md).

## iOS demo and phone comparison (2026-10-08)

- Added a signed Sokol/Metal iOS device build and installed it on the paired
  iPhone 13. Task and particle models, queries, edits, text cache and rendering
  remain shared with Android; Apple adapters provide rasterization, text input,
  safe areas and private storage paths.
- Selected POSIX semaphores for AELMDB on iOS, including CocoaPods builds, and
  added optional static core linkage for the native app.
- Shared the rendered benchmark workloads and summaries across phones; Apple
  captures Metal drawable presentation times. Five core and three demo tests pass.
- The first matched pass covers 5,040 frames per phone with full presentation
  coverage and no >25 ms gaps. At 1,000 tasks, iPhone median actions are
  0.96 ms toggle / 2.22 ms filter, versus Moto G7's 1.84 / 7.38 ms.
- Limitation: repeated automated iPhone relaunches sometimes show black frames
  and missing presentation timestamps. Sustained running is visible (confirmed
  by the user), with no GPU errors in the diagnostic run. Failed runs are retained
  and excluded from the comparison; the relaunch issue is not fully diagnosed.
  Ordinary interactive apps are restored. [Results and scope](datafriend-ios.md).

## Source layout and dependency cleanup (2026-10-08)

- Moved the Sokol client out of the Flutter tree into `demo-app/`, with explicit
  task/particle models, UI helpers, rendering, platform and profiling groups.
  Build/deploy with `make -C demo-app android-run`; host tests use `make test`.
- Moved Datafriend implementation to `src/`, secure storage to `platform/`, and
  CocoaPods-only vendor glue to `platform/apple/vendor/`. CMake now names the
  storage/crypto dependencies and selects the relevant platform implementation.
- Consolidated C ABI/identity wrappers and UI subscription helpers; renamed
  ambiguous codec, allocator, model and screen headers. Preserved ABI, shaders,
  signing identity, benchmark captures and the separate Flutter baseline.
- Added [the dependency map](../architecture.visualize) and refreshed READMEs,
  source links, build scripts and profiling provenance paths.
- Five Datafriend and three demo tests pass; Flutter's C++ particle adapter
  builds; all 23 C ABI exports are preserved; Apple vendor bridges and podspec
  validate. Android installs from the
  new location; a 1,000-task filter smoke run presents at 60 Hz with no >25 ms
  display gaps. No blocker for this cleanup.

## Text cache memory budget (2026-10-08)

- Replaced the 96-entry cache with a 100 MB RGBA texture-data budget, allocated
  on demand. Hash lookup and stable LRU nodes support larger caches; byte-based
  eviction releases old textures while preserving current-frame draws.
- Raised Sokol's separate texture-handle pools. Native tests pass for byte
  accounting, eviction, pinning, large caches and failed uploads.
- Phone traversal retained 223 textures / 14.7 MB at 60 Hz across 900 frames,
  with no display gaps >25 ms. Focused idle/filter/scroll checks also hold 60 Hz.
  New-title rasterization still has a cost; no blocker remains.
- [Budget scope, implementation and validation](datafriend-text-budget.md).

## Native text texture reuse (2026-10-08)

- Replaced drawing-slot texture residency with a content/font/width cache,
  bounded to 96 textures. Queued draws pin entries until submission; offscreen
  labels remain reusable across scrolling, reorder and filter changes.
- Repeated all 28 phone runs (10,080 frames): every workload now presents at
  60 Hz, with zero display gaps >25 ms. At 1,000 tasks scrolling improves from
  56.4 Hz and filter switching from ~51 Hz; scroll rasterizations fall 608→8.
- Median text preparation falls from 20.23→0.061 ms on reorder and
  39.88→0.062 ms on filter actions. Cold text still requires rasterization.
- Three native test suites pass; visual checks preserve text/tint/clipping.
  No blocker. [Comparison and limitations](datafriend-text-texture-reuse.md).

## Native task-list phone profiling (2026-10-08)

- Added opt-in phase/CPU timers and actual EGL presentation timestamps, plus a
  reproducible phone runner using disposable data and restoring saved tasks.
- Profiled 10,080 frames across 12/1,000 tasks, seven workflows, two rounds;
  four additional runs split JNI, text and graphics costs. All checks passed.
- Ordinary edits maintained 60 Hz. Scrolling fell to 56–57 Hz and rapid filter
  switches to 50–51 Hz, dominated by text-slot cache invalidation/rasterization.
- At 1,000 tasks: median blocking edit engine cost ~0.8–0.9 ms; reorder's entire
  action ~2.1 ms, followed by ~20 ms of text preparation. Row-boundary frames
  reached 72 ms. Reusing text textures is the clearest next optimization.
- Render-dispatch-to-scanout ~54 ms despite smooth ordinary edits; idle graphics
  wall time includes substantial waiting. Frame pacing deserves a separate pass.
- Both native test suites pass. Normal app restored; no profiling blockers.
  [Full report and raw captures](datafriend-native-task-profile.md).

## Native task list (2026-10-08)

- Added typed query clauses with include flags, scoped subscriptions, direct
  edits and `attempt(error, action)`, following the Dart demo's ergonomics.
- Task list projects order; visible rows own title/done views. Counter and detail
  panel subscribe independently. Scrolling releases offscreen row subscriptions.
- Model tests pass for selective updates, reorder identity, filtering/unmount
  during publication, stale-editor deletion protection and database reopen.
- Installed as **Datafriend demo**, opening the persistent task screen. Phone
  checks pass for keyboard add/rename/cancel, completion, filters, drag reorder,
  scroll, process restart and switching to/from particles. APK is 3.1 MiB.
- Android editor uses a separate PopupWindow surface so Sokol cannot overpaint
  its caret/selection. Application edits stay on the C++ thread; disk flush async.
- Reordering twice on the phone increased list builds 1→3 while mounted row
  builds stayed at 8 and counter builds at 1. Two native test suites pass.
- [API/design](datafriend-native-tasks.md), [device checks](validation/sokol-tasks-device.txt),
  [screen](benchmarks/datafriend-native-tasks.png). No remaining blocker for this
  slice; canvas accessibility, general layout and iOS remain follow-on work.

## Datafriend-driven native controls (2026-10-08)

- Rebuilt the native demo around one store for particles and controls. Pause,
  count, movement mode, automatic patch movement and patch position are facts;
  button callbacks submit blocking edits directly.
- Added a small native `watch(query, builder)` binding. Seven button views plus
  the settings/status view update synchronously from returned subscription IDs.
  Particle ticks never rebuild buttons; pause updates one, mode two, count four.
- Count changes now replace particles and the count in one transaction while
  preserving the engine, live result identity, subscriptions and other controls.
  Automatic patch changes share the particle tick transaction.
- Added six shared schema attributes (C++ and Dart generated together). Engine
  algorithms unchanged. The standalone demo still starts fresh on launch.
- Model integration tests pass: selective publication, atomic replacements,
  stable views, store-driven controls, paused no-op frames and stored settings
  after reopen. Phone controls/drag/lifecycle validated and app redeployed.
- [Implementation and checks](../demo-app/README.md),
  [device log](validation/sokol-datafriend-ui-device.txt).

## Standalone Sokol Android app (2026-10-08)

- Built and installed **Sokol particles** on the Moto G7: C++/Sokol/GLES3 calls
  the unchanged production Datafriend engine directly. Shared simulation code
  now lives in `demo-app/src/particle_simulation.h`; the benchmark adapter still links.
- `make -C demo-app android-run` compiles with CMake/Ninja and packages with direct
  SDK tools. No Gradle/Flutter dependency. Signed, aligned APK: **2.8 MiB**.
- Cold build **11.44 s**; latest C++ rebuild/package **2.69 s**, install **4.26 s**,
  launch **0.43 s**. Java/resources and unchanged APKs are cached separately.
- Verified count controls, local/all-moving modes, pause/resume, patch dragging
  and background/resume on the phone. Database validation passes; no app errors
  in captured device logs. Left the 1,000-particle local simulation running.
- Cached Android system text and system-bar insets work. Full text editing, todo
  port, iOS packaging and the native TCP adapter remain; no blocker for this demo.
- [Build/runtime details](../demo-app/README.md),
  [screen](benchmarks/datafriend-sokol-android.png),
  [device checks](validation/sokol-android-device.txt).

## Sokol direction and source recovery (2026-10-08)

- Committed the architectural direction to C++/Sokol; the Flutter client remains
  the working baseline during the port.
- Recovered 51 files from junstodos revision `f5cd92a` into `reference/junstodos`:
  native iOS lifecycle, todo app, atlas renderer, Core Text platform layer and
  historical engine/sync code. Verified every file against its original Git blob.
- Android arm64/API24 compile/link probe passes for recovered Sokol/GLES3 and
  regenerated shaders; NativeActivity entry point verified. No APK/device render
  claim yet. Current Dart TCP orchestration also needs a native port.
- Existing Sokol supports Android; the main portability gap is text shaping and
  input. API-31 TextShaper cannot be the only backend on the API-29 Moto G7.
- [Recovered source, reuse boundaries and Android plan](sokol-client.md).

## Native particle comparison (2026-10-08)

- Added a benchmark-only C++ particle client calling the unchanged production
  engine directly. Same APK/library, CPU 0, no rendering or vsync pacing.
- Two-round controls: 1,000 local **3.76 → 2.66 ms**, all-moving **45.00 →
  27.72 ms**; 3,000 local **13.09 → 9.00 ms**, all-moving **162.87 → 98.63 ms**.
  Native reduces full tick time 29–39% at these sizes, including client physics
  and caching improvements; this is not an isolated FFI transition cost.
- Diagnostic 1,000-all edit: **25.08 ms** in main native call, **10.27 ms** outside
  it; request encoding accounts for **6.23 ms**. Bulk transport/copying is small.
- All 20 paired cases pass database checks, 4,800 per-tick work comparisons and
  final coordinate equality (zero discrepancy). No Sokol renderer added yet.
- [Implementation, controls, phase profile and reproduction](datafriend-particle-native.md).

## Particle demo on Android (2026-10-08)

- Built and installed **Cell particles**, a Flutter simulation with 96 static
  region views, neighboring-cell interactions, one edit batch per tick and
  notification-driven read caches. Uses the production router and async flushing.
- Moto G7, two rounds: 1,000 particles / ~80 local moves holds **60 fps**, step
  **4.05 ms median / 4.48 ms p95**. 3,000 / ~247 local moves reaches **58.4 fps**,
  step **11.13 / 12.53 ms**. Moving all 1,000 falls to **20.5 fps**; edit dominates.
- Sustained 3,000-local test averages **51.1 fps**, **14.82 ms p95** steps:
  the short-run 58.4 fps does not establish a reliable 60 fps capacity. All 1,800
  sustained ticks pass state checks, including another 3,450 cell crossings.
- Sustained 1,000-local control holds **60.0 fps**, **4.61 ms p95** steps over
  1,800 ticks, with all consistency checks passing.
- Fixed missing Android native optimization for profile/release builds. Switched
  demo rendering from 96 repaint layers (~32 fps at 100 particles) to one batched
  canvas (60 fps); subscriptions and reads remain selective.
- Three native-backed simulation tests pass. All 3,840 measured phone ticks pass
  state/frame checks, including 20,704 cell crossings. No network benchmark here.
- [Workload, timings, limitations and reproduction](datafriend-particles.md).

## Batched routing and maintenance timing (2026-10-07)

- Benchmark-only option `DATAFRIEND_BATCH_ROUTING` collects all affected query/
  entity pairs from old indexes plus the complete pending edit batch.
- Predicate routing caches each entity's predicate values across changed
  attributes; concrete routing plans link removals/additions during discovery.
- Separate coarse clocks measure routing lookup/collection and subscription-link
  maintenance. Database staging, queries and publication remain unchanged.
- Four-way Moto G7 comparison, CPU 0, 1,000 four-entity views: current-router
  100-edit median **2.267 → 2.188 ms**; 100 replacements **5.695 → 5.502 ms**.
  Two-table replacements **6.546 → 6.340 ms**. Both options remain default-off.
- Replacement profile: current routing **0.887 → 0.706 ms**. Two-table routing
  **0.892 → 0.558 ms**, but separately measured link maintenance **0.707 →
  0.812 ms**. The two-table maintenance cost remains substantial.
- All 8,400 timed edits pass result/fan-out checks. Both batched variants pass
  six native suites and UBSan engine tests; both baselines pass updated engine
  regressions. All phone variants pass network integration in both runs.
- [Implementation, component timings and raw data](datafriend-batched-routing.md).

## Two-table routing comparison (2026-10-07)

- Experiment lives in the `datafriend-benchmarks` linked worktree, behind
  `DATAFRIEND_DIRECT_SUBSCRIPTIONS`; production routing is unchanged.
- Concrete EA dependencies route existing predicate candidates directly;
  attribute/value match subscriptions discover new candidates. Missing required
  attributes remain subscribed, and rollback/unsubscribe rebuild or remove links.
- Moto G7 CPU-0 control, 1,000 four-entity views: 100 ordinary edits **2.267 →
  2.180 ms** (−3.9%); 100 replacements **5.652 → 6.564 ms** (+16.1%). Fixed-query
  control is effectively unchanged. Candidate stays default-off.
- Setup **96.4 → 121.3 ms**, with about **3.92 MiB** additional process peak RSS
  growth during subscription creation (not an exact live-allocation measurement).
- Coarse profile: ordinary index/routing **0.576 → 0.426 ms**; replacement
  **1.611 → 2.394 ms**, principally new-state subscription link maintenance.
- 10,800 timed ticks across the broad matrix, CPU control and coarse profile pass
  correctness checks. Six candidate native suites, baseline engine suite, candidate
  UBSan engine suite and both phone variants' network integration pass.
- [Design, timings, reproducibility and limits](datafriend-two-table-routing.md).

## Index and routing breakdown (2026-10-07)

- Added optional old-state routing, index-update and new-state routing clocks;
  staging and indexing algorithms are unchanged.
- Moto G7, 100 replacements across 1,000 views: **0.925 / 1.234 / 0.603 ms**
  median respectively, **2.812 ms** combined. Routing is roughly half the work.
- These are fresh measurements, not a subdivision of the earlier 1.05 ms result:
  all major phases ran slower this time; CPU frequency/scheduling was not pinned.
- Two interleaved diagnostic comparisons, 800 samples each, pass result and
  notification checks; both builds pass phone network integration. Host engine
  tests pass with detailed profiling enabled.
- Per-map timers inflate the index loop from 1.234 to 3.123 ms even using the ARM
  counter. Fine-grained allocation/map timings are not reliable production costs.
- [Breakdown, explanation and measurement limits](datafriend-index-breakdown.md).

## Current priorities and boundaries (2026-10-07)

- First resolve the ~91 ms grouped-query simulation case before expanding scope.
- Collaborative document CRDTs belong at the UI/application layer. Datafriend
  sends/receives their data; it does not implement document merging.
- Specialized multi-writer server containers with CRDT logic are a fallback only
  if client-side collaboration proves too slow, not an agreed immediate change.
- A future browser UI connects to a local server/client architecture. A JavaScript
  API is deferred; a browser-hosted WASM engine is not the current direction.
- Implemented constant-predicate subscriber lookup and per-query changed-entity
  routing, checking both old and new index states. General joins stay conservative.
- Matched phone comparison: the original grouped case is **92.02 → 2.59 ms**
  median (**4.50 ms p95**); 100 entity replacements are **204.69 → 4.72 ms**.
  The 100-edit case now evaluates 100 query/entity pairs instead of 100,000.
- 112 interleaved processes / 1,680 timed samples pass all result/notification
  checks. Instrumented query work is now ~0.66 ms for the 100-edit grouped case.
- A separate small fixed-query control shows **0.629 → 0.688 ms** median; this
  is a targeted bulk/grouped improvement, not a universal speedup.
- Six native suites, 34 Flutter tests and the UBSan engine suite pass. Both phone
  builds pass network integration. Docker was unavailable for a Linux ASan rerun.
- [Implementation, results, raw data and limits](datafriend-predicate-routing.md).
  Full-frame/CRDT/network performance remains unmeasured for these workloads.

## Persistent push and subscriptions

- Explicit `store.sync.push(endpoint, identity: ...)` and
  `store.sync.subscribe(endpoint)` share persistent connections across namespaces.
- The server sends the first reconciliation packet on subscription and cache
  changes. Clients collect differences and request values. No namespace polling
  or separate change-notification round trip; the initial-offer optimization stays.
- New openings invalidate in-flight subscriber work. Stale requests return fresh
  openings when stable; writes proceed without locking an entire subscriber sync.
  Existing per-record kernel I/O pins still require a conflicting operation to retry.
- Missing namespaces preserve local replicas and wait for the owner; existing
  empty namespaces clear replicas. Reconnect reattaches and restores the cache.
- LBS5 correlates requests and namespace channels. Signed writes cover both IDs;
  challenges still piggyback and add no authentication round trips.
- Validation: six native suites and 34 app tests pass; Dart analysis is clean.
  Linux transport/authentication and Dart integration pass under ASan/UBSan with
  one CPU and 256 MiB for the integration server. A delayed-fetch regression
  verifies stale subscriber work is discarded before publication.
- [Protocol, API and limits](datafriend-network.md). No deployment performed.

## Native owner identities

- Datafriend now creates, securely stores and signs with owner keys. Dart holds
  only an opaque handle, public namespace and signatures; its seed import/export
  API and direct cryptography dependency are removed. Open by stable alias.
- Native signer accepts only its own LBS5 write envelopes. Pinned Monocypher
  4.0.3 provides Ed25519; Littlebear auth/wire format is unchanged.
- Apple Keychain stores device-only, non-synchronizing seeds. Android uses
  Keystore AES-GCM wrapping plus atomic, verified, non-backed-up records.
  Storage failures fail closed. Native secret buffers are wiped on release.
- Passed: 6 native suites, 34 app tests, 7 demo tests, RFC 8032 vector, Linux
  ASan/UBSan identity tests, Littlebear signature interoperability/paged writes,
  packaged iOS Keychain tests, Android tamper/reopen tests and process restart.
- macOS ASan deadlocked in runtime initialization before main; a stack sample
  confirmed the location and the Linux sanitizer run passed instead.
- Secure storage backends cover Apple/Android. Signing uses native memory;
  recovery/export and other host secure-storage backends remain unimplemented.
- [Interface](datafriend-network.md), [device/native validation](validation/datafriend-native-identity.json),
  [Littlebear interoperability](validation/datafriend-native-identity-network.json).
  No mobile implementation blockers.

## Littlebear TCP integration with signed owner writes

- Kept flat unordered packed-EA views and cached entity ordering.
- AELMDB read transactions supply stable reconciliation snapshots; local edits
  continue. Paged pulls enter one atomic local commit, update live views and notify
  Dart. Stale exchanges restart; missing server namespaces preserve replicas.
- Removed TLS and bearer credentials. Datafriend uses asynchronous TCP sockets;
  scheduling and retry now use persistent push/subscription channels (see above).
- Linux Littlebear uses one io_uring completion loop for accepts, socket I/O and
  file I/O. Buffers and connection slots survive until completions are drained.
- Namespaces are Ed25519 public keys. Reads/reconciliation are public; only signed
  applies create or mutate namespaces. Challenges ride on existing reconciliation
  or absent-namespace replies, adding no round trips. Equal pushes and pulls need
  none. One challenge covers paged writes; the negentropy engine is unchanged.
- Fixed per-channel nonce/sequence state rejects replay. No credential table
  or unauthenticated test build remains; tests exercise the production binary.
- Signed-write/replay tests and 9.6-MB Dart transfers pass normally and under
  ASan/UBSan in a one-CPU, 256-MiB Linux container. Native network integration,
  34 app tests and seven demo tests pass; Dart analysis is clean.
- [Interface, setup and limits](datafriend-network.md). No live deployment was
  performed. Earlier TLS validation reports are historical, not current auth checks.

## Simulation workloads on the phone

- 100/1,000 distinct views, 1/4/16 entities each, 1%/10% of views edited in one
  tick; stable values and entity replacement. 56 processes / 840 samples pass
  all result and notification checks. Added four separately instrumented cases.
- At 1,000 views: ten fixed-entity updates **1.87 ms**; ten grouped-view updates
  **14.44 ms**; 100 grouped updates **90.97 ms**; 100 replacements **204.33 ms**.
  These are native edit/publication times, excluding Flutter and networking.
- Grouped queries reconsider all 1,000 groups but notify only the actual 10/100
  changed views. Each group also visits every changed entity in the transaction.
  Phase timings confirm queries/routing dominate; database commits are small.
- Next design topic: route relevant changed entities to each query, including
  old/new group predicates. The heavy grouped workload is not frame-budget ready.
  [Method, raw evidence and breakdown](datafriend-simulation.md).

## Sorted dense maintenance experiment

- Added deferred structural batches, append/suffix fast paths, and an in-place
  merge that sorts only changed keys. Existing values still update in place.
- Retaining the original full-sort candidate for matched phone comparisons;
  added append/delete-tail cycles and batched 16-entity replacements.
- Randomized model/ownership tests, UBSan and four native suites pass. Sparse
  pending cleanup avoids clearing a retained full-view hash index.
- All 150 final phone processes / 126,000 samples pass. At 1,000×5, general
  entity replacement improves **331 → 217 µs**; append/delete-tail cycles improve
  **395 → 2.9 µs**. Grouped traversal remains 12.6 µs; CPU-0 control agrees.
- Tiny membership edits and initial construction cost more; pending-aware reads
  also add overhead. General middle edits retain linear rebuilding costs.
- [Method, results and limits](datafriend-sorted-merge.md). No blockers; production
  selection remains separate from these container experiments.

## View layout / Android comparison

- Prototyped flat packed-key native results; field reads use one hash lookup.
  Entity-local pruning probes projected keys rather than scanning a full view.
- Native suites pass. Added allocation-free ordered traversal to the mutable
  wide tree; randomized traversal/model checks pass.
- Comparing nested maps, flat maps, sorted dense maps and the slab-allocated
  mutable wide tree on the connected Moto G7 (Android 10, arm64).
- Phone is the preferred benchmark target going forward. The new runner records
  device/thermal state, source/binary hashes and raw samples.
- All 120 phone processes / 76,800 timing samples pass (normal scheduling and
  a CPU-0 control). For a 1,000×5 view, grouped
  scan: nested 115 µs, sorted dense 13 µs, wide tree 32 µs. Entity replacement:
  1.7 µs, 332 µs, 3.3 µs respectively. Sorting has a structural-edit cost.
- Flat prototype passes 19 targeted Dart/app tests and seven demo tests. Ordered
  view alternatives remain benchmark-only pending a design choice.
- [Phone method, results and limits](datafriend-android-views.md).


## Attribute-level storage

- Replaced whole-entity AELMDB records with packed `(uint32 entity, uint32
  attribute)` keys and fingerprint-prefixed value bytes. Local batches remain
  atomic; disk flushing remains asynchronous.
- Added one explicit-ID schema with generated C++/Dart enums, native/Dart schema
  agreement checks, and no per-store attribute allocation. Old disk formats are
  rejected; no migration or older-client compatibility layer.
- Added fact-level import/export. Whole-entity export/delete helpers remain
  conveniences, outside the ordinary edit path. Network I/O is not part of this step.
- Native suites, UBSan, 33 app tests and seven demo tests pass; analysis is clean.
  All 36 native benchmark processes / 72,000 edits and 36 UI cases / 720 edits pass.
- Five-field native edits: completion **20.42 → 17.13 µs**, title + completion
  **18.96 → 18.08 µs**. Unchanged 32-field / 32 KiB blob entities gain 42–60%.
- UI rebuild counts and persisted contents are unchanged. UI timing is inconclusive
  because system load rose to 349 during that run. Native timing preceded the spike.
- [Format, measurements and validation limits](datafriend-fact-storage.md).
  No implementation blockers. ASan stalled during macOS runtime startup before
  `main`; separate UBSan validation passed.

## Pure Flutter baseline

- Built a package-free, screen-owned `setState` todo baseline using ordinary
  mutable task objects, derived filtering/sorting/counts and lazy list rows.
- Matching Datafriend's layout and title/completion/order workloads; baseline
  is in-memory only. Reporting callback and frame work separately because the
  baseline derives its visible state during build.
- Seven demo tests pass, analysis is clean, and the Release bundle is built.
- All 36 matched Release cases / 720 measured edits pass rendered-state and
  rebuild checks. Both UIs mount/cache twelve rows.
- At 1,000 tasks, plain Flutter → Datafriend median frame build: completion
  **6.13 → 1.87 ms**, title **6.23 → 1.28 ms**, order **5.80 → 5.39 ms**.
  Flutter callbacks take ~0.02 ms; Datafriend ~0.35–0.45 ms. Callback plus
  framework intervals favor Datafriend for title/completion; order is about even.
- End-of-frame medians remain ~14–15 ms. Datafriend has higher callback tails;
  results compare in-memory UI state with the full store, not equal capabilities.
- [Method, results and limits](datafriend-pure-flutter.md). No blockers.

## Fixed-entity subscription routing

- Fixed entity clauses now subscribe by packed `(entity, attribute)`; variable
  entity clauses retain attribute-wide dependencies. Mixed joins register both.
- Commits route actual changed pairs, including removed attributes and entity
  imports/deletions. Shared groups and last-owner unsubscribe cleanup are retained.
- A regression with 1,000 row subscriptions proves one title edit evaluates one
  group. Coverage includes aliases, batches, predicate removal, zero/max IDs,
  joins, deletion and rollback recovery.
- Native suites, 32 app tests and six demo tests pass. The Release app is built.
- Matched old/new benchmarks pass: 36 GUI processes / 720 edits, and 18 native
  processes / 54,000 edits. Rebuild counts remain unchanged.
- Native title-edit medians: 12 subscriptions **16.67 → 15.71 µs**, 100
  **20.83 → 16.33 µs**, 1,000 **73.13 → 19.92 µs**. GUI totals are mixed;
  the ~1 µs saving at twelve rows does not explain the earlier ~0.14 ms overhead.
- [Measurements and limitations](datafriend-fixed-routing.md). No blockers.

## Current agreed design

- Direct namespace triple edits; blocking calls return after the littlebear,
  indexes and every affected query result are consistent.
- Query results are shared mutable dense maps keyed by packed entity/attribute. No persistent view trees,
  replacement result maps, local reconciliation, result locks or generation counters.
- `subscribe(query, onChanged: callback)` stores the callback in Dart. Native
  edits/imports return changed subscription IDs; Dart invokes the callbacks before
  edit returns. The app invalidates lazy projections and schedules Flutter builds.
- One application thread serializes reads and edits. The owner queue retains its
  dispatch synchronization. Future network changes must use this same boundary.
- A failed transaction rebuilds indexes from committed facts and reruns every
  active query. Recovery failure disables reads until reopen.
- Packed entity/attribute AELMDB/network records, namespace-local uint32 entity
  IDs, shared enum attribute IDs, immutable boxes,
  attribute/pair invalidation and query sharing are retained.

## Inline Flutter queries

- Added `DatafriendBuilder(database, query, orderBy, builder)`. Each widget owns
  its subscription, calls `setState` on changed results, and closes native handles
  on unmount. Structurally equal inline queries reuse the existing subscription;
  changing the database or query replaces it, while changing sort fields only
  replaces the ordering index.
- Builders receive a live indexed entity view: `view[index].id` and
  `view[index]['title']`. Fields decode lazily and cache per entity; change
  summaries invalidate affected caches and membership/sort changes drop ordering.
- The interactive todo demo now uses inline list/count queries and synchronous
  `database.edit` calls. Filtering changes the query. Controllers and InstantDB
  switching are absent from the interactive path; historical comparison code
  remains isolated in `comparison_screen.dart` and the benchmark adapters.
- Widget tests cover selective rebuilds, filtered membership, equivalent query
  reuse, query/database/sort changes, rollback invalidation, disposal, real CRUD,
  reopen, and deletion while a rename dialog is open.
- Validation: 28 app tests and five demo tests pass; wrapper/demo analysis is
  clean. The macOS Release bundle builds and its rendered smoke run passes CRUD,
  frame-update and reopen checks. Screenshot:
  [inline queries](validation/todo-demo/inline-queries.png). Smoke timings are
  correctness validation, not a new performance comparison.
- Follow-up [direct UI benchmark](datafriend-widget-benchmark.md): 12 alternating
  legacy/inline cases, 360 measured edits, all checks passed. At 1,000 tasks,
  median state-ready time is 0.324 ms versus 0.432 ms and edited-frame build is
  5.860 ms versus 6.940 ms. State-ready p95 is roughly unchanged (0.883 ms).
  The new benchmark invokes actual checkbox callbacks without old adapter
  subscriptions in inline cases. End-of-frame latency remains near 14 ms.

## Per-row task queries

- The task list returns order only; each mounted row subscribes to its title and
  completion state. Active/Completed add a non-projected membership predicate.
- Entity keys and a child-index callback preserve row subscriptions on reorder.
  All six demo tests pass, including exact rebuild assertions for title edits,
  completion, batched edits, offscreen edits, order, insertion/deletion and filters.
- Added optional application-builder counters and a matched full-list baseline.
  Release benchmarking checks rendered values, per-action rebuild scope and
  persistence for completion, title and order workloads.
- [Results](datafriend-row-queries.md): all 36 cases / 720 measured edits passed.
  At 1,000 tasks, median frame build fell from 5.413 to 1.929 ms for completion,
  4.449 to 1.305 ms for title and 6.638 to 4.892 ms for order. Title/completion
  add about 0.12–0.14 ms median synchronous work. Actual row rebuilds fell from 12
  to one for title/completion; order rebuilds the list and 12 rows while preserving
  subscriptions. End-of-frame medians remain near 14 ms and p95 improves.

## Query syntax

- Queries now take `(QueryClause, include)` pairs. Every clause matches; only
  `true` clauses contribute result triples. Removed the public projection-index
  array and migrated app, demo, tests and Dart benchmark tools.
- Renamed the reference value type to `Entity`. Query entity positions accept
  either numeric IDs or `Entity(id)`; attribute values still distinguish numeric
  values from references. Equivalent entity syntax reuses widget subscriptions.
- Native protocol and persisted encodings are unchanged. All 32 app tests and
  five demo tests pass; app/tool/demo analysis is clean.

## UI edit conveniences

- Added `attempt(action, error: ValueNotifier<Object?>)`: synchronous success/failure
  result, retained exception on failure, and previous-error clearing on success.
  The demo error area uses ValueListenableBuilder; screen-local try/catch is gone.
- Entities expose `attributes` and `triples` for projected fields. Attribute names
  are enumerated from one native row, without decoding values or scanning views.
- `entity.delete()` / `database.deleteEntity(id)` atomically delete all database
  attributes, including unprojected ones, using the existing whole-record path.
  The demo no longer carries an attribute list for deletion.
- Regression coverage includes error display and recovery, null-valued attributes,
  partial-view retraction, hidden-field deletion, idempotence and persistence.
- Validation: all three native suites, 30 app tests and five demo tests pass;
  analysis is clean. Naming is `attempt`, as requested.

## Mixed-entity view ordering

- Missing sort attributes now sort last for each requested field; entity ID breaks
  ties. Unsorted requests retain ascending entity-ID order.
- Attributes unknown to the namespace are treated as missing from every row;
  present non-integer sort values still report an error.
- Native and Dart regression coverage includes mixed entities, secondary sorting,
  removed attributes, and integer extremes.
- Validation: all three native test suites and all ten Datafriend Dart tests pass.

## Asynchronous disk flushing

- The app now defaults to a native flush worker with 100 ms batching. All writes,
  including attribute dictionaries, ID allocation and imports, schedule flushing.
- Edits still commit and publish synchronously; disk I/O never holds the worker's
  coordination mutex or accesses query maps. Explicit flush/close drain pending work.
- Flush errors retain pending work, reject new writes and retry; readable views
  remain intact. Checked close releases resources and reports errors.
- Native tests, 24 app tests, four demo tests and analysis pass. Linux engine/worker
  ASan/UBSan/leak checks and worker ThreadSanitizer checks pass.
- Android arm64, iOS simulator and macOS Release demo builds pass.
- Datafriend-only benchmarks now separate data-ready time, frame scheduling wait
  and frame work. [Implementation and results](datafriend-async-flush.md).
- All 27 final cases pass. At 1,000 tasks, median publication is **3.811 → 0.092 ms**
  (sync → async); open/seed **3.344 s → 71.657 ms**. Paced async publication is
  **0.294 ms** median, **0.602 ms** p95 while periodic flushing is enabled.
- In the actual UI, async state is ready in **0.391 ms**; the edited frame builds
  in **6.920 ms**, with **7.298 ms** waiting for its start. Total end-of-frame is
  **14.498 ms**; the remaining cost is frame scheduling and Flutter rebuilds.

## Todo demo and InstantDB comparison (before asynchronous flushing)

- Added `src/todo_demo`: one Flutter macOS screen with selectable Datafriend and
  community `flutter_instantdb` 2.3.0 backends, independent saved lists, CRUD and
  active/completed filters. Native library bundling is automated.
- Four tests pass: real-store CRUD/reopen for both adapters, shared UI actions,
  and 100-row ordering regression. Analysis and macOS Release build pass.
- Found the package's 200 ms query debounce and a query-ordering bug; the adapter
  sorts results explicitly, without patching the dependency. Sorting is timed.
- Benchmarks separate write completion, publication, visible/full reads and real
  Flutter frames. Both buffered and default-durable writes are measured; network
  sync is excluded. [Methodology and results](datafriend-instantdb.md).
- All 28 Release-app cases pass. At 1,000 tasks, median buffered publication is
  **0.038 ms / 465.500 ms** (Datafriend / Flutter InstantDB); default-durable
  publication **3.733 / 465.035 ms**. Actual UI end-of-frame **14.362 / 481.344 ms**.
- Default-durable bulk seeding remains slow on Datafriend (3.36 s for 1,000 tasks).
  Async disk flushing and network integration remain unfinished; no new blockers.

## Selective row caching

- Native query results record actual changed entities, projected attributes and
  membership changes. `view.changes` is read in the existing `onChanged` callback;
  no polling, generations or additional notification channel.
- Task/sticker projections cache models by entity ID, evict changed rows and
  retain ordering unless membership/sort fields changed. Failed edits reset all
  caches before rethrowing. Grouped strokes retain conservative rebuilding.
- All 23 Flutter tests pass with profiling both on and off. A 1,000-task toggle
  rebuilds one Task, decodes three row fields and creates zero ordering IDs.
- Native randomized query checks now verify exact change summaries, including
  joins, imports, predicates and batched changes. Native tests and Linux
  ASan/UBSan/leak checks pass (231.5 s). All 352 interleaved AOT processes pass.
- At 1,000 tasks, edit + full read **233 → 77 µs**, edit + visible rows/count
  **38 → 24 µs**. Edit-only **15.7 → 17.3 µs** pays for summary handling. Same
  native library for both adapters; warm-cache reads, no rendering/network/fsync.
- No implementation blockers. [Design and results](datafriend-row-cache.md).

## Decoder optimization

- Replaced the general reader in single-value decoding with checked fixed offsets,
  direct UTF-8 ranges and one owned binary copy. Wire format and read API unchanged.
- Boundary values, Unicode, binary ownership and retained-value checks pass;
  all 21 Flutter tests pass and changed-file analysis is clean.
- All 352 interleaved old/new AOT benchmark/validation processes pass, plus 399
  local malformed/offset/ownership decoder checks. At 1,000 tasks: pure decoding
  **191 → 37 µs**, native reads/decoding **320 → 154 µs**, complete edit + full
  read **413 → 234 µs**, edit + 15 rows/count **40 → 37 µs**.
- [Results and reproduction](datafriend-decoder.md). Existing integration work
  remains below; no new implementation blockers.

## Read-path benchmark

- Added controlled Dart AOT cases for native/Dart ID iteration, field probes,
  value decoding, model construction, cached reads and post-edit projections.
- Three interleaved rounds at 100 and 1,000 tasks; all 176 validation/benchmark
  processes pass. Preparatory edits are excluded from isolated read timers.
- At 1,000 tasks: native ID iteration **5.6 µs**, three field probes/task without
  decoding **89.5 µs**, with decoding **323 µs**. Constructing models from existing
  Dart values costs **10.2 µs**. Conversion dominates, followed by the lookup/FFI
  boundary. Fresh app read **388 µs**, cached read **18.2 µs**.
- [Methodology, controls and next targets](datafriend-read-path.md). Harness
  analysis passes; complete edit/read controls reproduce the ~0.4 ms result.
- Production code is unchanged. No blockers.

## Grouped query planner

- Implemented grouped entity field gathers, exact-count access-path selection and
  removal of unused value bindings. Independent brute-force and clause-permutation
  tests, native/instrumented suites, 20 Flutter tests and Linux ASan/UBSan/leak
  checks pass (233 s).
- Added a 25-workload native matrix (initial queries, local/reference edits,
  predicates, churn, batching, shared/distinct subscribers and no-ops), with
  preserved incremental-query library as baseline. [Details](datafriend-query-planner.md).
- All 550 native-matrix and 120 app/Redux processes pass. At 10,000 tasks: initial
  selective-last query **1,170 → 22 µs**; value-join edit **4,804 → 71 µs**;
  32-entity batch **263 → 240 µs**. Largest matrix median regression: 3.7%.
- Existing 1,000-task app operation changes modestly: edit **14.6 → 14.2 µs**,
  visible rows/count **39.4 → 38.8 µs**, full read **397 → 392 µs**. The remaining
  simple-edit gap to Redux is chiefly outside full-query planning. No blockers.

## Incremental entity-local queries

- Compile queries whose clauses all use the same entity term as entity-local.
  On edits/imports, reevaluate and prune only the changed entities for those queries.
  Cross-entity joins and all-facts queries retain full reevaluation.
- Reuse the existing matcher, including predicates, variable equality and duplicate
  projections. Initial subscriptions and failure recovery still evaluate fully.
- Differential tests compare 12 queries with fresh full evaluations through
  predicate changes, deletions, imports and 120 randomized batches. A 1,000-task
  edit now visits/emits/prunes 5 facts each, with one replacement.
- Native suites, instrumented work-count assertions, 20 Flutter tests and Linux
  ASan/UBSan/leak checks pass (163 s). All 120 before/after benchmark processes pass.
- Same Dart harness with preserved old/new native libraries: 1,000-task edit
  **252 → 14.6 µs** (17.3× faster); edit + 15 rows + count **276 → 39 µs**
  versus handwritten Redux **19 µs**. Full read **632 → 398 µs**, still dominated
  by costs outside the optimized query path. [Results](datafriend-incremental-queries.md).
- No changes to the Dart interface, read-cache invalidation or storage transaction.
- Datalevin's grouped entity scans, index-count costing and query caches informed
  the subsequent planner. [Research notes](datafriend-datalevin-notes.md) preserve
  the rationale. No implementation blockers.

## Direct read interface

- Replaced app bulk exports, `Triple` reconstruction and Dart entity maps with
  direct native field reads. Values are decoded immediately; borrowed native
  memory never escapes to application code.
- Native-owned uint32 ordering indexes support lazy UI rows. The task header
  counts completed entities natively. Explicit document exports still read all rows.
- The 1,000-task test proves that 15 visible rows create 15 task objects,
  decode 45 fields and reconstruct zero triples.
- Interleaved old/new AOT reader comparison against the same native library:
  1,000-task edit + full readback **2.38 ms → 0.65 ms**; edit + 15 visible rows
  plus completion count **0.28 ms**. All 48 benchmark processes pass.


## Completed implementation

- Native query evaluator now mutates ordinary `unordered_dense` nested maps.
  Shared handles observe current contents; last unsubscribe retains only the
  final contents for outstanding handles. No retained version history.
- Removed local view reconciliation from edits and the native snapshot ABI.
  Added direct edit operation and changed-subscription replies to edit/import.
- Dart offers direct `database.edit(add:, remove:)`, live result references and
  subscription callbacks. App callbacks submit direct database edits; notifications
  invalidate typed projections without decoding unchanged results.
- Updated native and Flutter tests for live lifetime, subscription sharing,
  selective notifications, no-op edits and unsubscribed database edits.
- Added a failure injected after map mutation to verify full recovery before error
  return. Map-full rollback and subsequent writes also pass.
- Updated benchmark adapters to use mutable results and notification-driven caches.
  Historical CSVs are preserved; current runs use separate output files.

## Validation

- Native engine and historical tree suites pass, including direct reads, signed
  ordering, borrowed-field identity and poisoned-result errors.
- All 20 Flutter tests pass with profiling on/off; analysis is clean.
- All 48 AOT benchmark processes pass correctness and notification checks.
  The comparison uses preserved old/new executables with the same native library.
  Timing excludes Flutter rendering, synchronous disk flushing and networking.
- Linux engine ASan/UBSan/leak checks pass (51 s), including the new pointer and
  ID-buffer APIs. The previous sanitizer timeout adjustment remains in place.
- Android arm64 and iOS simulator debug builds pass.
- No new implementation blockers.

## Original Redux comparison (before incremental queries)

- Added a reproducible comparison with production Redux Toolkit, Toolkit without
  Immer freezing, and a handwritten Redux reducer. No application code changed.
- 1,000-task edit + 15 visible rows + completion count: Datafriend **0.278 ms**,
  default Toolkit **0.126 ms**, Toolkit without freezing **0.021 ms**, handwritten
  Redux **0.019 ms**. Full read: **0.638 / 0.115 / 0.015 / 0.009 ms**, respectively.
- Three interleaved rounds, 2,000 warmup + 3,000 timed edits per case. All 24 small
  validation processes and 72 timed processes pass; Dart benchmark analysis clean.
- Redux directly updates known task state and retains unchanged row objects.
  Datafriend still performs a DB transaction, index/query maintenance and Dart
  field decoding. Rendering, networking and synchronous disk flush are excluded.
- See [methodology, results and limits](datafriend-redux-benchmark.md). No blockers.

## Existing integration work

- Async disk flushing is implemented and enabled by default. Explicit flush/close
  provide durability barriers; historical `durable:false` measurements used no
  automatic flushing. See [current measurements](datafriend-async-flush.md).
- Live network transport and UI-isolate delivery remain to be connected. Background
  I/O must not mutate published maps concurrently with app reads.
- Physical-device/full-frame performance has not been established.

See [direct read interface and measurements](datafriend-direct-reads.md),
[current design](datafriend-live-results.md), [native API](datafriend-native.md),
[historical design discussion](datafriend-design.md) and
[earlier measurements](datafriend-packed-benchmark.md).
