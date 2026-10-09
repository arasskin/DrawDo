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
  asynchronous I/O, and the compact 64-bit reconciliation protocol.
- Target one CPU core, approximately 500 MB RAM, and 10 GB total storage.
  Keep index entries compact; the smaller namespace table may spend more space
  to improve speed. Preserve io_uring batching and buffer reuse opportunities.

## 1. Establish a repeatable build and correctness checks

- [ ] Provide a documented Linux build environment with C++23, liburing, and
  the standalone reconciliation checks (no crypto-library dependency).
- [x] Add component-test build targets, header dependencies, and distinct
  debug/release outputs.
- [x] Add server integration-test targets once storage and sync are connected.
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
- [x] Repair the namespace table's stale `zip_zip` references; test lookup,
  replacement, growth, deletion, and allocation failures.
- [x] Preserve the distinction between a missing table entry and an existing
  entry with an empty tree. Do not use tree emptiness as namespace absence.
- [x] Validate the growing slab allocator's growth and failure paths, including
  free-list pointer validity if memory moves.
- [x] Reproduce and repair the suspected tree-deletion traversal/index bug;
  check node reclamation and partial-allocation rollback.
- [x] Compare randomized insert, replace, delete, lookup, and range operations
  against a simple reference map, checking ordering and subtree summaries.

## 3. Build the namespace and record storage API

- [x] Specify namespace identity and ownership, record keys, byte encoding,
  size limits, and canonical full-record hashing into separate 64-bit fingerprints.
- [x] Add explicit namespace lookup, creation, clearing, and removal operations;
  clearing preserves an empty namespace, while removal makes it absent.
- [x] Implement value-file reads and writes using the size-class allocator,
  with explicit offset/length handling and I/O error reporting.
- [x] Connect namespaces, trees, and values through insert/replace, get, and
  delete operations; reclaim replaced/deleted values and removed namespace data.
- [x] Keep tree entries and value allocations valid across asynchronous I/O
  and failures; publish records only when their bytes are available to read.
- [x] Define a clean cache reset on startup so stale value-file contents or
  lost allocation metadata cannot be mistaken for valid cached records.

The internal [storage API](littlebear-storage.md) now provides these operations
and an io_uring adapter. The [TCP endpoint with signed owner writes](datafriend-network.md)
connects them to Datafriend; operational scaling and deployment remain separate.

## 4. Connect the compact reconciliation engine

- [x] Replace the vendored engine with an independent header using 8-byte IDs,
  independent 8-byte fingerprints, bounded work/buffers, and no internal allocations.
- [x] Supply a shared wide-tree adapter for range fingerprints and bounded ID
  enumeration; validate the engine against an exact set model and the real tree.
- [x] Reconcile the existing key-ordered index directly using separate record
  keys and fingerprints; detect content changes without changing the ID.
- [x] Connect ID lookup to the payload bytes requested by reconciliation.
- [x] Manage stable snapshots or restart detection across network rounds.

## 5. Define and implement the wire protocol

- [x] Specify versioned, length-delimited messages for namespace status,
  reconciliation rounds, record transfer, and owner mutations, with explicit
  success/error responses and binary-safe values.
- [x] Return distinct results for an absent namespace and an existing empty
  namespace; a read of an absent namespace must not silently create it.
- [x] Authenticate ownership and enforce owner-only writes; define and enforce
  public reads for non-owners. Ed25519 challenges piggyback on reconciliation
  or absent-namespace replies; signed batches include nonce/sequence replay protection.
- [x] Handle fragmented/coalesced TCP messages, partial sends, malformed
  requests, and messages larger than the current receive buffer.
- [x] Handle connection-pool and io_uring queue exhaustion without crashing;
  bound message sizes, queued work, and per-connection resources.
- [x] Manage asynchronous request state, disconnect cleanup, and retries so
  failed or repeated requests do not corrupt storage or leak resources.

## 6. Connect the complete reconciliation flow

- [x] Drive compact reconciliation rounds and transfer the identified record differences.
- [x] Apply owner sync toward the server, including deletion of server records
  absent from the owner's view; server state never deletes owner-local data.
- [x] Supply non-owner reconciliation with server records and deletion results
  for existing namespaces, including existing empty namespaces.
- [x] Handle concurrent edits and interrupted rounds with safe retry/restart
  behavior, so repeated sync converges once edits stop. Preserve memory safety
  without imposing a cache-completeness barrier on readers.
- [x] Add a small test client that exercises both owner and non-owner roles,
  retains local state, and repeats sync after edits and reconnects.
- [x] Separate push and subscribe; multiplex namespaces on persistent connections.
  Server openings start subscriber reconciliation after updates. Changed revisions
  restart subscriber work without locking an entire exchange against owner writes.

## 7. Prove recovery, operation, and sync speed

- [x] Run an end-to-end scenario: owner creates records, reader syncs, owner
  replaces/deletes records, and reader converges to the resulting set.
- [ ] Verify that emptying an existing namespace clears a reader's copy, while
  removing the namespace leaves the reader's last-known copy intact.
- [x] Kill and restart the server with its cache lost; verify the owner retains
  its data, the reader retains its last-known copy while the namespace is
  absent, and both converge after the owner repopulates the cache.
- [ ] Exercise failed I/O, disconnects during transfers, repeated requests,
  concurrent sync sessions, and resource exhaustion under integration tests.
- [ ] Add configuration for listen address/port, cache file, and resource limits,
  plus useful error logs, resource cleanup, and startup/shutdown instructions.
- [ ] Complete memory and cache-file budgets with OS/page-cache headroom, and
  bound deployment-release/log retention so total disk use fits the 10 GB server.
  The value-file limit has been reduced from the prototype's 45 GiB to 1 GiB.
- [ ] Measure sync latency, round trips, bytes transferred, memory use, and
  event-loop responsiveness for initial sync, no changes, small edits, and cache
  rebuilds at representative namespace sizes and connection counts.
- [ ] Set concrete performance targets from those measurements, then tune
  batching and work per event-loop turn to keep reconciliation fast.

## Later additions

Subscriptions now trigger reconciliation directly from server opening packets;
the protocol does not require a separate change notification round trip.
Mobile native bindings and durable owner-side app storage belong to the client
workstream; the test client allows backend completion to be verified first.

Server write-ahead logging, durable restart recovery, conflict merging, and
reader cache-completeness gating are not requirements of this design.
