import json
import sys
import tempfile
import time
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import AsyncMock, patch

sys.path.insert(0,str(Path(__file__).resolve().parents[2]))
from remote.companion_core import DeathJournal, DesktopBridge, decode_view
from test_companion import snapshot


class Journal(unittest.TestCase):
    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory(); self.addCleanup(self.tmp.cleanup)
        self.root=Path(self.tmp.name)
        self.j=DeathJournal(self.root,'room-one',0)
        self.event=dict(role=0,run=[1,2],participants=[10,20],ids=[1,2])
    def test_reload_and_capacity(self):
        event=dict(self.event,ids=list(range(1,89)))
        self.assertTrue(self.j.save(False,event))
        self.assertEqual(DeathJournal(self.root,'room-one',0).load(False),event)
        self.assertLess(self.j.path(False).stat().st_size,self.j.LIMIT)
        with self.assertRaises(ValueError): self.j.save(False,dict(event,ids=list(range(1,90))))
    def test_room_and_role_scope(self):
        self.j.save(False,self.event)
        self.assertIsNone(DeathJournal(self.root,'room-two',0).load(False))
        self.assertIsNone(DeathJournal(self.root,'room-one',1).load(False))
        self.assertNotIn('room-one',self.j.path(False).read_text())
    def test_interrupted_replace_keeps_committed_record(self):
        self.j.save(False,self.event)
        with patch('remote.companion_core.os.replace',side_effect=PermissionError('busy')):
            self.assertFalse(self.j.save(False,dict(self.event,ids=[1,2,3])))
        self.assertEqual(self.j.load(False),self.event)
        self.assertTrue(self.j.path(False).with_suffix('.tmp').exists())
    def test_bad_files_are_bounded_and_ignored(self):
        for data in (b'{',b'null',b'[]',b'\xff',b'x'*(self.j.LIMIT+1)):
            self.j.path(False).write_bytes(data)
            self.assertIsNone(self.j.load(False))
    def test_modified_record_and_duplicate_ids_rejected(self):
        self.j.save(False,self.event)
        record=json.loads(self.j.path(False).read_text())
        record['event']['ids']=[1,3]
        self.j.path(False).write_text(json.dumps(record))
        self.assertIsNone(self.j.load(False))
        record['event']['ids']=[1,1];record['checksum']=self.j.checksum(record['event'])
        self.j.path(False).write_text(json.dumps(record))
        self.assertIsNone(self.j.load(False))
    def test_legacy_requires_room_derived_identity(self):
        self.j.path(False).write_text(json.dumps(self.event))
        self.assertIsNone(self.j.load(False))
        event=dict(self.event,run=self.j.legacy_run,participants=self.j.legacy_participants)
        self.j.path(False).write_text(json.dumps(event))
        self.assertEqual(self.j.load(False),event)
        event['role']=1;self.j.path(False).write_text(json.dumps(event))
        self.assertIsNone(self.j.load(False))
    def test_legacy_received_record_without_role(self):
        event=dict(run=self.j.legacy_run,participants=self.j.legacy_participants,ids=[1])
        self.j.path(True).write_text(json.dumps(event))
        self.assertEqual(self.j.load(True),dict(event,role=1))


class Recovery(unittest.IsolatedAsyncioTestCase):
    async def asyncSetUp(self):
        self.tmp=tempfile.TemporaryDirectory();self.addCleanup(self.tmp.cleanup)
        self.root=Path(self.tmp.name);(self.root/'lgfr-sl.gba').write_bytes(b'ROM')
        self.net=SimpleNamespace(role=0,secret='room-one',local=None,joined=False,epoch=0,
                                 wan_send=AsyncMock(),local_send=AsyncMock())
        self.event=dict(role=0,run=[1,2],participants=[10,20],ids=[1])
        self.d=DesktopBridge(self.net,self.root)
    async def test_outage_and_launcher_restart_replay_without_new_snapshot(self):
        self.d.remember_deaths(False,self.event)
        await self.d.coordinate();self.net.wan_send.assert_not_awaited()
        for seconds in (5,30,300):
            with patch('remote.companion_core.time.monotonic',return_value=time.monotonic()+seconds):
                fresh=DesktopBridge(self.net,self.root);self.net.joined=True
                self.net.wan_send.reset_mock();await fresh.coordinate()
                self.net.wan_send.assert_awaited_once_with('death_notice',event=self.event,build=fresh.build)
                self.net.joined=False
    async def test_repeated_death_is_idempotent_and_does_not_reset_resend_timer(self):
        self.d.remember_deaths(False,self.event);self.d.last_death_sent=time.monotonic()
        before=self.d.last_death_sent
        with patch.object(self.d.journal,'save') as save:
            self.d.remember_deaths(False,self.event)
            save.assert_not_called()
        self.assertEqual(self.d.last_death_sent,before)
        self.d.remember_deaths(False,dict(self.event,ids=[2]))
        self.assertEqual(self.d.local_deaths['ids'],[1,2])
        self.assertEqual(self.d.last_death_sent,0)
    async def test_write_failure_keeps_memory_and_retries_at_bounded_rate(self):
        with patch.object(self.d.journal,'save',return_value=False) as save:
            self.d.remember_deaths(False,self.event)
            self.d.remember_deaths(False,self.event)
            self.assertEqual(save.call_count,1)
            self.assertEqual(self.d.local_deaths,self.event)
            self.assertIn('writable',self.d.note)
        self.d.journal_tried[False]-=6
        self.d.remember_deaths(False,self.event)
        self.assertFalse(self.d.journal_failed[False])
        self.assertIsNone(self.d.note)
        self.assertEqual(DesktopBridge(self.net,self.root).local_deaths,self.event)
    async def test_received_death_applies_after_restart_while_relay_is_offline(self):
        self.d.remember_deaths(True,dict(self.event,role=1))
        fresh=DesktopBridge(self.net,self.root)
        fresh.local=decode_view(snapshot());fresh.last_local_beat=time.monotonic()
        self.net.local=object()
        await fresh.coordinate()
        self.assertEqual(fresh.pending['kind'],4)
        self.net.local_send.assert_awaited_once()
    async def test_received_death_never_applies_to_another_run_or_participant(self):
        self.d.remember_deaths(True,dict(self.event,role=1))
        self.net.local=object()
        for key,value in (('run',[3,4]),('participants',[11,21])):
            fresh=DesktopBridge(self.net,self.root)
            fresh.local=decode_view(snapshot());fresh.local[key]=value
            fresh.last_local_beat=time.monotonic()
            await fresh.coordinate()
            self.net.local_send.assert_not_awaited()
    async def test_rom_update_preserves_deaths_but_room_change_does_not(self):
        self.d.remember_deaths(False,self.event)
        (self.root/'lgfr-sl.gba').write_bytes(b'updated ROM')
        self.assertEqual(DesktopBridge(self.net,self.root).local_deaths,self.event)
        self.net.secret='room-two'
        self.assertIsNone(DesktopBridge(self.net,self.root).local_deaths)


if __name__=='__main__': unittest.main()
