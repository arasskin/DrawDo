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

## Littlebear's current state

The network loop parses and echoes `add` and `retract` requests, but does not yet
connect them to the namespace table, tree, or value storage. Record persistence,
recovery, reconciliation requests, authentication, and subscriptions remain
unfinished. There is no automated test suite in the imported project.

Confirmed component failures in the source baseline:

- A 64-byte allocation request produces a 32-byte slot; 128 produces 64.
- Tree-arena and fingerprint-array guard-page addresses are not page-aligned,
  causing `mprotect` to fail in local component checks.
- The namespace-table header still references `zip_zip` instead of
  `augmented_tree`, so compiling that header fails.

Inspection also found missing TCP framing, partial-send handling, and graceful
connection-pool exhaustion. The README includes design goals and historical
concurrency descriptions; it should not be read as a list of working features.

The full server has not been run during this import: it requires Linux and
`liburing`, and the local Docker daemon was unavailable.

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

1. Add regression checks and repair the known littlebear component failures.
2. Validate tree insert/update/delete and range operations against a simple
   reference model, including randomized sequences and sanitizers.
3. Define namespace ownership, public/private collections, record encoding,
   versions, deletion semantics, and reconciliation consistency.
4. Decide whether littlebear remains a reconstructable cache or gains durable
   storage. Client persistence and restart recovery must work in either model.
5. Establish upstream-compatible reconciliation and record transfer in the
   backend and mobile native bindings.
6. Build a Kotlin client slice on both platforms: drawing and dragging, local
   save/reload, and reconciliation with the backend.

The existing iCare physics, drawing behavior, task model, and notification
behavior are available under `reference/icare/`. They are behavior references,
not requirements to reproduce the old internal architecture or its bugs.
