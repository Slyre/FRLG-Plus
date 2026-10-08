import sys, unittest
from pathlib import Path
from unittest.mock import patch
sys.path.insert(0,str(Path(__file__).resolve().parents[2]))
from remote.companion_core import InternetMetrics

class InternetMetricsTest(unittest.TestCase):
    def test_sample_age_and_disconnect_invalidation_keep_historical_values(self):
        from unittest.mock import patch
        m=InternetMetrics()
        with patch('remote.companion_core.time.monotonic',return_value=10):m.add('relayRoundTrip',23)
        with patch('remote.companion_core.time.monotonic',return_value=12):self.assertEqual(m.report()['timings']['relayRoundTrip']['ageMs'],2000)
        m.invalidate('relayRoundTrip');r=m.report()['timings']['relayRoundTrip']
        self.assertIsNone(r['ageMs']);self.assertEqual(r['lastMs'],23)

    def test_bounded_percentiles(self):
        metrics=InternetMetrics()
        for value in range(200): metrics.add('relayRoundTrip',value)
        report=metrics.report()['timings']['relayRoundTrip']
        self.assertEqual(report['samples'],128)
        self.assertEqual(report['lastMs'],199)
        self.assertEqual(report['p95Ms'],193)
    def test_monotonic_timer_and_no_duplicate_completion(self):
        metrics=InternetMetrics()
        with patch('remote.companion_core.time.monotonic',side_effect=[10,10.125,10.125]):
            metrics.mark('transferToFinish');metrics.finish('transferToFinish')
        metrics.finish('transferToFinish')
        self.assertEqual(metrics.report()['timings']['transferToFinish']['lastMs'],125)
        self.assertEqual(metrics.report()['timings']['transferToFinish']['samples'],1)
        self.assertEqual(metrics.report()['pendingMs'],{})

if __name__=='__main__': unittest.main()
