"""Correlate phone monotonic traces through a common LAN calibration clock."""
import statistics


def percentiles(values):
    values = sorted(values)
    return {'p50': statistics.median(values),
            'p95': values[min(len(values)-1, int(len(values)*.95))],
            'max': max(values)}


def frame_start(frame):
    return (frame[0] + (frame[7] if len(frame) > 7 else 0)) / 1000


def timed_edits(report):
    return [e for e in report['edits'] if e[0] >= frame_start(report['frames'][0])]


class ClockMapping:
    def __init__(self, report):
        start = frame_start(report['frames'][0])
        end = max([e[0] for e in timed_edits(report)] +
                  [r[0] for r in report['receives'] if r[0] >= start]) + 100000
        samples = report['clock_samples']
        def midpoint(s): return (s[0]+s[3])/2
        def delay(s): return (s[3]-s[0])-(s[2]-s[1])
        before = [s for s in samples if midpoint(s) < start]
        after = [s for s in samples if midpoint(s) > end]
        if len(before) < 5 or len(after) < 5:
            raise ValueError('Insufficient LAN clock calibration before/after edits')
        pre, post = min(before, key=delay), min(after, key=delay)
        self.x0, self.x1 = midpoint(pre), midpoint(post)
        self.y0 = ((pre[1]-pre[0])+(pre[2]-pre[3]))/2
        self.y1 = ((post[1]-post[0])+(post[2]-post[3]))/2
        self.u0, self.u1 = delay(pre)/2, delay(post)/2
        self.drift = (self.y1-self.y0)/(self.x1-self.x0)
        inconsistent = 0
        for s in samples:
            x = midpoint(s)
            if not self.x0 <= x <= self.x1:
                continue
            offset, error = self.offset(x), self.uncertainty(x)
            # Without assuming symmetric delays, the offset lies in this interval.
            low, high = s[2]-s[3], s[1]-s[0]
            inconsistent += offset+error+2 < low or offset-error-2 > high
        self.report = {'samples': len(samples), 'pre_sample': pre, 'post_sample': post,
                       'pre_rtt_ms': delay(pre)/1000, 'post_rtt_ms': delay(post)/1000,
                       'estimated_drift_ppm': self.drift*1e6,
                       'inconsistent_samples': inconsistent,
                       'assumption': 'affine relative clock drift during this short run; midpoint estimate with delay bounds'}
        if inconsistent:
            raise ValueError('Clock observations disagree with the affine calibration bounds')

    def offset(self, t):
        return self.y0 + (t-self.x0)*self.drift

    def uncertainty(self, t):
        a = (t-self.x0)/(self.x1-self.x0)
        return abs(1-a)*self.u0 + abs(a)*self.u1

    def host(self, t):
        if not self.x0 <= t <= self.x1:
            raise ValueError('Event outside calibrated clock interval')
        return t + self.offset(t)


def analyze(reports):
    clocks = {name: ClockMapping(report) for name, report in reports.items()}
    result = {'clock_calibration': {name: clock.report for name, clock in clocks.items()},
              'directions': {}}
    for sender, receiver in [('android', 'iphone'), ('iphone', 'android')]:
        source, target = reports[sender], reports[receiver]
        a, b = clocks[sender], clocks[receiver]
        uploads = sorted([r for r in source['request_timings'] if r[2] == 6], key=lambda r: r[7])
        rows = []
        for edit in timed_edits(source):
            tick, revision = edit[2], edit[8]
            upload = next(r for r in uploads if r[5] >= revision and r[6] >= edit[0])
            receive = next(r for r in target['receives'] if r[1] >= tick)
            if receive[3] < frame_start(target['frames'][0]):
                raise ValueError('Peer update arrived before frame recording; increase recording lead-in')
            frame = next(f for f in target['frames'] if f[6] >= tick and f[5] > 0)
            sent, received, published, displayed = upload[7], receive[3], receive[0], (frame[5]+frame[7])/1000
            uncertainty = max(a.uncertainty(edit[0]), a.uncertainty(sent)) + max(b.uncertainty(received), b.uncertainty(displayed))
            rows.append({'tick': tick, 'upload_request': upload[0],
                         'edit_us': edit[0], 'sent_us': sent, 'received_us': received,
                         'published_us': published, 'displayed_us': displayed,
                         'send_to_receive_ms': (b.host(received)-a.host(sent))/1000,
                         'edit_to_ui_ready_ms': (b.host(published)-a.host(edit[0]))/1000,
                         'edit_to_display_ms': (b.host(displayed)-a.host(edit[0]))/1000,
                         'edit_to_send_ms': (sent-edit[0])/1000,
                         'receive_to_display_ms': (displayed-received)/1000,
                         'clock_uncertainty_ms': uncertainty/1000})
        metrics = ['send_to_receive_ms', 'edit_to_ui_ready_ms', 'edit_to_display_ms',
                   'edit_to_send_ms', 'receive_to_display_ms']
        if any(r['send_to_receive_ms'] < -r['clock_uncertainty_ms'] for r in rows):
            raise ValueError('Negative one-way latency outside clock uncertainty')
        result['directions'][sender+'_to_'+receiver] = {
            'edits': len(rows), 'distinct_uploads': len({r['upload_request'] for r in rows}),
            **{metric: percentiles([r[metric] for r in rows]) for metric in metrics},
            'max_clock_uncertainty_ms': max(r['clock_uncertainty_ms'] for r in rows),
            'samples': rows}
    return result
