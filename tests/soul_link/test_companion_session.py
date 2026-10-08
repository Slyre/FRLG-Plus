import json
import tempfile
import unittest
from pathlib import Path
from remote.companion_core import ActivityLog

class SessionSummaryTests(unittest.TestCase):
    def test_counts_only_new_records_and_does_not_load_old_totals(self):
        with tempfile.TemporaryDirectory() as root:
            log=ActivityLog(root,'room',0)
            log.add('capture','one',species=7)
            log.add('capture','one',species=7)
            log.add('death','one',species=7)
            log.add('team-applied','one',ids=[1])
            self.assertEqual(log.session_counts['capture'],1)
            self.assertEqual(log.session_counts['death'],1)
            self.assertEqual(log.session_counts['team-applied'],1)
            log.save()
            restored=ActivityLog(root,'room',0)
            self.assertEqual(sum(restored.session_counts.values()),0)
            restored.add('capture','one',species=7)
            self.assertEqual(restored.session_counts['capture'],0)
            self.assertNotIn('session_counts',json.loads(Path(root,'duo-history.json').read_text()))
    def test_new_run_resets_session_without_carrying_another_duo(self):
        with tempfile.TemporaryDirectory() as root:
            log=ActivityLog(root,'room',0)
            log.observe(dict(run=[1,2],participants=[10,20],pairs=[]))
            log.add('capture','one',species=7)
            log.observe(dict(run=[3,4],participants=[30,40],pairs=[]))
            self.assertEqual(sum(log.session_counts.values()),0)
