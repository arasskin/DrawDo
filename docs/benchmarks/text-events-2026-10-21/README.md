# Event scheduling evidence

All source paths are relative to the project root. Collections use the physical
Moto G7 and iPhone 13, 60 updates per device, 1,000-byte documents, 503 ms apart,
with the keyboard closed. Android targets API 29 in both modes.

- `poll-1`, `poll-2`: `--timing --frame-poll`.
- `events-1`, `events-2`, `events-3`: `--timing`; event mode.
- `events-network`: event mode, real paired synchronization through Littlebear.

Run order: events-1, events-2, events-network, poll-1, poll-2, events-3.
No installation overlapped a timed workload. A host-only XCTest driver rebuild
occurred during poll-2; it did not install or launch anything on either phone.

`events-2` iPhone evidence is invalid: its trace was stale and capture log empty.
Do not include that iPhone trace in any aggregate. The Android portion is valid.
The benchmark now removes its previous event trace at startup. The failure is
retained rather than silently discarded. Its cause was not established.

Only timing traces, test logs, and counter crops are archived here; app databases,
identities, public-key files, and peer configuration are excluded.

`stage-counts.json` records app processing (50), Sokol frames (10), producer ready
(9), request (3), assignment (5), and lifecycle suspend/resume (60/61).
Android presentation matches must be unique. iPhone captures are OCR-confirmed
observations with recording overhead, not physical panel timestamps.
