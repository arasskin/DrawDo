# Starting point

## Decisions

DrawDo targets both iOS and Android. The intended client stack is Kotlin
Multiplatform and Compose Multiplatform. Littlebear will remain a C++ backend;
its custom storage design is part of the project, not something to discard for
an earlier launch date.

Use the upstream negentropy protocol engine rather than porting iCare's custom
protocol. Kotlin/Native can consume a C interface on iOS; Android will need a
JNI adapter behind the same shared Kotlin interface. The Linux server can call
the C++ engine directly.

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
Include negentropy's IDs, accumulators, counts, and any additional indexes in the
memory accounting before settling their representation.

Measure resident memory separately from reserved virtual address space, and
account for page cache, connection buffers, and operating-system headroom.
The 10 GB disk budget also includes the OS, build tools, deployment releases,
and logs. The current 45 GiB value-file allocation limit is a prototype default
that must be reconciled with the actual available cache-storage budget.

## Littlebear's current state

The network loop parses and echoes `add` and `retract` requests, but does not yet
connect them to the namespace table, tree, or value storage. Record persistence,
recovery, reconciliation requests, authentication, and subscriptions remain
unfinished. Component tests now cover the file-offset allocator, its metadata
arena, rank-partitioned arena, fingerprint array, and augmented tree. The GitHub
workflow builds the server and runs normal and sanitizer checks before deployment.

Component failures found in the source baseline:

- Fixed: a 64-byte allocation request produced a 32-byte slot; 128 produced 64.
- Fixed: tree-arena and fingerprint-array guard-page addresses were not
  page-aligned, causing `mprotect` to fail in local component checks.
- The namespace-table header still references `zip_zip` instead of
  `augmented_tree`, so compiling that header fails.

Tree insertion/deletion fingerprint updates, deletion traversal, allocation
rollback, and maximum-key range handling have also been repaired. Nodes remain
16 bytes; tree operations use iterative paths and explicit allocation/cleanup.
See [component development](littlebear-components.md) for contracts and checks.

Inspection also found missing TCP framing, partial-send handling, and graceful
connection-pool exhaustion. The README includes design goals and historical
concurrency descriptions; it should not be read as a list of working features.

The server now builds and runs on the Linux droplet with liburing through the
[deployment workflow](deployment.md). Its health check verifies the prototype's
request loop; storage and reconciliation remain disconnected.

## Negentropy integration

Upstream negentropy reconciles 32-byte record IDs ordered by timestamp and ID.
It identifies differences; record transfer, authentication, and deletion policy
belong to the application protocol.

Littlebear's current 64-bit XOR fingerprints are incompatible with upstream's
fingerprint scheme. Retaining the custom tree requires a storage adapter with
compatible accumulators, subtree counts, ordered iteration, and record-ID
lookup. This work should be tested against upstream behavior.

The imported upstream C interface was built on macOS in a temporary checkout,
and a small two-set reconciliation check returned the expected have/need IDs.
This does not verify mobile packaging or littlebear integration.

## First milestones

See [the Littlebear operational checklist](littlebear-checklist.md) for the
detailed backend work and acceptance checks.

1. Add regression checks and repair the known littlebear component failures.
2. Validate tree insert/update/delete and range operations against a simple
   reference model, including randomized sequences and sanitizers.
3. Define ownership authentication, public/private collections, record encoding,
   versions, and owner-authoritative reconciliation under eventual consistency.
4. Implement durable owner-side persistence and server-cache reconstruction,
   preserving non-owner local copies when the server namespace is absent and
   reconciling them to empty when the server namespace exists but is empty.
5. Establish upstream-compatible reconciliation and record transfer in the
   backend and mobile native bindings.
6. Build a Kotlin client slice on both platforms: drawing and dragging, local
   save/reload, and reconciliation with the backend.

The existing iCare physics, drawing behavior, task model, and notification
behavior are available under `reference/icare/`. They are behavior references,
not requirements to reproduce the old internal architecture or its bugs.
