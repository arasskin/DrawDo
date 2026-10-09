# Synchronization without edit acknowledgments

Implemented, deployed and measured on Moto G7 / iPhone 13, 2026-10-08.

Successful optimistic uploads have no success response. The next frame's changes
can be queued immediately, using the same challenge and increasing signed sequence
numbers. Local output is bounded; socket backpressure coalesces later changes in
the journal. Disconnects reconcile the current database when reconnected.

The document demo no longer writes receipt facts, subscribes to them, tracks pending
receipts, or reports acknowledgment round trips. It publishes merged state/text
only when Yrs changes. Old local receipt facts are removed on open; schema slot 36
remains reserved for existing stores. No document state is discarded.

Authentication, reconciliation pages and connection-liveness exchanges retain
responses. A fingerprint mismatch sends one unsolicited recovery event (status 6).
The server consumes signature sequences but discards later queued deltas until a
fresh start. TCP ordering makes that start a barrier; reconciliation then uses the
latest local state. Invalid signed writes emit a rejection and trigger reconnect.
`settled()` means local output drained and no pending reconciliation, not receipt
or durability confirmation. Upload timing records finish at socket handoff;
operation 6 has no RTT statistic.

Removing replies exposed a server scheduling dependency: an earlier subscriber
slot could wait until the 100 ms idle timeout after a later owner slot committed.
The loop now schedules subscribers again whenever a pass changes namespaces,
before waiting for I/O. A real TCP regression checks this slot ordering.

## Phone measurements

Public server at 147.182.181.182:7777; 1,000-character seed, 67 concurrent edits
per phone, one every 150 ms. Median / p95:

| Interval | Android → iPhone | iPhone → Android |
| --- | ---: | ---: |
| Edit starts → upload handed to socket | 23.7 / 30.9 ms | 17.4 / 17.7 ms |
| Socket handoff → peer Datafriend callback | 105.2 / 128.0 ms | 108.2 / 122.2 ms |
| Edit starts → peer UI state published | 130.7 / 145.2 ms | 125.9 / 140.6 ms |
| Edit starts → first presented updated frame | **178.7 / 193.6 ms** | **179.5 / 193.3 ms** |
| Peer callback → presentation | 48.6 / 48.8 ms | 53.8 / 55.0 ms |

The previous build's edit-to-send medians were 51.5 / 50.7 ms, and its
edit-to-display medians were 212.9 / 214.9 ms. These are separate runs, not a
controlled estimate of every subcomponent. Cross-phone estimates have up to
±12.0 ms calibration uncertainty; same-phone intervals need no clock mapping.
Component medians should not be summed.

All 134 edits converged to identical text and operation counts, with no errors or
normal-run fallbacks. Both phones had full presentation coverage at about 60 Hz
and zero gaps over 25 ms. Local edit medians were 0.898 ms Android / 0.690 ms iPhone.
There were 126 / 127 uploads: these include republishing merged CRDT state as well
as local edits. They are not receipt messages. Faster sending can expose more
intermediate states instead of coalescing them behind a round trip.

## Rendering and presentation

Receiving the update makes the new text available in memory. The app still needs
to prepare text/drawing commands, submit GPU work, and have the resulting frame
presented by the display system. Frame buffering and synchronization with display
refresh can add waiting even after the app's frame callback has finished.

| Receiver interval, median | Android | iPhone |
| --- | ---: | ---: |
| Datafriend callback → merged UI state published | 0.682 ms | 0.383 ms |
| CPU time for entire update frame | 17.524 ms | 3.691 ms |
| Wall time for entire update frame callback | 18.945 ms | 16.560 ms |
| Frame callback ends → platform presentation | 35.812 ms | 32.702 ms |

Whole-frame measurements overlap the receive/merge work; do not add these rows.
At 60 Hz a refresh interval is 16.7 ms. The final 33–36 ms is consistent with about
two refresh intervals in the presentation pipeline. This trace does not isolate
GPU execution from driver/compositor waiting, nor establish an exact buffering
policy. Presentation is the EGL/Metal timestamp of the first frame using the new
state, not an optical measurement of a glyph appearing on the panel.

## Verification and artifacts

- Five core, five demo/model and four clock-analysis tests pass.
- Real native TCP tests cover silent uploads, send-only timing, pipelined owner
  mismatch recovery, replica mismatch recovery, large batches, reconnect,
  concurrent offline CRDT changes, stale drafts and deletion.
- Production protocol/auth tests pass, including no successful upload response,
  replay/tamper rejection, one recovery event for queued writes, and immediate
  subscriber scheduling. Server and native interoperability also pass ASan/UBSan.
- [Final phone summary](benchmarks/datafriend-noack/2f558819697a4593a31de67390823e8a-summary.json)
  and [per-edit timings/calibration](benchmarks/datafriend-noack/2f558819697a4593a31de67390823e8a-one-way.json).
- [Native validation](validation/datafriend-noack-native.json),
  [protocol checks](validation/datafriend-noack-server.json),
  [sanitizer interoperability](validation/datafriend-noack-asan.json).

The [five-second-offline phone run](benchmarks/datafriend-noack/2c322665a1564d958d6d7aae77264b15-summary.json)
also converged all 134 edits after reconnecting, with no errors and full
presentation coverage. Both phones are restored to interactive mode with
calibration and detailed timing disabled.

Active public release: `d891d8b86795aafed8162f71644356c67e040b4e-20261008-5`,
working-tree build with binary SHA256
`e04a21e1c726f3bb512b44d0e46ce8f457cfd251efb428c5be0b0230ec6ab568`.
Earlier runs `11c560...` and `57e50c...` are retained as diagnostic data from before
the subscriber scheduling fix, with their server versions annotated.
