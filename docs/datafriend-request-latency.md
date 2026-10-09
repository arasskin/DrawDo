# Public-server request latency

Measured 2026-10-08 against the existing server at 147.182.181.182:7777.
No server binary, protocol or deployment changes were needed. The existing
read-only heartbeat (operation 7) immediately returns a fixed response without
database access or signing. TCP connections stay open and use TCP_NODELAY.

## Independent baselines

| Test | Samples | Median | p95 |
| --- | ---: | ---: | ---: |
| Same heartbeat over server loopback | 100 | 0.064 ms | 0.317 ms |
| Mac to public server, blocking socket, no frame loop | 100 | 110.38 ms | 126.91 ms |
| Android app, same connection as sync | 186 | 100.31 ms | 199.86 ms |
| iPhone app, same connection as sync | 175 | 100.21 ms | 116.93 ms |

Android's independent ICMP ping had 98.16 ms **mean**, 95.85 ms minimum,
111.30 ms maximum and no loss over 20 packets. These are different probes and
schedules, so subtracting their summary statistics is not an exact attribution.
The server loopback number includes Python/socket/OS overhead, not just server CPU.
An earlier exploratory loopback sample measured 0.046 ms median; the retained
100-sample run above measured 0.064 ms.

The roughly 100 ms baseline is present without Datafriend queries, file I/O or
the UI frame loop. This establishes that the public path is the main cost for
these small requests. It does not identify geographic distance, Wi-Fi, ISP
routing or other individual network contributions.

## Phone request types

The same concurrent document case used a 1,000-character seed and 67 timed edits
per phone. Optional diagnostic heartbeats ran every 250 ms over each app's
existing multiplexed connection. Request samples also cover initialization and
settlement. Added probes can affect scheduling/connection activity, so this is a
diagnostic run rather than a controlled speed comparison to the earlier run.

| Request | Android median / p95 | iPhone median / p95 |
| --- | ---: | ---: |
| Start | 100.48 / 109.96 ms | 100.07 / 116.81 ms |
| Reconcile | 100.74 / 110.66 ms | 100.09 / 133.36 ms |
| Fetch records | 100.03 / 109.42 ms | 99.90 / 117.14 ms |
| Signed apply | 99.34 / 132.84 ms | 99.79 / 133.18 ms |

All elapsed timers start after encoding/signing, at enqueue, and stop when the
complete reply is parsed, before its facts are applied. Client queue timers stop
after the full request is handed to the socket; kernel/network queues remain
inside the rest of the elapsed time. Median queue times across request types
were 0.16–0.34 ms. Occasional queue delays reached 22.3 ms. Android heartbeat
tails reached 300.5 ms; no packet-level capture was taken to attribute those tails.

Storage requests have similar medians to the no-storage heartbeat. We have not
separately timed server file reads/writes, and should not label all elapsed time
as pure network RTT. The app still processes replies once per frame.

Both devices converged, acknowledged all 134 edits, had no sync restarts/errors,
and achieved full presentation coverage with no >25 ms display gaps. Local edit
medians were 0.904 / 0.805 ms (Android/iPhone); acknowledgment medians were
982.93 / 951.96 ms. The sequential protocol exchanges remain the main target for
reducing acknowledgment latency. One-way remote presentation is still unmeasured.

## Reproduction and artifacts

- [Phone summary](benchmarks/datafriend-request-latency/ec74d07a494a41dd98759c487f90df9a-summary.json)
- [Mac heartbeat](benchmarks/datafriend-request-latency/mac-heartbeat.json)
- [Server loopback heartbeat](benchmarks/datafriend-request-latency/server-loopback-heartbeat.json)
- [Android ICMP ping](benchmarks/datafriend-request-latency/android-ping.txt)
- Raw phone traces and source/binary hashes are beside the summary.

`scripts/benchmark_littlebear_latency.py --host 147.182.181.182` runs the blocking
heartbeat probe. On the deployed server use `--host 127.0.0.1 --schema-file
/opt/littlebear/current/SCHEMA`. Phone command:

```sh
python3 demo-app/tools/sync_phones.py --host 147.182.181.182 --port 7777 \
  --case concurrent --seed 1000 --duration 25 --lead 25 \
  --probe-interval-ms 250 --prefix datafriend-request-latency
```

`network::measure_requests(interval)` enables bounded diagnostics (at most 4096
samples); a positive interval also changes heartbeat frequency. Normal operation
does not collect samples or change its 10-second heartbeat interval. Raw
`request_timings` entries are `[request_id, channel, operation, elapsed_us,
queue_us]`. Existing aggregate `request_us/request_count` still exclude heartbeats.

Both phone builds and five native demo tests pass. Phones are restored to the
interactive `yrs-document-demo` room with diagnostics disabled.
