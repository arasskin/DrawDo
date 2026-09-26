# Littlebear operational checklist

Littlebear is operational when an owner can sync a namespace to the server,
another client can reconcile a local copy, and both can repeat this after
edits, disconnects, and loss of the server cache. Work below is ordered by
dependency; repairing an existing component counts as unfinished work.

## Fixed design rules

- The namespace owner is always authoritative.
- Littlebear is a reconstructable cache. Server durability is not required.
- Owners make the server match their local state; non-owners make their local
  copies match an existing server namespace.
- A missing server namespace leaves a non-owner's last-known local copy intact.
- An existing empty server namespace reconciles non-owners to empty, whatever
  the reason for that empty state.
- Consistency is eventual. Fast, repeated reconciliation should keep periods
  of divergence short; readers do not wait for cache-completeness guarantees.
- Keep the custom tree and allocation design, the single event loop with
  asynchronous I/O, and upstream negentropy.
- Target one CPU core, approximately 500 MB RAM, and 10 GB total storage.
  Keep index entries compact; the smaller namespace table may spend more space
  to improve speed. Preserve io_uring batching and buffer reuse opportunities.

## 1. Establish a repeatable build and correctness checks

- [ ] Provide a documented Linux build environment with C++23, liburing, and
  the dependencies needed by the vendored negentropy engine.
- [x] Add component-test build targets, header dependencies, and distinct
  debug/release outputs.
- [ ] Add server integration-test targets once storage and sync are connected.
- [ ] Compile every storage component in checks, including headers that the
  server does not yet include.
- [x] Run component checks with address and undefined-behavior sanitizers,
  and automate the Linux build and tests in CI.
- [ ] Establish component benchmarks and memory accounting from the start:
  bytes per record/namespace, allocation overhead, lookup/update/range throughput,
  and resident memory on the target machine. Measure optimized builds separately
  from sanitizer runs.

## 2. Repair and validate the storage primitives

- [x] Fix size-class rounding so every supported request gets enough space;
  explicitly handle the upper size limit and zero-length values.
- [x] Fix tree-arena and fingerprint-array guard-page alignment; validate
  rank boundaries, index arithmetic, exhaustion, reuse, and mapping cleanup.
- [ ] Repair the namespace table's stale `zip_zip` references; test lookup,
  replacement, growth, deletion, and allocation failures.
- [ ] Preserve the distinction between a missing table entry and an existing
  entry with an empty tree. Do not use tree emptiness as namespace absence.
- [x] Validate the growing slab allocator's growth and failure paths, including
  free-list pointer validity if memory moves.
- [x] Reproduce and repair the suspected tree-deletion traversal/index bug;
  check node reclamation and partial-allocation rollback.
- [x] Compare randomized insert, replace, delete, lookup, and range operations
  against a simple reference map, checking ordering and subtree summaries.

## 3. Build the namespace and record storage API

- [ ] Specify namespace identity and ownership, record keys, byte encoding,
  size limits, and how records obtain negentropy timestamps and 32-byte IDs.
- [ ] Add explicit namespace lookup, creation, clearing, and removal operations;
  clearing preserves an empty namespace, while removal makes it absent.
- [ ] Implement value-file reads and writes using the size-class allocator,
  with explicit offset/length handling and I/O error reporting.
- [ ] Connect namespaces, trees, and values through insert/replace, get, and
  delete operations; reclaim replaced/deleted values and removed namespace data.
- [ ] Keep tree entries and value allocations valid across asynchronous I/O
  and failures; publish records only when their bytes are available to read.
- [ ] Define a clean cache reset on startup so stale value-file contents or
  lost allocation metadata cannot be mistaken for valid cached records.

## 4. Adapt the custom tree to upstream negentropy

- [ ] Budget the per-record cost of IDs, ordering metadata, accumulators, counts,
  and record-ID lookup before selecting their layouts; measure the resulting
  memory use and throughput against the target machine's limits.
- [ ] Implement compatible accumulators and fingerprints, replacing the
  current XOR scheme for reconciliation.
- [ ] Support ordering by timestamp and record ID, including timestamp ties,
  and maintain the counts needed for efficient access by position.
- [ ] Implement the storage adapter: size, item access, ordered iteration,
  lower-bound search, and range fingerprints.
- [ ] Provide record-ID lookup for fetching the bytes identified by a sync.
- [ ] Verify the adapter against an upstream reference storage implementation
  for equal, empty, disjoint, overlapping, and incrementally edited sets.

## 5. Define and implement the wire protocol

- [ ] Specify versioned, length-delimited messages for namespace status,
  reconciliation rounds, record transfer, and owner mutations, with explicit
  success/error responses and binary-safe values.
- [ ] Return distinct results for an absent namespace and an existing empty
  namespace; a read of an absent namespace must not silently create it.
- [ ] Authenticate ownership and enforce owner-only writes; define and enforce
  the read-access policy for non-owners.
- [ ] Handle fragmented/coalesced TCP messages, partial sends, malformed
  requests, and messages larger than the current receive buffer.
- [ ] Handle connection-pool and io_uring queue exhaustion without crashing;
  bound message sizes, queued work, and per-connection resources.
- [ ] Manage asynchronous request state, disconnect cleanup, and retries so
  failed or repeated requests do not corrupt storage or leak resources.

## 6. Connect the complete reconciliation flow

- [ ] Drive negentropy rounds and transfer the identified record differences.
- [ ] Apply owner sync toward the server, including deletion of server records
  absent from the owner's view; server state never deletes owner-local data.
- [ ] Supply non-owner reconciliation with server records and deletion results
  for existing namespaces, including existing empty namespaces.
- [ ] Handle concurrent edits and interrupted rounds with safe retry/restart
  behavior, so repeated sync converges once edits stop. Preserve memory safety
  without imposing a cache-completeness barrier on readers.
- [ ] Add a small test client that exercises both owner and non-owner roles,
  retains local state, and repeats sync after edits and reconnects.

## 7. Prove recovery, operation, and sync speed

- [ ] Run an end-to-end scenario: owner creates records, reader syncs, owner
  replaces/deletes records, and reader converges to the resulting set.
- [ ] Verify that emptying an existing namespace clears a reader's copy, while
  removing the namespace leaves the reader's last-known copy intact.
- [ ] Kill and restart the server with its cache lost; verify the owner retains
  its data, the reader retains its last-known copy while the namespace is
  absent, and both converge after the owner repopulates the cache.
- [ ] Exercise failed I/O, disconnects during transfers, repeated requests,
  concurrent sync sessions, and resource exhaustion under integration tests.
- [ ] Add configuration for listen address/port, cache file, and resource limits,
  plus useful error logs, resource cleanup, and startup/shutdown instructions.
- [ ] Set memory and cache-file budgets with OS/page-cache headroom; replace the
  prototype's 45 GiB value-file limit and bound deployment-release/log retention
  so total disk use fits the 10 GB server.
- [ ] Measure sync latency, round trips, bytes transferred, memory use, and
  event-loop responsiveness for initial sync, no changes, small edits, and cache
  rebuilds at representative namespace sizes and connection counts.
- [ ] Set concrete performance targets from those measurements, then tune
  batching and work per event-loop turn to keep reconciliation fast.

## Later additions

Subscriptions/change notifications can trigger reconciliation sooner. They are
not required for the first operational backend if clients initiate sync.
Mobile native bindings and durable owner-side app storage belong to the client
workstream; the test client allows backend completion to be verified first.

Server write-ahead logging, durable restart recovery, conflict merging, and
reader cache-completeness gating are not requirements of this design.
