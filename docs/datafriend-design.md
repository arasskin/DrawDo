# Datafriend design

The current design is [live mutable query results](datafriend-live-results.md).
It supersedes the persistent-view and local-reconciliation decisions below.
Explicit triple edits, blocking calls, lock-free result access through one caller,
and app-owned subscription callbacks are the implemented contract. Engine calls
now execute directly on the application context; `subscribe(query)` returns the
ID and shared result together. See the [supported C++ API](../datafriend/README.md).
Dart/Flutter clients are unsupported historical reference.
The [attribute-level storage revision](datafriend-fact-storage.md) also supersedes
the whole-entity records and per-store attribute dictionary discussed below.

## Fact granularity and view scope

Choose facts around independently edited/synchronized values, and project only
what a UI consumer needs. Subscription shape and fact granularity are part of the
performance design: an all-facts subscription can turn a small edit into a scan
and reconstruction of the entire document. Do not expose a CRDT's internal
character operations as database entities merely to render its text.

The [Yrs document demo](datafriend-document-yrs.md) uses exact entity/attribute
subscriptions and one materialized text fact for its UI. CRDT state and benchmark
acknowledgments have separate facts. This is not a universal one-fact rule:
Littlebear transfers a changed fact in full, so larger facts trade less indexing
and routing work for more encoding and network bytes. Benchmark both the local
edit path and synchronization with realistic document sizes and histories.

## Historical design discussion


Current implementation: [native design and API](datafriend-native.md).
Validation and remaining work: [implementation progress](datafriend-progress.md).

## Callback mutation boundary

UI callbacks express their intended changes directly against a subscribed view.
A blocking view edit creates the authoritative working view, reconciles it with
Datafriend's baseline, updates the littlebear/indexes, reevaluates affected query
groups (including the originating group), and publishes all affected native roots
before returning to the callback. Callback execution then continues normally.

The app must not construct an authoritative `AppDocument`, diff every task back
into triples, or eagerly reconstruct a complete document as part of this mutation
boundary. `AppViews` now supplies direct callback edits and lazy read projections. The
old document adapter survives only as a historical benchmark fixture. Flutter
uses `ChangeNotifier` with `ListenableBuilder` to schedule builds after native
publication; view reading/decoding happens when widgets ask for the new state.

The original iCare completion callback directly transacts the changed completion
attribute and then calls its separate `render!` function. Its transactions target
the shared database; the new design preserves explicit callback edits while
scoping them to editable query views.

## Agreed identity revision: namespace-scoped views

Implemented after the view-maintenance profiling; see the
[comparison results](datafriend-packed-benchmark.md).
This revision supersedes the earlier hashed entity/attribute view-key design.

- Each view belongs to exactly one namespace. Namespace identity is carried by
  the view/session context, not repeated in each tree key. Cross-namespace views
  are outside this design.
- Entity IDs are unsigned 32-bit integers, unique only within their namespace.
  Different client namespaces can allocate the same entity ID independently.
  A persisted namespace-local counter is sufficient; IDs must not wrap or be
  reused while references to their earlier identity remain valid.
- Attributes have unsigned 32-bit interned IDs. Participants reconciling a view
  must agree on the attribute-ID dictionary used by that view.
- The exact, collision-free view key is
  `(uint64_t(entity_id) << 32) | attribute_id`. Integer ordering is entity first,
  then attribute ID. No identity hashing, byte-buffer construction, pair object,
  or pointer packing is required.
- Content fingerprints remain separate from identity keys; value edits retain
  their key and change their fingerprint. Persistent roots retain old values.
- Values are always boxed immutable objects with stable addresses for their
  lifetime. Indexes and view payloads reference the boxes. A value edit publishes
  a replacement box; old snapshots keep the previous box alive. Native ownership
  reclaims a box only after its final owner releases it. There is no inline
  representation for values of eight bytes or less in this initial design.
  Boxing alone does not imply value interning or pointer-based semantic equality.
- Littlebear's whole-entity storage keeps its eight-byte key representation,
  carrying the zero-extended 32-bit entity ID within its namespace.
- The wide tree must derive rank from a mix of the complete packed key, rather
  than raw low bits containing only the attribute ID. The ordered key itself
  remains the packed pair; ordinary lookups need no key hash.
- Query sharing, subscription invalidation, edits and reconciliation are scoped
  to the view's namespace. Identical packed keys in different namespaces refer
  to different facts and must not be reconciled as the same view.

## Current implementation

The identity revision above is implemented. The remaining notes preserve earlier
design exploration and benchmark rationale; the current native API is authoritative.

- Datafriend owns mutable AELMDB EAV and mutable AEV/AVE indexes sharing boxed values.
- Entity and attribute IDs are uint32. Editable views use their exact packed EA
  pair, with a separate content fingerprint and cached subtree counts/XOR.
- One store directory owns one namespace and persists its entity counter and
  attribute dictionary. Equal-rank entries share variable-width nodes; 16 is
  expected fanout, not a slot limit. Rank mixes the complete packed key.
- Persistent native wide trees provide value semantics and explicit reference
  ownership; ordinary blocking, non-leaf FFI serializes edits and publication.
- Set mutations and multi-view batches commit durably, recompute subscriptions,
  and publish all native roots before returning. No asynchronous UI edit queue,
  optimistic revision rejection, or separate deletion markers.
- The local engine and Dart app integration now exist. Server transport remains
  separate integration work; shared LBR3 reconciliation is connected and tested.

## Earlier design notes

## Terms and responsibilities

- A **littlebear** is the main entity-map data structure.
- **Datafriend** is a portable C++ embedded service that owns the authoritative
  local littlebear, persistence, query indexes, subscriptions, and networking.
- **LittleBear** is the network server. Its owner namespaces remain
  reconstructable caches of owner data, with the authority and absent/empty
  namespace rules documented in [starting-point.md](starting-point.md).
- An app-side **view** is an editable entity map representing a subscribed
  query. Datafriend maintains the corresponding view on its side.

Preserve and extend iCare's isolate boundary. The app communicates with
Datafriend through messages and negentropy reconciliation. Datafriend manages
connections, retrying, persistence, and server synchronization independently of
UI code. A thin Dart/isolate wrapper can host the portable engine; the engine
must also be usable and testable without Flutter. Independence of responsibility
does not imply unrestricted execution while a mobile app is suspended.

The app holds the views it needs. Datafriend alone maintains the complete local
littlebear for network synchronization. Existing iCare data does not need to be
migrated; start with a fresh database format.

## Two reconciliation representations

| Boundary | Reconciliation unit |
| --- | --- |
| App view <-> Datafriend's copy of that view | `(entity, attribute, value)` triples |
| Datafriend <-> LittleBear server | Whole-entity records |

Attribute values have set/remove semantics as in the original Datafriend. A
changed value is represented by retracting the old triple and adding the new
triple. All retractions in a batch precede additions, and the batch is atomic.
Do not delete an entity halfway through an attribute replacement: determine
whether it has any remaining attributes after applying the complete batch.

Canonical encodings remain to be specified. Native view entries are ordered
lexicographically by entity, then attribute; the protocol adapter must preserve
that ordering while reconciling complete triples. Triple
hashes must cover entity, attribute, and value; network record hashes must cover
entity identity and contents so an edit changes the reconciliation fingerprint.

## Authority and local edit flow

The app's view is authoritative during an app-to-Datafriend edit exchange.
Triple absence in the completed incoming view means an actual retraction from
the littlebear. There is no separate app-side transaction outbox or deletion
marker. Authority is scoped to the negotiated view, not to data outside it.

1. An interaction edits triples in the app's local view table.
2. Negentropy reconciles that table against Datafriend's copy. Transfer the
   differing triples and stage the resulting additions/retractions until the
   exchange completes; a partial exchange is not an instruction to delete data.
3. Apply the batch to the littlebear and its indexes. Rebuild each touched
   entity's network record once, and update its network reconciliation index in
   the same durable transaction. An entity with no remaining attributes has no
   network record.
4. Publish the accepted source-view state after commit. Use the batch's touched
   attributes to find affected query subscriptions, including the originating
   subscription. Deduplicate the candidate subscriptions for this batch.
5. Datafriend reevaluates affected queries and maintains their triple result
   sets. Notify view owners when their results change.
6. Owners reconcile to Datafriend's refreshed query results. Datafriend is
   authoritative for this refresh; the refreshed view becomes the agreed state
   for the next edit exchange. A refresh is not echoed back as a new edit.

For example, completing a task replaces `(task, completed, false)` with
`(task, completed, true)` in the local table. Datafriend commits those triple
changes and then recomputes the unfinished-task query. The task leaves that
query's result while remaining completed in the littlebear. Removing all of an
entity's triples from an editable view propagates those retractions; attributes
outside a projected view are unaffected.

Incoming whole-entity network changes follow the reverse path: compare each
old and incoming record, derive its triple differences, update local storage,
and invalidate relevant subscriptions. Apply these changes according to the
namespace's ownership rules; server cache loss must not erase owner data.

## Query subscriptions

The primary invalidation index maps `(namespace, attribute)` to dependent query
subscriptions. Dependencies cover filters, joins, returned attributes, and
attribute/entity existence. Queries returning unrestricted attributes require
a wildcard dependency. False-positive invalidations are acceptable; missing a
dependency would leave stale results.

The initial implementation should reevaluate changed entities for simple
entity-local filters/projections and rerun affected join queries. Incremental
join maintenance is a later optimization driven by measurements. Identical
query/parameter/namespace subscriptions can share evaluation and result indexes,
while retaining separate client synchronization state.

Each arbitrary query result needs an ordered triple index with maintained
fingerprints for reconciliation. The selected native view tree also caches
counts for rank/select; this is not a server protocol requirement. A result that corresponds exactly to an
indexed contiguous range may reuse that range's aggregate support. The main
littlebear's aggregate alone cannot describe arbitrary filtered/joined views.
Derived views may be rebuilt from durable data; their persistence policy is open.

## Persistent native view values

The preferred local transport direction is to keep view data in persistent,
immutable C++ containers. Here persistent means previous in-memory versions stay
valid after edits; durable storage remains Datafriend's responsibility.

"Snapshot" in these notes means an ordinary value of the persistent map, not
a separate abstraction, snapshot operation, full copy, or snapshot store.
For example, `b = a.set(entity, attribute, value)` leaves `a` unchanged and
shares unchanged structure with it. C++ owns allocation and memory management;
the implementation does not depend on Dart garbage collection. Dart holds an
opaque native map handle, with explicit release as the normal ownership path.

Each side owns a snapshot root. New roots share unchanged nodes and payloads
with previous versions. Dart uses a thin map-like FFI facade and sends small
view/revision/snapshot handles through its messaging boundary. Native ownership
must retain a snapshot before publication and release it after all readers and
in-flight messages finish with it. Keep UI snapshot lifetimes independent of
AELMDB transaction lifetimes.

With stable snapshots locally accessible, a worker-side session runner can drive
both negentropy endpoints in memory. This avoids a Dart event-loop round trip
for every protocol round while preserving the same reconciliation semantics.
The byte-message transport remains a separate implementation of the boundary.
New app edits create newer revisions while a prior snapshot is being reconciled;
publication and refresh rules must preserve those newer edits.

Use a purpose-built persistent ordered tree, based on ClojureDart's
`HashRankedWideTreapMap`, as the intended native view representation. The
implementation inspected is in `clj/src/cljd/core.cljd` at ClojureDart commit
`4ee792f3044589c96b019c2a86db9b23f1b283ab`, the version pinned by the reference
iCare project. See the author's
[description of the design](https://buttondown.com/tensegritics-curiosities/archive/maps-implementation-secrets/).
This replaces Immer as the proposed foundation. A mutable native benchmark port
now exists, together with persistent app-view values and blocking FFI. Preserve the
source's copyright/EPL-1.0 notices and provenance when porting.

The tree uses deterministic hash-derived ranks and variable-width nodes, with
an average fan-out of 16. Values live in leaves, internal keys separate child
ranges, and nodes already cache subtree entry counts. For canonical keys and a
fixed comparator/hash, tree shape is independent of insertion history. Shared
node identity provides an exact unchanged-subtree shortcut; separately built
snapshots can have matching structure without sharing allocations. Performance
depends on hash distribution: shallow depth alone does not bound node width or
the amount of copying per edit.

### Confirmed tree design

The following choices were explicitly confirmed for the persistent app-view
tree; the server benchmark below uses a separate mutable implementation:

- An ordered map from `(entity, attribute)` to `value`, with one value per
  entity/attribute pair. Keys sort lexicographically by entity, then attribute,
  keeping each entity's attributes contiguous.
- Deterministic ranks derived from keys only. Replacing a value changes neither
  the key's rank nor the tree shape. Exact encoding and rank-hash parameters
  remain to be specified independently of Dart's runtime object hashes.
- Immutable leaf entries and internal separator keys/child references, following
  ClojureDart's hash-ranked wide tree. Values occur only in leaves.
- A cached entry count and 64-bit XOR summary per node. Each leaf entry
  contributes `xxHash64(canonical(entity, attribute, value))`. Separator keys
  contribute nothing because their entries already occur in leaves. Internal
  summaries combine child summaries; recompute only newly constructed nodes,
  including split/join paths.
- Triple-based reconciliation despite pair-keyed map storage: replacement
  retracts the old triple and adds the new one. No deletion markers or separate
  app transaction outbox are introduced.
- Value semantics: copying a map shares its root; set/remove return a new map
  with affected paths copied and unchanged nodes/payloads shared.
- Entirely native ownership using reference counting for immutable nodes, with
  each map retaining its root and parents retaining their children. Thread-safe
  reference ownership allows separate map values to be held on different threads.
- Core operations: lookup, set, remove, ordered range iteration, selection by
  rank, and range count/XOR queries.
- Start the prototype with ordinary C++ allocation and reference counting.
  Measure before adding allocation pools or deferred reclamation; neither is
  selected as an initial implementation requirement.

The persistent implementation and FFI are now available; see the current
[native design and API](datafriend-native.md) for the final boundary.

Expose this through a C ABI with opaque retained snapshot handles, batched
edits returning a new snapshot, batched reads/range iteration, and reconciliation
operations. Dart remains the map-like facade; native node layouts stay private.
Published nodes are immutable. Native ownership replaces reliance on
ClojureDart's garbage collector. Batch construction may
mutate unpublished nodes only; published snapshots must remain readable while
other snapshots are edited.

The interactive local flow should target completion within a display frame.
Measure snapshot edits, publication, local reconciliation, query updates, result
delivery, Dart value decoding, and UI work together. Thread scheduling and
durable disk flushes have variable latency; define the relationship between
view publication and durable acknowledgments explicitly. Avoid per-triple FFI
calls and whole-view Dart materialization. Include old-snapshot reclamation in
the frame budget, and consider reclaiming large snapshots on the worker.

## Hashing and storage direction

Use **64-bit xxHash contributions combined with XOR**, retaining the backend's
existing width. This explicitly supersedes the earlier 128-bit decision. The
collision/security tradeoff is an accepted project decision; the smaller width
has a higher accidental-collision probability. Native view-tree counts use
ordinary arithmetic, but range fingerprint comparison does not require counts.
The exact xxHash
variant, seed, and canonical byte encoding still need to be fixed on both sides.
This is a custom negentropy variant; upstream wire/fingerprint compatibility is
not a requirement. Both local peers and the server need agreed encoding,
ordering, fingerprint, and protocol-version rules.

[AELMDB](https://github.com/amparore/aelmdb) is the intended storage foundation
to investigate: it maintains counts and range aggregates within its persistent
B+tree. In the inspected 0.2.0 source, `MDB_HASH_SIZE=8` configures the chosen width,
but XOR requires a maintained patch/fork of the centralized hash add/subtract
operations. Its persisted format must distinguish XOR from modular addition.
Arithmetic tests and the reconciliation adapter must reflect that distinction.

The [existing adapter](https://github.com/amparore/negentropy-aelmdb) assumes
32-byte IDs and the standard Negentropy accumulator/fingerprint scheme. It is
reference material for the custom adapter, not a drop-in implementation of the
chosen 64-bit XOR protocol.

### Stable entity keys

Use stable entity keys for whole-entity records instead of keys that change
with each update timestamp. This removes the entity-to-timestamp locator and
the delete/reinsert operation on each edit. Record-key generation remains to
be specified within the backend's unsigned 64-bit key space. No timestamp-based
conflict policy is implied; view revisions/ordering of exchanges are separate
from stored entity timestamps.

Recent edits may now be spread across the key space rather than clustered in a
recent-update suffix. This can increase reconciliation work, but timestamps are
not necessary for range reconciliation correctness. Benchmark scattered edits.

Whole-record fingerprints must cover the stable key and canonical entity
contents together; hashing payloads alone permits value swaps between keys to
leave the range XOR unchanged. The final differing-item exchange must compare
keys AND content fingerprints (or contents), not just stable-key membership,
so changes to existing entities are detected. The custom adapter also needs an
explicit unbounded range endpoint: unlike upstream's timestamp field, the
backend permits `UINT64_MAX` as an actual key.

### Preserve compact server nodes

This records the earlier binary-tree constraint. It was superseded by the
subsequent decision to implement the wide tree with a shared slab allocator.

The earlier approach kept LittleBear's 16-byte tree nodes and separate 64-bit XOR accumulator
array. Do not enlarge nodes or introduce a parallel subtree-count index to
support upstream Negentropy's count-based range splitting. Server reconciliation
must work without cached subtree entry counts; counts are not required in the
range fingerprint comparison. Lengths needed to frame explicit item lists can
be computed while assembling those bounded lists.

If retaining this tree, a splitting strategy would use ordered keys/tree
traversal rather than rank/select on the server. It must make
progress on sparse and skewed key distributions and bound work per event-loop
turn. Peers can choose different split strategies while agreeing on range
boundaries, hash encoding, and reconciliation semantics. The native persistent
view tree may retain its already selected cached counts without imposing them
on LittleBear.

The expected benefit is lower metadata memory and better cache density, at the
cost of potentially less balanced reconciliation splits or extra traversal.
Measure total reconciliation latency, bytes exchanged, and server CPU/memory
under scattered and clustered differences; a net speedup is a hypothesis, not
a measured result yet.

### Candidate: use the augmented wide tree on the server

The user proposed replacing the server's binary tree with the same hash-ranked
wide-tree design being considered for native views. This would amortize cached
counts over multiple entries and let both peers use count-based subdivision,
avoiding custom server separator-selection or client-only refinement. Assess
this option before committing to either of those reconciliation strategies.

Compare total bytes per record rather than node size: the existing external
binary tree has N leaves and N-1 internal nodes for a nonempty N-record tree.
At 16 bytes plus an 8-byte separate fingerprint per node, its logical node/hash
storage is `24 * (2*N - 1)` bytes, before allocator/page overhead. Wide leaves
would still need each record's key, packed file reference, and cached fingerprint
to avoid reading value files when hashing partial leaf ranges. Counts and node
summaries can then be shared across the leaf's entries; occupancy, allocation
overhead, and internal nodes determine actual savings.

The user selected the **mutable wide tree with the shared slab allocator** for
LittleBear, and the storage API now uses it. The value-file allocator, packed
file references and asynchronous I/O machinery are retained. Namespace headers
remain 24 bytes. The namespace directory now uses the benchmarked
`ankerl::unordered_dense` implementation, with no application namespace-count
limit. It grows on demand, reuses the low half of namespace fingerprints as its
hash, and compares both halves for equality. Counts and fingerprints are
stored in wide nodes; the separate binary node arena/hash mapping is gone from
the storage owner. The persistent app-view implementation is still separate
future work. See [the current storage contract](littlebear-storage.md).

The initial isolated prototype compared tree operations and encoded range
reconciliation without changing backend sources. The client uses cached counts in all sync
cases; server variants use wide-tree counts, binary-tree separators, or balanced
splits obtained by scanning the binary tree. Separator splitting is retained as
a benchmark baseline, not an accepted production design. Results and limitations
are documented in [the benchmark report](datafriend-tree-benchmark.md).

The next comparison adds Negentropy's own `BTreeCore` at revision
`b3687652922785c6f3b6c00684f51c6977965784`: both its native 32-byte-ID format
and a 64-bit XOR/type adaptation with the same node algorithms and occupancy
thresholds. Many-namespace cases share one binary arena or B-tree node store,
with separate map roots. The wide tree continues to use `malloc`/`free`, which
the user considers adequate given the initial measurements. These workloads
are explicitly single-threaded; there is no worker-based allocation scheme.
See [the B-tree and namespace comparison](datafriend-btree-benchmark.md).

The user subsequently set aside further B-tree work and requested an allocator
benchmark for the wide tree. The isolated candidate uses a size-class slab pool
shared across namespace roots on the same thread, preserving the tree's existing
capacity classes and algorithms. It uses 64 KiB slabs, retains one empty slab
per used class, and falls back to malloc for large nodes. Full slab capacity
must be included in memory accounting. The user subsequently chose this allocator
for the backend; the standalone generic tree still offers malloc and slab
policies. Insertion and deletion were hardened to preserve logical contents on
allocation failure before integration. The archived
[allocator comparison](datafriend-allocator-benchmark.md) measures the earlier
prototype, before that failure-handling work.

Feasibility checks performed in temporary checkouts, without integrating them
into the app:

- AELMDB 0.2.0 commit `359b89807d638e0f255e012cfe8c64dca33a0c02` compiled
  and linked unmodified for iOS arm64 and Android arm64.
- Upstream API tests passed on macOS outside the execution sandbox.
- A standalone iOS simulator test committed 100 records, reopened the database,
  and verified aggregate preservation and rank/select.
- Android runtime, sandboxed mobile-app lifecycle, the XOR modification, and
  end-to-end reconciliation have not been validated by these checks.

## Performance model and first measurements

Feasibility is supported by the bounded component design and native build
checks, but there are no measurements yet of the proposed complete system.
Performance depends on changed data, subscription fan-out, query plans, record
sizes, and how much work is batched.

| Work | Main driver | Initial approach to measure |
| --- | --- | --- |
| Local reconciliation | Changed triples, set size, protocol rounds, serialization | Maintain aggregates incrementally on both sides; measure compared ranges and bytes copied |
| Persistence | Index writes, dirty pages, durable commit latency | One commit per accepted batch; rebuild each touched entity record once |
| Invalidation | Subscriptions depending on changed attributes | Deduplicate dirty subscriptions and coalesce commits where latency permits |
| Query evaluation | Candidate entities, join cardinality, result size | Indexed entity-local updates; affected-query reruns for joins |
| View maintenance | Result changes and overlapping subscriptions | Share equivalent queries and immutable payloads where useful |
| Network synchronization | Changed entity count and whole-record size | Coalesce repeated edits to an entity before network reconciliation |

The reverse dependency lookup is unlikely to be the largest cost by itself.
An attribute with many subscribers can trigger expensive query work even when
the lookup is fast. Rebuilding every result set or rehashing every triple on each
sync would introduce work proportional to entire views. Maintained indexes are
necessary to realize the expected benefit of small differences.

Memory accounting must include source records, query indexes, the network
reconciliation index, per-view membership/aggregate indexes, app-side views,
serialization buffers, and retained snapshots. Count overlapping view entries
as well as distinct entities. AELMDB's mapped address space is not a resident
memory measurement.

Drawing and dragging are high-frequency workloads. Measure batching transient
updates over a short interval or gesture, while making the durable-commit
acknowledgment explicit. Whole-entity network records create write/transfer
amplification when a tiny change affects a very large entity; stroke granularity
and representation need particular attention.

Benchmark these workloads before selecting more elaborate optimizations:

1. Single-attribute edits at 1k, 10k, and 100k stored entities, with 1, 10, and
   100 active views; vary broad versus selective attribute dependencies.
2. Unchanged reconciliation and small edits against large views, including
   additions, removals, and updates that leave query membership unchanged.
3. Join queries with controlled fan-out, and many overlapping/equivalent views.
4. Dragging and growing strokes at 60 input updates/second, across several
   batch sizes; compare serialization, commit, and query-refresh costs.
5. Initial subscription, reconnect, and server-cache reconstruction, where the
   result or transfer may be large even if steady-state edits are small.

Report median/p95/p99 edit-to-commit and edit-to-view-refresh latency, worker CPU,
resident memory, copied bytes, protocol rounds, disk write volume, network bytes,
and queue growth. Include physical iOS and Android devices; isolate placement
protects the UI from blocking work but does not eliminate CPU or battery costs.

## Existing LittleBear storage constraints

Inspection of the current backend establishes the following implementation
constraints; these are not a finalized wire format. See
[the backend storage contract](littlebear-storage.md).

- Record keys are unsigned 64-bit integers, including zero and `UINT64_MAX`.
  Their application meaning is not fixed: the backend does not require them to
  be entity IDs or timestamps. The reference iCare uses update timestamps for
  its ordered whole-entity records and carries entity identity inside the value.
- Record values are opaque binary payloads of 1 through 65,535 bytes. The
  backend does accept arbitrary byte contents, including embedded zero bytes;
  it does not inspect attributes or impose JSON/other typed value schemas.
  The size cap applies to the entire encoded entity record, not each attribute.
- Namespace IDs in the internal table are 128-bit fingerprints. The README's
  intended 32-byte public-key namespaces are a separate wire identity; that
  mapping and ownership authentication are not implemented by the storage API.
- The current internal record fingerprints/XOR summaries are 64-bit, supplied
  by the caller. Retain this width; the caller must supply the agreed hash of
  key and contents. The storage API itself does not choose the hashing codec.
- The TCP request loop is a prototype and does not submit record storage I/O.
  Its whitespace-delimited debug commands do not define the eventual payload
  codec or a production record transfer protocol.

Consequently, attribute identifiers, typed values, and canonical entity encoding
remain Datafriend design decisions. Stable entity keys have now been selected
in place of update timestamps; exact key generation and how large drawings/
entities fit the whole-record size limit remain open.

## Remaining integration work

The earlier encoding, edit serialization, native lifetime and publication choices
are resolved in [the current contract](datafriend-native.md). Remaining work is
live server transport/authority integration, physical-device frame measurements,
and optimizations justified by those measurements. The first query evaluator is
a positive conjunctive subset, with explicit clause projections and no planner.
