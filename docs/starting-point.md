# Starting point

## Decisions

DrawDo targets both iOS and Android. As of 2026-10-08, the client direction is
**C++ with Sokol**, superseding the earlier pure Dart/Flutter choice. The Flutter
client is unsupported historical reference. Native platform/rendering code has been
recovered from the original junstodos iOS app. A standalone Sokol particle app
now runs on Android alongside a persistent task list with native text editing.
The native demo now also builds on iOS; native TCP orchestration remains follow-on work.
See [the recovered source and platform assessment](sokol-client.md).
Littlebear remains a C++ backend with its custom storage design.

Datafriend now implements local persistence, queries and editable views in C++
behind a synchronous native interface. Networking remains integration work; see
[the current implementation contract](datafriend-native.md). Its main entity-map data structure
is called a **littlebear**; **LittleBear** names the network server.

Use a custom negentropy variant with 64-bit xxHash contributions combined by
XOR. App views reconcile triples with Datafriend; Datafriend reconciles whole
entity records with the server. The app is authoritative when submitting an
edited view, including retractions. Datafriend applies those changes, recomputes
affected queries, and becomes authoritative for view refreshes. The full agreed
flow and open protocol details are in [datafriend-design.md](datafriend-design.md).
Start with fresh local data; migration from iCare is not required.

The namespace owner is always authoritative. Owner-to-server reconciliation
must make the server's cached namespace match the owner's state, including
removing records that are absent from the owner's authoritative view. The
server does not merge competing versions or override the owner's state.

Littlebear is a reconstructable cache, not a durable source of truth. Cached
data may be lost and rebuilt from the owner. An empty or incomplete server
cache must not be interpreted as an instruction to delete the owner's local
data. Durable owner-side storage and cache reconstruction are required;
server-side durable storage is not a requirement.

Synchronization is eventually consistent. The goal is to make reconciliation
fast enough that inconsistent states are brief, rather than requiring readers
to wait for a cache-completeness or freshness guarantee.

A missing namespace and an existing empty namespace have different meanings:

- If a namespace is absent from the server (for example after a server crash),
  a non-owner keeps its last-known local copy until the owner syncs again.
- If a namespace exists on the server but contains no records, a non-owner
  reconciling against it obtains an empty namespace, regardless of why it is
  empty. Readers do not need to distinguish rebuilding from owner deletions.

Non-owners reconcile their local copies to existing server namespaces; owners
reconcile the server to their own authoritative local state.

## Backend resource and performance baseline

The target droplet has one CPU core, approximately 500 MB RAM, and 10 GB total
storage. These constraints underlie Littlebear's design. Compact data structures
and high throughput on this machine are requirements throughout implementation.

The record index dominates metadata volume, so per-record and per-node overhead,
allocation behavior, and cache locality deserve particular attention. The
namespace hash table is expected to hold an order of magnitude fewer entries
than the index and may trade space for faster lookup when the total cost fits
the machine's budget. Structures need not all make the same space/speed tradeoff.

The single-threaded io_uring event loop is a throughput-oriented design. Preserve
its opportunities for batching, asynchronous I/O, buffer reuse, and low syscall
overhead. Assess hot-path changes with component measurements as they are built.
The storage index now uses the mutable hash-ranked wide tree with a shared
size-class slab allocator. Leaves hold keys, packed file references and 64-bit
fingerprints; nodes cache subtree counts and XOR summaries. Namespace headers
remain 24 bytes. The namespace directory now uses vendored `unordered_dense`,
reuses fingerprint hashes and has no application namespace-count limit. The
prior binary tree, arena and two-choice table remain as benchmark/test baselines. See the
[tree comparison](datafriend-tree-benchmark.md),
[allocator comparison](datafriend-allocator-benchmark.md), and current
[storage contract](littlebear-storage.md). Include whole slab capacity, protocol
buffers and additional indexes in memory accounting.

Measure resident memory separately from reserved virtual address space, and
account for page cache, connection buffers, and operating-system headroom.
The 10 GB disk budget also includes the OS, build tools, deployment releases,
and logs. The value-file allocation limit is now 1 GiB. Resident-memory admission
and deployment/log retention still need a complete server resource budget.

## Littlebear's current state

The default build is now the LBS5 TCP server: one io_uring loop handles connections,
signed owner writes, reconciliation, paged transfers and persistent subscriptions.
The echo prototype has been removed. `server.cpp` coordinates protocol parsing,
authentication and sync; storage and reconciliation remain independent of sockets.
See the [module map](../littlebear/README.md) and [network contract](datafriend-network.md).

The storage API provides safe asynchronous lifetimes, rollback, and cache reset at
startup. Native and Linux integration tests cover stale exchanges, owner changes
during subscriber reads, replay protection, reconnect and cache reconstruction.
Resource budgets and operational performance measurement remain on the
[checklist](littlebear-checklist.md). Deployment scripts target the real server;
existing installations need the updated unit and libsodium from bootstrap.

## Reconciliation integration

The independent [reconciliation header](reconciliation.md) uses 8-byte record IDs
and separate 8-byte fingerprints with cached XOR summaries. IDs are chosen
record keys and remain stable across content edits; timestamps and
upstream's 32-byte identity format are not required. The checked-in upstream
engine and C bindings have been removed. Original B-tree measurements remain
reproducible through an optional pinned reference fetched into the build cache.

The engine is allocation-free and has an adapter for the existing shared wide
index: record keys supply IDs and stored hashes supply fingerprints. Explicit
lists compare both fields to detect same-key content changes. Canonical record bytes, transport framing, authentication, and owner-directed
transfer are implemented in the [network interface](datafriend-network.md).

## First milestones

See [the Littlebear operational checklist](littlebear-checklist.md) for the
detailed backend work and acceptance checks.

Client development proceeds alongside the backend repairs. The first
[`src/app/` slice](../src/app/README.md) implements personal tasks, drawing,
bounded dragging with deceleration, and local save/reload. Storage has a small
interface with an in-memory test substitute. Friends, the full force-based
physics, and synchronization remain to be ported. Connect real reconciliation
when the backend protocol is ready; local client development does not depend on
completing the backend milestones below.

1. Add regression checks and repair the known littlebear component failures.
2. Validate tree insert/update/delete and range operations against a simple
   reference model, including randomized sequences and sanitizers.
3. Define ownership authentication, public/private collections, record encoding,
   versions, and owner-authoritative reconciliation under eventual consistency.
4. Implement durable owner-side persistence and server-cache reconstruction,
   preserving non-owner local copies when the server namespace is absent and
   reconciling them to empty when the server namespace exists but is empty.
5. Establish the agreed custom reconciliation and record transfer between app
   views, Datafriend, and the backend.
6. Integrate the Dart/Flutter client slice on both platforms: drawing and
   dragging, local save/reload, and reconciliation with the backend.

The existing iCare physics, drawing behavior, task model, and notification
behavior are available under `reference/icare/`. They are behavior references,
not requirements to reproduce the old internal architecture or its bugs.
