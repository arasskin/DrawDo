# Yrs document demo

Implemented and measured, 2026-10-08. This replaces the custom RGA described in
[datafriend-document-sync.md](datafriend-document-sync.md).

The benchmark acknowledgment fact and its subscriptions/tracking have since been
removed. Historical round-trip measurements below describe the previous build;
see [current acknowledgment-free sync](datafriend-noack.md). Schema ID 36 is
reserved for reading old stores, and old local receipt facts are deleted on open.

## Dependency

The app uses [Yrs 0.24.0](https://github.com/y-crdt/y-crdt/tree/v0.24.0), commit
`7dbc9d402533af3efa1e936342abeec499988b6b`, through its `libyrs.h` C API. It is
**not header-only**: CMake builds and statically links its Rust implementation.
Datafriend itself has no CRDT or Rust dependency. The MIT license, unmodified
upstream sources/header, locked Cargo dependencies and file hashes are retained
under `third_party/yrs`. The newer 0.28.0 release did not compile with the installed
Rust toolchain, so it was not adopted.

CRDT-Lite was considered for its header-only integration, but its line-based text
model and upstream warning about broken automatic merging do not fit concurrent
character editing. No CRDT-Lite code is included. Yrs V2 snapshots are used. Testing exposed that
0.24's deletion-set decoder still uses 32-bit client IDs even with V2; the adapter
therefore retains the library's default ID width rather than supplying 53-bit IDs.

Build prerequisites: Cargo/Rust and `aarch64-linux-android` / `aarch64-apple-ios`
targets for the phone builds. Existing CMake/Ninja and Xcode commands still work.
Cargo downloads pinned dependencies on the first build; later builds reuse
`demo-app/build/cargo-home` and `demo-app/build/yrs`.

## Storage and views

Each owner namespace has one document entity (ID 1):

| Attribute | Contents |
| --- | --- |
| `document/state` | Versioned room envelope, local edit sequence, complete mergeable Yrs state |
| `document/text` | Materialized UTF-8 text, the **single fact projected by the UI** |

All document subscriptions specify the exact entity and attribute. There are no
all-facts views or per-character database entities. A local edit runs Yrs, encodes
its state, and commits state plus text in one Datafriend transaction. Entity-ID
allocation is gone from the edit path. Disk flushing remains asynchronous.

The peer state merges into Yrs at the app layer, then the merged state and text
publish together when the merge changes the document. Duplicate state is a no-op in Yrs, preventing
endless echo. Republishing merged state makes each owner's latest snapshot
self-contained. Littlebear continues to store ordinary owner-authenticated opaque
facts and does not interpret the CRDT.

The native text editor retains an independent Yrs document created from its
observed state. Subsequent callbacks edit that document; merging it into the live
model preserves remote insertions that the editor has not observed. This replaces
our custom character-ID tracking. It still does not transform the native caret or
inject remote edits into the open platform text field.

Yrs client IDs use this release's default random 32-bit identifiers, fresh on
each document/editor open. These are suitable for this small demo; a long-lived
many-client deployment should revisit the collision budget.
They are separate from Datafriend's secure Ed25519 owner identities. Restoring
state does not reuse a possibly rolled-back client clock after an asynchronous
flush was lost.

`document-yrs-v2-<room>` directories keep the new wire/storage format separate from
old demo rooms. The exact previous attribute schema is accepted for a one-time
additive metadata upgrade so existing task stores survive IDs 34–36 being added.
Other incompatible disk formats still fail validation. Network schema matching
remains exact; the server and both phones need the updated attribute enum.

## Cost and limits

This deliberately sends a **complete encoded CRDT state** when that fact changes.
Negentropy identifies the changed fact; it does not diff the bytes inside it.
This reduces query work and record count but may increase bytes for long histories.
It is a simple document demo, not a claim that full snapshots are optimal for large
documents. State is subject to Datafriend's existing fact-size limit.

The replacement adapter compares cached UTF-8 bytes to find the changed range,
validates UTF-8 without allocating one string per character, and invokes Yrs range
insert/delete. It no longer performs tree lookups for every unchanged character.
The UI borrows its one text value until the next edit/pump.

Reports identify `yrs-0.24.0-state-fact-v2`. Edit components are now `crdt`,
`encode_state`, and `commit_publish`; the old diff/allocation/merge component names
must not be applied to these results. `operations` now counts document edits
(including seeding), not character operations. `state_bytes` and `ui_facts` make
the new representation explicit.

## Validation

- Yrs adapter: reordered/duplicate snapshots, concurrent replacements, repeated
  stale-draft edits, UTF-8 boundary changes, invalid input, deletion and 20K text.
- Model: exactly one UI fact, bounded fact/subscription count, repeated editor
  callbacks, persistence and restart after deleting the document contents.
- Real TCP integration: 1K seed, concurrent offline replacements, stale editor,
  exact one-fact UI on both replicas; passes against the local Linux server.
- Five core and five demo tests pass; Android and iPhone builds/installations pass.
- Public concurrent phone run: all 134 edits acknowledged, identical final text
  and edit counts, no pending acknowledgments/errors, both connections settled.

## Phone measurements

Same 1,000-character seed and 67 timed edits per phone, every 150 ms, against the
public cache. One run per implementation is exploratory, not a controlled study.

| Metric (median / p95) | Moto G7 | iPhone 13 |
| --- | ---: | ---: |
| Previous custom CRDT local edit | 7.14 / 7.78 ms | 2.43 / 2.77 ms |
| Yrs local edit | **0.883 / 1.074 ms** | **0.744 / 1.116 ms** |
| Yrs edit/diff work | 0.241 / 0.268 ms | 0.083 / 0.140 ms |
| Encode full Yrs state | 0.121 / 0.134 ms | 0.055 / 0.077 ms |
| Datafriend commit + publication | 0.516 / 0.722 ms | 0.603 / 0.958 ms |
| Return acknowledgment | 1.166 / 1.333 s | 0.969 / 1.336 s |

Component medians need not sum to the median total. Local edit maxima were
1.194 ms / 7.223 ms; the iPhone outlier occurred in commit/publication, which these
timers cannot further attribute. Both devices had 100% presentation coverage at
59.97 / 59.93 Hz. Android had one >25 ms display gap; iPhone had none. These are
not claims of perfectly smooth rendering or subsecond remote display latency.

Both final state snapshots were **1,222 bytes**. Each UI projected **one fact**.
Total sent/received bytes (including initialization and settlement): Android
97,147 / 92,282; iPhone 109,413 / 83,099. The return acknowledgment still traverses
both directions through owner and subscriber synchronization. This change does
not remove the protocol's successive round trips.

### Remaining latency

Follow-up [request latency measurements](datafriend-request-latency.md) isolate
the heartbeat and individual request types on both phones, plus server loopback
and independent ICMP baselines. The figures below describe the earlier run.

The measured mean request enqueue-to-reply-processing time was 115.42 ms on
Android (131 replies) and 117.39 ms on iPhone (140 replies). This includes client
scheduling, transport, server work and server cache-file I/O; it is not a pure
wire RTT. Counters cover initialization and settlement as well as timed edits.

For these two/three-fact namespaces, reconciliation explicitly lists IDs (the
threshold is 32), and the steady-state path is:

1. Owner starts an exchange, reconciles, then sends signed changed facts: three
   successive request/replies.
2. Server sends the subscriber a reconciliation opening containing the IDs;
   subscriber fetches changed values: one request/reply after that notification.
3. Peer applies facts, merges Yrs, publishes merged state/text and acknowledgment.
4. Peer owner upload and original phone subscriber fetch repeat the same path.

Thus an isolated acknowledgment follows roughly eight request/reply waits, plus
notification delivery and scheduling. Eight times the observed request mean is
about 0.93 seconds, an illustrative explanation rather than a measured phase
breakdown. Concurrent edits coalesce, exchanges overlap across channels, and
edits made after an upload snapshot can wait for a subsequent exchange. Network
pumping once per frame adds scheduling granularity; part of that time is already
inside the request timer. Only one restart on Android and none on iPhone occurred.

Whole-run native reconciliation, signing, received-fact application and app CRDT
merge/publication totaled 10.10/23.23/19.74/24.43 ms on Android and
6.61/13.24/19.85/13.41 ms on iPhone. These are cumulative work counters, not
per-edit times, and must not be added to the inclusive pump/request timers.
Phone disk flushing is asynchronous; server request replies still await their
cache-file reads/writes (without an explicit fsync barrier).

The peer can display an edit before its acknowledgment completes the return
journey. This benchmark does not separately measure one-way edit-to-presentation
latency. Exact attribution needs per-phase traces, especially server I/O and
waiting for an already active exchange. The evident next protocol target is the
number of sequential exchanges, not more UI/query optimization. Both the full
CRDT state and materialized text currently cross the network; that duplication
also deserves review for larger documents.

[Raw successful run](benchmarks/datafriend-document-yrs/d1abe2a2199c41d98b049d4c6870681a-summary.json).
The earlier `0881c7790e1a458ab77494e227dffe4e` run used unsupported 53-bit IDs with
V1 and failed convergence; its raw reports are retained and excluded.

[Validation and binary hashes](validation/datafriend-document-yrs.json). Both phones
are left paired to the interactive public-server room `yrs-document-demo`.
