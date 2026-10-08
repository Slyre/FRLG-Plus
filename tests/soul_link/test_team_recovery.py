import base64
import json
import sys
import tempfile
import time
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import AsyncMock,patch

sys.path.insert(0,str(Path(__file__).resolve().parents[2]))
from remote.companion_core import DesktopBridge,TeamJournal,decode_view,team_applied
from test_companion import snapshot

class Recovery(unittest.IsolatedAsyncioTestCase):
    async def asyncSetUp(self):
        self.tmp=tempfile.TemporaryDirectory();self.addCleanup(self.tmp.cleanup)
        self.root=Path(self.tmp.name);(self.root/'lgfr-sl.gba').write_bytes(b'ROM')
        self.net=SimpleNamespace(role=0,secret='room',local=object(),joined=True,epoch=0,
                                 wan_send=AsyncMock(),local_send=AsyncMock())
        self.d=DesktopBridge(self.net,self.root)
        self.prepare(self.d)
        self.proposal=dict(id='a'*32,ids=[1],run=[1,2],digest=100,author=1)
    def prepare(self,d):
        d.local=decode_view(snapshot());d.peer=decode_view(snapshot(1));d.peer_build=d.build
        d.peer_active=d.peer_connected=True;d.last_local_beat=time.monotonic()
    async def accept(self):
        await self.d.message(dict(type='apply_team',proposal=self.proposal))
    async def test_selection_and_command_ack_are_not_commit_confirmation(self):
        await self.accept();self.assertEqual(self.d.pending['kind'],3)
        data=bytearray(snapshot());data[13]=1
        import struct
        struct.pack_into('<I',data,8,self.d.pending['id'])
        await self.d.snapshot(data)
        self.assertIsNotNone(self.d.accepted_team)
        self.d.local.update(team=[1],locked=True)
        self.assertFalse(team_applied(self.d.local,[1]))
        self.assertNotEqual(self.d.status()[0],'ready')
    async def test_only_one_real_party_applied_stays_pending(self):
        await self.accept();self.d.pending=None
        self.d.team_retry_at=time.monotonic()+10
        self.d.local.update(team=[1],locked=True,party=[dict(pair=1)])
        await self.d.coordinate()
        self.assertIsNotNone(self.d.accepted_team)
        self.assertEqual(self.d.team_record['state'],'pending')
        self.assertEqual(self.d.status()[1],'Team change partly applied')
        self.d.peer.update(team=[1],locked=True,party=[dict(pair=1)])
        await self.d.coordinate()
        self.assertIsNone(self.d.accepted_team)
        self.assertEqual(self.d.team_record['state'],'confirmed')
    async def test_restart_reproposes_exact_saved_agreement_before_native_command(self):
        await self.accept();self.net.local_send.reset_mock();self.net.wan_send.reset_mock()
        fresh=DesktopBridge(self.net,self.root);self.prepare(fresh)
        await fresh.coordinate()
        self.net.wan_send.assert_awaited_once_with('propose',proposal=self.proposal)
        self.net.local_send.assert_not_awaited()

    async def test_two_clients_and_fresh_relay_resume_prior_approval(self):
        from remote.network import Relay,Peer
        from collections import deque
        await self.accept()
        second_root=self.root/'b';second_root.mkdir();(second_root/'lgfr-sl.gba').write_bytes(b'ROM')
        net_b=SimpleNamespace(role=1,secret='room',local=object(),joined=True,epoch=0,
                              wan_send=AsyncMock(),local_send=AsyncMock())
        b=DesktopBridge(net_b,second_root)
        b.local=decode_view(snapshot(1));b.peer=decode_view(snapshot());b.peer_build=b.build
        b.peer_active=b.peer_connected=True;b.last_local_beat=time.monotonic()
        await b.message(dict(type='apply_team',proposal=self.proposal))
        # Both companions and the relay restart; only A's game has committed.
        a=DesktopBridge(self.net,self.root);self.prepare(a)
        b=DesktopBridge(net_b,second_root)
        b.local=decode_view(snapshot(1));b.peer=decode_view(snapshot());b.peer_build=b.build
        b.peer_active=b.peer_connected=True;b.last_local_beat=time.monotonic()
        a.local.update(team=[1],locked=True,party=[dict(pair=1)])
        b.peer=a.local
        relay=Relay('room');queue=deque();bridges=[a,b]
        class Writer:
            def __init__(self,role): self.role=role
            def write(self,data): queue.append((self.role,json.loads(data[4:])))
            async def drain(self): pass
        for role,bridge in enumerate(bridges):
            peer=Peer(role,Writer(role));peer.view=bridge.local;peer.view_build=bridge.build;peer.active=True
            relay.peers[role]=peer
            async def send(kind,role=role,**values):
                await relay.desktop_message(relay.peers[role],dict(type=kind,**values))
            bridge.net.wan_send=AsyncMock(side_effect=send)
            bridge.net.local_send.reset_mock()
        await a.coordinate()
        while queue:
            role,message=queue.popleft();await bridges[role].message(message)
        self.assertIsNone(relay.proposal)
        for bridge in bridges:
            self.assertFalse(bridge.team_recovery)
            self.assertIsNotNone(bridge.accepted_team)
            self.assertEqual(bridge.pending['kind'],3)
            bridge.net.local_send.assert_awaited_once()
    async def test_matching_agreement_auto_votes_but_different_one_does_not(self):
        await self.accept();self.net.wan_send.reset_mock()
        await self.d.message(dict(type='proposal',proposal=self.proposal))
        self.net.wan_send.assert_awaited_once_with('vote',id=self.proposal['id'],accept=True)
        self.net.wan_send.reset_mock()
        await self.d.message(dict(type='proposal',proposal=dict(self.proposal,id='b'*32)))
        self.net.wan_send.assert_not_awaited()
    async def test_missing_agreement_requires_manual_approval(self):
        await self.d.message(dict(type='proposal',proposal=self.proposal))
        self.net.wan_send.assert_not_awaited()
        self.assertEqual(self.d.proposal,self.proposal)
    async def test_dead_pair_cancels_without_native_retry_or_auto_vote(self):
        await self.accept();self.d.pending=None;self.d.local['pairs'][0]['state']=3
        self.net.local_send.reset_mock();await self.d.coordinate()
        self.assertEqual(self.d.team_record['state'],'cancelled')
        self.assertIsNone(self.d.accepted_team);self.net.local_send.assert_not_awaited()
        self.net.wan_send.reset_mock()
        await self.d.message(dict(type='proposal',proposal=self.proposal))
        self.net.wan_send.assert_not_awaited()
    async def test_changed_run_participant_or_rules_cancels(self):
        for key,value in (('run',[3,4]),('participants',[11,21]),('config',[1,1,1,1,1,2,0,0])):
            self.prepare(self.d);await self.accept();self.d.pending=None
            self.d.peer[key]=value;await self.d.coordinate()
            self.assertEqual(self.d.team_record['state'],'cancelled',key)
    async def test_disk_failure_prevents_native_change_until_saved(self):
        with patch.object(self.d.team_journal,'save',return_value=False):
            await self.accept()
            self.net.local_send.assert_not_awaited()
            self.assertIsNotNone(self.d.accepted_team)
        self.d.team_retry_at=0;await self.d.coordinate()
        self.assertTrue(self.d.team_saved)
        self.d.team_retry_at=0;await self.d.coordinate()
        self.net.local_send.assert_awaited_once()
    async def test_corrupt_oversized_other_room_and_other_build_records_ignored(self):
        await self.accept()
        path=self.d.team_journal.path;data=path.read_bytes()
        self.assertIsNone(TeamJournal(self.root,'other',0,self.d.build).load())
        self.assertIsNone(TeamJournal(self.root,'room',1,self.d.build).load())
        self.assertIsNone(TeamJournal(self.root,'room',0,'12345678').load())
        for bad in (b'{',b'[]',b'x'*4097):
            path.write_bytes(bad);self.assertIsNone(self.d.team_journal.load())
        envelope=json.loads(data);envelope['record']['proposal']['ids']=[2]
        path.write_text(json.dumps(envelope));self.assertIsNone(self.d.team_journal.load())
    async def test_interrupted_write_preserves_previous_agreement(self):
        await self.accept();old=self.d.team_journal.load()
        new=dict(self.d.team_record,state='confirmed')
        with patch('remote.companion_core.os.replace',side_effect=PermissionError()):
            self.assertFalse(self.d.team_journal.save(new))
        self.assertEqual(self.d.team_journal.load(),old)
    async def test_confirmed_receipt_recovers_older_game_save_on_restart(self):
        await self.accept();self.d.finish_team('confirmed')
        fresh=DesktopBridge(self.net,self.root);self.prepare(fresh)
        self.net.wan_send.reset_mock();self.net.local_send.reset_mock()
        await fresh.coordinate()
        self.net.wan_send.assert_awaited_once_with('propose',proposal=self.proposal)
        self.net.local_send.assert_not_awaited()

if __name__=='__main__':unittest.main()
