# Native text latency probe

Opt-in diagnostics for the two phones used during development. Normal builds do not
link the timing recorder or ReplayKit. The timing build uses its own identity alias,
`timing/` stores and peer configuration; it does not edit the regular demo document.

The workload replaces a 1,000-byte ASCII document 60 times per device, at 503 ms
intervals, in separate sending phases. The changing `LBxxxxxx` first line identifies
each update. Both apps are foregrounded with the keyboard closed. Android starts at
20 seconds, iPhone at 60 seconds; results are saved at 100 seconds.

`events.csv` uses the receiving device's monotonic clock, starting at the Datafriend
watch callback, after the network payload has been imported. It excludes socket
arrival-to-import time and waiting for the frame that pumps the network. Stages:

1. Receive callback entry.
2. CRDT merge and local publish complete.
3. App requests the native editor update.
4. Native editor begins processing the request.
5. Native text/selection assignment complete.
6. Android `onDraw` complete / iOS `layoutSubviews` complete.
7. Android frame-commit callback (not display presentation).
8. Rejected editor update.

Local producer tests also record stage 9 (producer-ready), stage 10 (Sokol frame),
and stage 50 (app processing). Build both phones with `--timing --frame-poll` for
the polling control, or `--timing` for event wakeups. Keep Android at API 29 in
both variants. Use `schedule_run.py` and `schedule_analyze.py` with the same
60-update role-3 workload. Stage 50 counts let us verify that event mode does not
silently continue processing every frame. Display endpoints remain as described
below; earlier processing alone does not establish earlier presentation.
Stages 60/61 record suspend/resume. Each run removes the old event trace at
startup; check for fresh traces, all 60 updates, and capture matches before
accepting a result. An XCTest success alone does not establish a valid capture.

Android's native editor lives in a PopupWindow, separate from Sokol's background
surface. The collector polls that window's SurfaceFlinger history. Analysis matches
each tagged draw to a single submitted buffer within one refresh interval, then
uses that buffer's actual-presentation timestamp. Raw candidate counts are retained.

On iPhone, ReplayKit captures the app. Only a small crop containing the benchmark's
first line is saved when its pixels change; audio samples are discarded. Offline
Vision OCR identifies the update. We report arrival of the first captured buffer
containing it, using the same monotonic clock as the receive callback. This includes
capture delivery delay and is **not a physical display-presentation measurement**.
The raw video PTS has a different clock epoch and is not subtracted from event times.
Capture adds overhead, so the two devices' final-stage measurements are not equivalent.

Build and pair both apps with `--timing` using the usual scripts in `../tools`.
To prepare the UI driver (XcodeGen required):

```sh
mkdir -p /tmp/crdt-timing-driver
cp crdt-demo-app/bench/ios/* /tmp/crdt-timing-driver/
xcodegen generate --spec /tmp/crdt-timing-driver/project.yml
xcodebuild build-for-testing \
  -project /tmp/crdt-timing-driver/CRDTTimingTest.xcodeproj \
  -scheme CRDTTimingTest -destination 'id=00008110-000A7D681490401E' \
  -derivedDataPath /tmp/crdt-timing-driver/build \
  DEVELOPMENT_TEAM=466U33S75F -allowProvisioningUpdates
python3 crdt-demo-app/bench/run.py --driver /tmp/crdt-timing-driver --output /tmp/crdt-timing-results
swiftc crdt-demo-app/bench/read_frames.swift -o /tmp/crdt-read-frames
/tmp/crdt-read-frames /tmp/crdt-timing-results/ios > /tmp/crdt-timing-results/ios-tags.csv
python3 crdt-demo-app/bench/analyze.py /tmp/crdt-timing-results
```

The driver accepts the app's recording prompt and explicitly activates it again.
Check for all 60 receives and all 60 frame matches on each device; a successful UI
test alone does not establish that the timing run was valid. P95 is nearest-rank.
The collector currently names the development phones, SDK location and signing team
explicitly. Keep raw captures local; only benchmark traces/crops belong in reports.

After collection, rebuild/install both apps **without `--timing`** to restore their
normal stores and remove recording/timing code from the installed binaries.

For native adapter regression checks, set `timing/run.txt` to `2` and relaunch a
timing build. It skips the app's network/model step, runs the real native adapter
through Unicode replacements, empty/unchanged text and selection updates, and
checks stale-revision rejection. Read `timing/native-check.txt` for the result.
These checks do not publish test text. Late native revisions are retried using the
same policy as the app; the result includes the retry count.

For controlled native-producer timing and a native Flutter release comparison,
see [the Flutter benchmark](flutter/README.md). Timing role 3 runs that local
workload without pumping the model/network. The diagnostic Android build now
records vsync, pre-draw and replacement boundaries, and the recorder reports
65,536-event capacity overflow. These diagnostics are absent from normal builds.
