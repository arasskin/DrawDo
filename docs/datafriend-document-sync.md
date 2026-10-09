# Native document sync

This report describes the initial custom RGA implementation. The replacement
uses Yrs and one document-text fact: see [current design](datafriend-document-yrs.md).
The measurements below remain the historical baseline.

Implementation and measurements, 2026-10-08.

- Build a nonblocking native Littlebear connection, multiplexing owner and replica
  stores. Pump it on the application thread; keep engine access serialized and
  disk flushing asynchronous. No futures or engine worker.
- Keep identities and signing inside Datafriend, using Keychain/Android Keystore.
- Demo a sequence CRDT at the app layer. Each phone owns its operation namespace
  and subscribes to its peer; Littlebear remains an owner-authoritative cache.
- Measure edits, native networking work, remote apply/merge, acknowledgment
  round trips, bytes, convergence and rendered frame intervals on both phones.
- Public server testing/deployment is authorized. The old cache deployment was
  replaced and the current LBS5 endpoint is live on port 7777.

Existing task/particle stores are preserved; document and benchmark stores are separate.

## Progress

- Native nonblocking TCP client implemented in `datafriend/src/network.cpp`:
  framing, signed writes, per-channel state machines, unsolicited openings,
  revision-triggered pushes, stable snapshots, reconnect/backoff and heartbeat.
- Identity is now a public C++ API; Android demo initializes the Datafriend
  Keystore adapter. Keys never leave Datafriend's platform storage/signing path.
- Native TCP tests pass against both local Linux and the updated public server:
  paging, empty creation, two multiplexed namespaces, Unicode, notifications,
  edits during transfers, reconnect, deletion and clear.
- App-level RGA sequence implemented with immutable insert/delete operations,
  full actor keys, Lamport ordering and retained deletion targets. Shuffled and
  duplicate delivery, concurrent edits, stale editor drafts and Unicode tested.
- Both apps installed; first LAN concurrent run converged with every edit
  acknowledged. Raw results retained under `docs/benchmarks/datafriend-document-lan*`.
  Initial prototype timings exposed excessive CRDT materialization/ID copying;
  component instrumentation and compact in-memory IDs added before final runs.
- Public server replaced on port 7777. Native transport integration passed.
  Binary SHA-256: `101390a8fa951879b6a8a23bf29eac22ceeb5e005fa7bbce5854deff011ad332`.
  Release directory: `/opt/littlebear/document-demo-20261008`.
  The installed systemd unit still names `littlebear/core`; a tiny executable
  launcher at that path starts the actual `server` with explicit bind/port/cache
  arguments. No private keys were uploaded. User authorized future latest-server
  deployments without additional approval or cache backups.


## API and behavior

The portable C++ network client attaches engines and a native identity, then
advances nonblocking sockets in `pump()` on the app thread. There is one
connection per explicitly constructed endpoint session, up to 64 attached stores,
and one stable AELMDB snapshot per active store exchange. App code can read live
views and perform edits in publication callbacks. Network failures preserve local
edits; reconnect repushes owners and resubscribes replicas. Public reads and
Ed25519 write authentication match Littlebear; TCP is not encrypted.

Each device owns a namespace keyed by its secure Ed25519 public key. Immutable
CRDT operations are individual packed-EA records in that namespace; the peer's
namespace is replicated into a separate engine. The app merges both sets. Record
entity IDs remain local uint32 IDs; CRDT character IDs contain a Lamport counter
and the full 32-byte actor key. The demo uses opaque `points` values for operations
and entity-zero `id` for acknowledgments, inside separate document-only stores.
These fields have no special semantics inside Datafriend or Littlebear.

The CRDT orders concurrent sibling inserts deterministically and retains deletion
targets. It handles duplicate/out-of-order operations and edits based on stale
visible character IDs. Text is UTF-8 and operations address Unicode scalars, not
user-perceived grapheme clusters. It is intentionally a small plain-text demo:
no rich-text formatting, shared cursors, history compaction or production CRDT
library integration. The native multiline editor retains a local draft while the
shared preview updates from both phones; it does not transform its caret for
remote edits. No server-side multi-writer container is required.

## Reproduce

```sh
make -C datafriend test
make -C demo-app test
python3 scripts/test_native_sync.py littlebear/build/server \
  datafriend/build/native/transport_test demo-app/build/tests/document_network_test
make -C demo-app android-run
make -C demo-app ios-run
python3 demo-app/tools/sync_phones.py --host 147.182.181.182 --port 7777 \
  --case concurrent --seed 1000 --duration 25
# Leave both devices on a paired interactive document:
python3 demo-app/tools/sync_phones.py --host 147.182.181.182 --port 7777 \
  --case interactive --seed 0
```

The protocol test runner requires a Linux server binary. On macOS use
`scripts/littlebear_linux_test_server.py` and `DATAFRIEND_LINUX_SERVER` for the
container-backed server. The deployment CI now includes the native TCP client.
Only public identity keys, configuration and synthetic reports are copied by the
phone runner. Each benchmark uses a unique document room and stores; it preserves
normal task data and the interactive document. Repeated runs retain their raw
reports instead of overwriting a summary with only successful attempts.

## Measurement definition

A 1,000-character initial document is synchronized before the timed phase.
Scripted changes run every 150 ms for 10 seconds: six middle insertions followed
by one deletion, repeated (67 edits per active phone). `typing` has one writer;
`concurrent` has both; `offline` disconnects both at 2 seconds and reconnects at
7 seconds while edits continue. Additional recording time lets transfers settle.
Inputs are scripted app-thread calls, not hardware/keyboard delivery latency.

Edit timing includes diff generation, entity allocation, Datafriend commit and
local CRDT materialization. Return-acknowledgment latency runs from the local
edit's monotonic timestamp until the peer has merged it, published an acknowledgment
in its own namespace, and that acknowledgment has reconciled back. This includes
**both directions** and four namespace sync legs, coalescing and protocol waiting;
it is not a measurement of one-way display latency. No comparison assumes that
phone clocks are synchronized. Wall clocks only coordinate the workload start.

Frames record app-thread CPU/work, wall duration and actual EGL/Metal presentation
times. Connection counters include initial sync, the timed phase and settlement;
they are not exclusively steady-state per-keystroke bytes. One run per reported
case/device is exploratory evidence, not a controlled statistical comparison.


## Results

Measured builds: Moto G7 (Android 10) and iPhone 13 (iOS 26.5), 1,000 initial
characters, 67 edits per active writer. Public server: `147.182.181.182:7777`,
binary `9436bdfe52e43ef01de2855df8d81b6ac506661892e1d67484814b2c0f86fb76`.
Server sockets now use TCP_NODELAY; the client already did. A development-machine
heartbeat to the public server took about 110 ms. That is a round trip from the
computer, not a measured phone-to-phone delay.

Median / p95:

| Workload | Android local edit | iPhone local edit | Android return acknowledgment | iPhone return acknowledgment |
| --- | ---: | ---: | ---: | ---: |
| Public, concurrent | 7.14 / 7.78 ms | 2.43 / 2.77 ms | 1.68 / 2.60 s | 1.64 / 2.56 s |
| Public, Android typing | 6.90 / 7.62 ms | no local edits | 1.26 / 1.48 s | no local edits |
| LAN, concurrent | 7.01 / 8.22 ms | 2.35 / 2.62 ms | 0.50 / 0.79 s | 0.54 / 0.90 s |

All three runs converged to identical text and operation counts with every edit
acknowledged, no pending acknowledgments, no final network error and settled
connections. Each presented at approximately 60 Hz with 100% presentation
coverage and no measured display gaps over 25 ms.

Public offline/reconnect also converged with all 134 edits acknowledged. Both
phones deliberately disconnected for five seconds while editing; their median
acknowledgments were 3.78 / 3.94 seconds and maxima 7.62 / 7.83 seconds, including
time offline. Presentation coverage was complete; Android had one >25 ms gap,
iPhone none. This validates catch-up, not subsecond offline recovery.

The public concurrent local-edit median components were:

| Component | Android | iPhone |
| --- | ---: | ---: |
| Text diff → CRDT operations | 1.44 ms | 0.44 ms |
| Reserve entity IDs / encode operations | 0.34 ms | 0.68 ms |
| Datafriend commit and view publication | 2.11 ms | 0.24 ms |
| Local CRDT merge/materialization | 3.18 ms | 1.01 ms |

Component medians do not necessarily sum to the median total. The first prototype
used heap strings for full actor IDs and took about 11–12 ms per Android edit.
Keeping full actor IDs inline reduced this to about 7 ms; a cached text/order
representation avoids rebuilding on reads. Materialization remains O(document
size), and this demo is not intended as a tuned CRDT implementation.

The TCP_NODELAY public run improved the return-acknowledgment median from the
preceding run's 2.85 / 2.67 seconds to 1.68 / 1.64 seconds. These are sequential
exploratory runs; they do not isolate that setting from network variability.
The remaining delay is consistent with repeated reconciliation/fetch round trips,
frame-driven socket servicing and coalescing behind active exchanges. Native
signing/reconciliation/apply CPU is measured in milliseconds, while request waits
accumulate over seconds. A per-request trace would be needed to apportion the
remaining waiting time precisely.

**This demonstrates correct cross-phone synchronization, not Google Docs-class
collaboration latency.** The next performance experiment should compare the
current reconciliation-on-every-edit path with a small signed-delta path for
already-connected peers, retaining reconciliation for initial sync/recovery.
Moving CRDT merging into Littlebear is not indicated by these measurements.

One typing run had zero iPhone drawable presentations following an automated
relaunch. Its raw data is retained and excluded from rendering conclusions.
Explicitly terminating the old process, allowing the scene to settle, and then
launching again produced a complete repeated run. This mitigates the test-runner
issue; it does not establish the underlying iOS lifecycle cause as fixed.

[Selected raw-run index](benchmarks/datafriend-document-sync-summary.json) links
to complete summaries and device reports. Earlier prototype runs remain under
`datafriend-document-lan`, `datafriend-document-lan-cached` and
`datafriend-document-public`; final runs use the `*-final` directories.

## Validation and remaining limits

- Five core and four demo correctness tests pass.
- Native transport and the two-model document integration pass against the real
  Linux server; [repeatable report](validation/datafriend-native-document-sync.json).
- The production server's auth/framing/replay/subscription/disconnect suite passes
  after TCP_NODELAY. Native transport passes Linux ASan/UBSan with leak detection.
  The installed macOS ASan runtime deadlocked before `main`; Linux supplied the
  sanitizer validation instead.
- Persistent phone identities survived APK/app upgrades and repeated launches.
- Initial-document sync, connection loss, concurrent edits and Unicode are tested;
  this is not an audited CRDT or a production mobile lifecycle/presence system.
- Numeric IP endpoints only; transport encryption, background mobile networking,
  arbitrary document discovery/invitations and operation compaction remain outside
  this prototype. Foreground socket progress runs on the serialized app context.
- No current build or deployment blocker. The remaining issues are measured sync
  latency and the intermittent automated iPhone relaunch/presentation problem.


A final real Android keyboard check found a bridge bug that scripted model edits
could not expose: copying the `std::function` for each live notification reset its
mutable draft-ID capture, duplicating text. Both native text bridges now retain
one shared callable across frame deliveries, while a local shared handle protects
it from cancellation during delivery. The scripted benchmark path did not use the
keyboard bridge, so its timings/convergence results are unaffected. The corrected
bridge is included in the final installed apps.

The corrected Android bridge passed three separate keyboard deliveries producing
exactly `Live sync works.` (16 characters and 16 insert operations), with no
duplicates. [Screenshot](validation/datafriend-document-keyboard.png). Both phones
are left on the public-server room `keyboard-verified-demo`; earlier rooms and
normal task stores are preserved. The iPhone bridge has the same fix and is built,
signed and installed; its real keyboard path was not driven by automation.
