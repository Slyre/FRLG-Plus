import asyncio, tempfile, unittest
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import AsyncMock
from remote.companion_core import ActivityLog, DesktopBridge

class AuditHistoryTests(unittest.TestCase):
    def test_nonobject_history_is_optional(self):
        with tempfile.TemporaryDirectory() as folder:
            for text in ('[]','null','42','"text"'):
                Path(folder,'duo-history.json').write_text(text)
                self.assertEqual(ActivityLog(folder,'room',0).events,[])
    def test_owner_species_wins_and_terminal_death_still_propagates(self):
        def view(player,species,encounter=1):
            return dict(player=player,run=[1,2],participants=[10,20],pairs=[dict(id=1,area=8,state=2,ordinal=[1,1],encounters=[encounter,1],species=[species,7])])
        with tempfile.TemporaryDirectory() as folder:
            log=ActivityLog(folder,'room',0)
            log.observe(view(0,19));log.observe(view(0,20));log.observe(view(1,19))
            self.assertEqual(log.session_counts['evolution'],1)
            self.assertEqual(log.seen['0:1'],[1,20])
            log.observe(view(1,19,3));log.observe(view(0,20))
            self.assertEqual(log.seen['0:1'],[3,20])
            self.assertEqual(log.session_counts['death'],1)

class AuditTransportTests(unittest.IsolatedAsyncioTestCase):
    async def test_coordinator_survives_disconnect_and_retries_record(self):
        with tempfile.TemporaryDirectory() as folder:
            Path(folder,'lgfr-sl.gba').write_bytes(b'test')
            recovered=asyncio.Event();calls=[]
            async def send(kind,**payload):
                calls.append((kind,payload))
                if len(calls)==1:raise ConnectionResetError('reset during death replay')
                recovered.set()
            net=SimpleNamespace(secret='room',role=0,local=None,joined=True,epoch=0,wan_send=send)
            bridge=DesktopBridge(net,folder)
            record={'ids':[1],'role':0,'run':[1,2],'participants':[10,20]};bridge.remember_deaths(False,record)
            bridge.write_state=lambda:None
            task=asyncio.create_task(bridge.run())
            try:
                await asyncio.wait_for(recovered.wait(),2)
                self.assertFalse(task.done());self.assertGreaterEqual(len(calls),2)
                self.assertEqual([kind for kind,_ in calls],['death_notice','death_notice'])
                self.assertEqual(calls[1][1]['event'],record)
                self.assertEqual(bridge.journal.load(False),record)
            finally:
                task.cancel()
                with self.assertRaises(asyncio.CancelledError):await task
