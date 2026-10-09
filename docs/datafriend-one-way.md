# One-way document delivery

Measured 2026-10-08 on Moto G7 and iPhone 13, through the public Littlebear server
at 147.182.181.182:7777. No server or wire changes. Workload: 1,000-character seed,
67 concurrent edits per phone, one every 150 ms.

## Results

| Interval: median / p95 | Android → iPhone | iPhone → Android |
| --- | ---: | ---: |
| Full upload handed to socket → peer Datafriend callback | **108 / 118 ms** | **107 / 129 ms** |
| Local edit begins → peer UI state published | 165 / 229 ms | 162 / 215 ms |
| Local edit begins → first presented frame using new state | **213 / 277 ms** | **215 / 268 ms** |
| Local edit begins → full upload handed to socket | 52 / 116 ms | 51 / 100 ms |
| Peer Datafriend callback → first presented frame | 49 / 49 ms | 53 / 55 ms |

Cross-phone estimates have maximum calibration uncertainty approximately
**±11.2 ms**, under the short-run affine-clock assumption below. Same-phone
intervals do not depend on cross-phone calibration. Component medians need not
add to the total median. Samples are edit-weighted: 67 edits used 66 distinct
Android uploads and 67 iPhone uploads.

The previous 300+ ms metric ends later, when the peer's benchmark acknowledgment
fact returns. In this run its medians were 338 / 318 ms. No acknowledgment is
needed to publish the received document.

All 134 edits converged and acknowledged: identical final text, no errors,
restarts, fallbacks or pending work. Both phones had full presentation coverage
at approximately 60 Hz with no gaps over 25 ms. Local edit medians were
0.868 / 0.653 ms. Both are restored to interactive mode with probes disabled.

### Where the time before and after transport goes

Derived from the same per-edit trace, without another benchmark run:

| Sender interval (median) | Android | iPhone |
| --- | ---: | ---: |
| Local edit work | 0.868 ms | 0.653 ms |
| Commit completed → upload enqueued | 48.521 ms | 49.645 ms |
| Enqueued → handed to socket | 0.265 ms | 0.317 ms |

The middle interval includes waiting for the previous owner upload, the next
pump, and batch preparation/signing. Those subcomponents were not separately
timestamped here. One outstanding upload per namespace plus a roughly 100 ms
reply time explains the dominant scheduling wait; it is not 50 ms of database
work. The framework sends on a subsequent pump rather than inside the edit.

| Receiver timing (median) | Android | iPhone |
| --- | ---: | ---: |
| Datafriend callback → merged UI state published | 0.908 ms | 0.401 ms |
| CPU time for the entire frame showing the update | 18.905 ms | 3.810 ms |
| Wall time for that entire frame callback | 20.185 ms | 16.529 ms |
| Frame callback ends → platform presentation | 34.017 ms | 32.691 ms |

Whole-frame measurements include work before receipt and overlap the merge;
these rows must not be summed. Android spends significant CPU time in update
frames, while iPhone spends much of its frame wall time waiting. The additional
33–34 ms after the callback is consistent with about two 60 Hz frames in the
presentation pipeline. These traces do not isolate rasterization, driver work,
GPU execution or compositor waiting within those intervals.

## Boundaries and correlation

- **Edit:** callback begins, before CRDT work and local commit.
- **Send:** immediately after the final upload byte is accepted by the sender's
  socket. Kernel queues, server processing and both network legs are included
  afterward; this is not a packet-capture timestamp.
- **Receive:** subscriber callback after Datafriend validates/imports facts,
  before the app merges Yrs. Includes waiting for the receiving app's pump.
- **UI ready:** Yrs merge and local state/text publication completed.
- **Display:** platform presentation timestamp for the first frame drawn using
  at least that peer edit sequence. This identifies a frame using updated state,
  not an optical measurement of a particular glyph.

Local edits record their committed engine revision; outgoing requests record
the retained snapshot revision and enqueue/send times. Analysis pairs each edit
with the first optimistic upload covering its revision and the first peer
publication/frame covering its edit sequence. Coalesced edits share an upload.
No timestamps or extra facts enter document synchronization payloads.

Each frame records the conversion from its CLOCK_MONOTONIC timestamps to the
engine's C++ steady_clock domain. A five-second recording lead-in covers the
phones' different wall-clock workload start times.

## Calibration and limits

Both apps send small, benchmark-only UDP probes to a temporary responder on the
Mac's LAN address, every 100 ms. Document traffic still uses the public server.
Probes run on the existing app context, with no engine worker or clock changes.

Samples contain phone send `t1`, Mac receive `t2`, Mac send `t3`, phone receive
`t4`. For nonnegative delays, Mac-minus-phone offset is between `t3-t4` and
`t2-t1`. The estimate is the midpoint and half the residual round trip is the
delay uncertainty. Minimum-delay samples before and after the workload anchor
a linear clock mapping. This assumes approximately affine drift during the
short run; it does not assume the paths are symmetric. Other probe intervals
must remain consistent with the fitted uncertainty bounds.

Android had 465 samples; selected residual round trips were 6.11 / 6.57 ms.
iPhone had 444 samples; selected round trips were 8.80 / 16.22 ms. No samples
contradicted the fitted bounds. Combined uncertainty at measured events was at
most 11.14 ms. Calibration traffic can influence Wi-Fi scheduling/power behavior,
so this is an instrumented active workload, not an idle-wakeup guarantee.

Initial run `9446f3ab0bf846ff91400e92edb12911` had valid delivery timings but
missed early receiver frames due to different phone wall clocks. Its display
results are explicitly marked invalid and excluded. The final run includes
receive/display samples for all 134 edits.

## Artifacts and validation

- [Final summary](benchmarks/datafriend-one-way/d394e85d6de846809fe3d24349c701d8-summary.json)
- [Per-edit timestamps and calibration](benchmarks/datafriend-one-way/d394e85d6de846809fe3d24349c701d8-one-way.json)
- Raw phone reports, source hashes and binary hashes are beside the summary.
- Five native model tests and four analysis tests pass. Analysis tests cover
  asymmetric delays, affine drift, coalesced edits/frame epochs and missing data.
- Both phone builds/installations pass. No public-server deployment was needed.

```sh
python3 demo-app/tools/sync_phones.py --host 147.182.181.182 --port 7777 \
  --case concurrent --seed 1000 --duration 25 --lead 25 \
  --clock-host 192.168.1.19 --prefix datafriend-one-way
```

The responder stops when the runner exits. Normal interactive operation leaves
clock probes and detailed request sampling disabled.
