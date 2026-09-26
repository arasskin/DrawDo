# Littlebear storage API

`littlebear/src/storage.h` joins the namespace table, augmented trees, and
size-class value-file allocator. `storage_uring.h` provides the io_uring submission
and completion adapter. Production code uses plain structs, namespace functions,
and explicit lifetime management.

## Scope and record format

This is an internal API for trusted callers on one event-loop thread. Namespace
IDs are the existing 128-bit table identifiers; record keys are unsigned 64-bit
integers, including zero and `UINT64_MAX`. Values are opaque binary payloads of
1–65,535 bytes. Offset zero is valid. The packed value reference contains a 48-bit
file offset and a 16-bit payload length; file limits above 2^48 are rejected.

Callers supply the current 64-bit tree fingerprint with a write. It must describe
the supplied record consistently. This is still the prototype XOR summary, not a
negentropy ID or ownership proof. Public namespace identity, authentication,
record encoding, negentropy timestamps/32-byte IDs, and timestamp-tie ordering
remain unresolved protocol/adapter work. The wire API must enforce owner-only
mutations before invoking this layer.

## Namespace and record operations

- `create_namespace` is idempotent and creates an empty namespace explicitly.
- `lookup_namespace` distinguishes absent, empty, and populated namespaces.
  Reads never create a namespace.
- `prepare_write` reserves a new file slot and cleanup metadata; successful I/O
  completion inserts or replaces the tree entry. The old value remains indexed
  until the new bytes are completely written. A failed replacement preserves it.
- `prepare_read` pins the indexed value and checks the destination capacity.
  Successful completion reports its payload length in `request.transferred`.
- `erase_record` removes the index entry and returns its file slot. Erasing the
  last record leaves an existing empty namespace.
- `clear_namespace` reclaims records but keeps the namespace entry.
  `remove_namespace` reclaims records and then removes that entry.

Clear/remove process at most 256 records by default, returning `more_work` if
records remain. Call again on a later event-loop turn. Metadata exhaustion can
also leave a partially cleared namespace; remaining entries are valid and the
operation is retryable. Clearing/removal is not an atomic snapshot transition.
These operations return `busy` while that namespace has pending I/O.

Tree replacement updates its leaf value and all affected fingerprints without
allocation or topology changes. Existing rank priorities remain in place. New
record insertion can still fail to allocate tree nodes after its write finishes;
that failure returns the new file slot and leaves the published index unchanged.
Release tickets reserve free-list metadata before mutation or I/O, so rollback
and retirement do not depend on a successful allocation at completion time.

## Asynchronous lifetime and admission

Initialize `store` and `request` with `{}`. Create the store in its final location:
namespace tree headers borrow its allocator address. The store, active requests,
and their buffers must not move or be copied during use. Write buffers must also
remain unchanged until completion. The driver owns request/buffer storage.

The default pending limit is 64 operations. A bounded intrusive list tracks only
active I/O; there is no added bookkeeping per stored record. Mutations serialize
per record, multiple reads can share one record, and operations on different
records can be batched, including records in the same namespace. Requests carry
namespace IDs and re-look up tree headers at completion, so unrelated namespace
growth or deletion cannot invalidate an outstanding request.

The driver flow is:

1. `prepare_read` or `prepare_write` returns `ok` and leaves a ready request.
2. `storage_uring::enqueue` reserves an SQE and marks the request submitted. If
   the SQ is full, it returns false and leaves the request ready for retry.
3. The event loop submits SQEs, routes storage CQEs to `storage_uring::complete`,
   and marks those CQEs seen itself.
4. `more_io` means a short transfer or retryable `EINTR`/`EAGAIN`: re-enqueue the
   remaining bytes at the advanced offset. Zero progress/EOF is an I/O error.
5. Any terminal completion releases the request from the active list. The driver
   can then reuse or free its request and buffer. Failed reads may have partially
   filled the buffer; consume data only after a successful terminal completion.

`cancel` releases a ready request. A submitted request returns `busy`: the driver
must wait for its original completion, including after kernel cancellation or a
client disconnect. Failed submission must not free buffers referenced by queued
SQEs. `destroy` also returns `busy` until all requests finish. The lower-level
`start_io`/`complete_io` pair allows deterministic tests of the same state machine.

## Cache reset and resource limits

Creation opens a dedicated disposable value file and takes a nonblocking exclusive
lock before truncation. Another store cannot reset that same live file. After
allocation setup succeeds, startup truncates the file and starts with an empty
namespace table and fresh allocation metadata. Cached bytes are never interpreted
as recovered records. Owners must repopulate the cache after restart.

The server now initializes this store on startup with a 1 GiB logical file limit,
replacing the old 45 GiB prototype limit. It does not preallocate that disk space
or require fsync durability. Writes can still fail with ENOSPC and report it.
The file extent is bounded, but OS/page-cache headroom, resident index admission,
and deployment/log retention still need a complete resource policy.

The TCP loop still parses/echoes prototype requests. It does not yet invoke record
operations or dispatch storage completions; framing, authentication, and the sync
protocol must be connected before clients can use the storage API over TCP.

## Verification

`make test` runs actual value-file I/O and 5,000 randomized insert/replace/delete
operations against a reference map, checking bytes and full/range fingerprints.
Tests cover binary and maximum-sized values, short transfers, failed partial
writes, read EOF, file capacity, metadata/node exhaustion, rollback/reuse,
cancellation, concurrent reads/writes, table growth during I/O, bounded clearing,
absent-versus-empty behavior, file locking, and reset on reopen. The tree suite
also checks allocation-free replacement against its structural reference model.

On Linux the storage test includes real io_uring batched writes, a read, and SQ
exhaustion. Environments denying io_uring report a skip for that part only; set
`LITTLEBEAR_REQUIRE_URING=1` to require kernel integration. It is required for the
on-droplet validation. Normal and sanitizer targets include the storage test;
Linux therefore needs liburing installed for these targets as well as the server.

On the one-core droplet, GCC 13.3 reports a 1,856-byte store and 104-byte requests:
64 pending requests need 6,656 bytes, excluding their buffers and allocated
table/arena/heap storage. No new bytes were added to each tree node or namespace
table entry. On 2026-09-26 the storage test, including required real io_uring,
passed ASan/UBSan under a 160 MB memory cap and 75% CPU quota, peaking at
17,408 KiB resident memory. This is a correctness-test footprint, not a loaded
server capacity or throughput measurement.
