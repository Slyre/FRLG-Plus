import json
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch
from remote.companion_core import ActivityLog

def view(state=1, species=1, pair_id=1):
    return dict(run=[1,2],participants=[10,20],pairs=[dict(id=pair_id,area=87,state=2 if state==1 else 3,
        ordinal=[1,1],encounters=[state,1],species=[species,7])])

class ActivityTests(unittest.TestCase):
    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory();self.root=Path(self.tmp.name);self.log=ActivityLog(self.root,'private-code',0)
    def tearDown(self):self.tmp.cleanup()
    def test_replays_promotion_restart_and_deaths_do_not_duplicate_or_revive(self):
        self.log.observe(view(pair_id=0));self.log.observe(view());self.log.observe(view())
        self.assertEqual([e['kind'] for e in self.log.events],['capture','capture','pair-ready'])
        self.log.save();again=ActivityLog(self.root,'private-code',0);again.observe(view())
        self.assertEqual(len(again.events),3)
        again.observe(view(species=2));again.observe(view(state=3,species=2));again.observe(view())
        self.assertEqual([e['kind'] for e in again.events][-2:],['evolution','death'])
        self.assertNotIn('private-code',(self.root/'duo-history.json').read_text())
    def test_room_and_run_scope_and_limits(self):
        self.log.observe(view());self.log.save()
        self.assertEqual(ActivityLog(self.root,'other-room',0).events,[])
        changed=view();changed['run']=[3,4];self.log.observe(changed)
        self.assertEqual(len(self.log.events),3)
        for i in range(200):self.log.add('team-proposed',str(i),ids=[1])
        self.assertEqual(len(self.log.events),128)
    def test_failed_write_is_nonfatal_and_keeps_previous_file(self):
        self.log.observe(view());self.log.save();old=self.log.path.read_bytes()
        self.log.add('team-approved','new',ids=[1]);self.log.retry=0
        with patch('remote.companion_core.os.replace',side_effect=OSError):self.log.save()
        self.assertTrue(self.log.dirty);self.assertEqual(self.log.path.read_bytes(),old)
        self.log.retry=0;self.log.save();self.assertFalse(self.log.dirty)
    def test_corrupt_and_oversized_history_is_ignored(self):
        self.log.path.write_text('{');self.assertEqual(ActivityLog(self.root,'private-code',0).events,[])
        self.log.path.write_text('x'*65537);self.assertEqual(ActivityLog(self.root,'private-code',0).events,[])

if __name__=='__main__':unittest.main()
