import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from one_way_latency import ClockMapping, analyze


def report(base, forward=100, backward=300):
    offset = 100000000-base
    samples = []
    for elapsed in range(0, 6000000, 100000):
        sent = base+elapsed
        samples.append([sent, sent+offset+forward, sent+offset+forward+20,
                        sent+forward+20+backward])
    # Frame timestamps deliberately use a different epoch than the engine.
    frame_offset = 700000000
    def frame(elapsed, tick):
        t = (base+elapsed)*1000-frame_offset
        return [t, 0, 0, 0, 0, t, tick, frame_offset]
    return {'clock_samples': samples,
            'frames': [frame(2000000, 0), frame(2236000, 2), frame(5000000, 2)],
            'edits': [[base+2100000, 500, 1, 1, 0, 0, 0, 0, 10],
                      [base+2110000, 500, 2, 1, 0, 0, 0, 0, 11]],
            'receives': [[base+2220500, 2, 2, base+2220000]],
            'request_timings': [[1, 1, 6, 100000, 100, 11, base+2119900, base+2120000]]}


class OneWayTest(unittest.TestCase):
    def test_clock_asymmetry_bounded(self):
        r = report(10000000)
        clock = ClockMapping(r)
        t = 12200000
        self.assertLessEqual(abs(clock.host(t)-(t+90000000)), clock.uncertainty(t))
        self.assertEqual(clock.report['inconsistent_samples'], 0)

    def test_coalesced_edits_and_frame_epoch(self):
        result = analyze({'android': report(10000000), 'iphone': report(20000000)})
        for direction in result['directions'].values():
            self.assertEqual(direction['edits'], 2)
            self.assertEqual(direction['distinct_uploads'], 1)
            self.assertAlmostEqual(direction['send_to_receive_ms']['p50'], 100)
            self.assertAlmostEqual(direction['edit_to_ui_ready_ms']['p50'], 115.5)
            self.assertAlmostEqual(direction['edit_to_display_ms']['p50'], 131)
            self.assertAlmostEqual(direction['edit_to_send_ms']['p50'], 15)

    def test_linear_clock_drift(self):
        r = report(10000000)
        rate, offset = 1.00002, 90000000
        def host(t): return t*rate+offset
        r['clock_samples'] = [[s[0], host(s[0])+100, host(s[0])+120,
                               (host(s[0])+420-offset)/rate] for s in r['clock_samples']]
        clock = ClockMapping(r)
        self.assertAlmostEqual(clock.report['estimated_drift_ppm'], 20, places=5)
        self.assertLessEqual(abs(clock.host(12200000)-host(12200000)), clock.uncertainty(12200000)+1)

    def test_missing_calibration_rejected(self):
        r = report(10000000)
        r['clock_samples'] = r['clock_samples'][:10]
        with self.assertRaises(ValueError):
            ClockMapping(r)


if __name__ == '__main__':
    unittest.main()
