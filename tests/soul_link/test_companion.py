import asyncio, base64, json, struct, sys, tempfile, time, unittest
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import AsyncMock
sys.path.insert(0,str(Path(__file__).resolve().parents[2]))
from remote.companion_core import MAGIC,SIZE,PAIR,ViewAssembler,decode_view,DesktopBridge,proposal_valid
from remote.network import Relay,Peer,receive,Companion,local_frame,LOCAL

def snapshot(role=0, state=2, battle=False, digest=100):
    b=bytearray(SIZE);struct.pack_into('<I',b,0,MAGIC);struct.pack_into('<I',b,44,MAGIC)
    struct.pack_into('<8B',b,52,1,not battle,battle,1,3,role,13,1)
    struct.pack_into('<4I',b,60,1,2,10,20);struct.pack_into('<I',b,104,digest)
    PAIR.pack_into(b,180,10,501,1,0,20,601,19,0,1,8,1,1,state,0)
    return bytes(b)

class Bounds(unittest.TestCase):
    def test_reassembly_and_interleaving_refused(self):
        data=snapshot();a=ViewAssembler();result=None
        for offset in range(0,SIZE,48): result=a.add(struct.pack('<IHH',7,offset,SIZE)+data[offset:offset+48])
        self.assertEqual(result,data);self.assertEqual(decode_view(result)['pairs'][0]['species'],[1,19])
        a.add(struct.pack('<IHH',8,0,SIZE)+data[:48])
        with self.assertRaises(ValueError): a.add(struct.pack('<IHH',9,48,SIZE)+data[48:96])
        with self.assertRaises(ValueError): decode_view(data[:-1])
    def test_death_invalidates_accepted_roster(self):
        p=dict(id='a'*32,ids=[1],digest=100,run=[1,2])
        self.assertTrue(proposal_valid(p,[decode_view(snapshot()),decode_view(snapshot(1))]))
        self.assertFalse(proposal_valid(p,[decode_view(snapshot(state=3)),decode_view(snapshot(1))]))
        self.assertFalse(proposal_valid(dict(p,ids=[1,1]),[decode_view(snapshot()),decode_view(snapshot(1))]))

class Coordination(unittest.IsolatedAsyncioTestCase):
    async def asyncSetUp(self):
        self.tmp=tempfile.TemporaryDirectory();root=Path(self.tmp.name);(root/'lgfr-sl.gba').write_bytes(b'test ROM')
        self.net=SimpleNamespace(role=0,secret='test',local=object(),joined=True,epoch=0,wan_send=AsyncMock(),local_send=AsyncMock())
        self.d=DesktopBridge(self.net,root);await self.d.snapshot(snapshot());self.d.peer=decode_view(snapshot(1));self.d.peer_build=self.d.build
        self.d.peer_active=self.d.peer_connected=True;self.d.last_local_beat=time.monotonic()
    async def asyncTearDown(self): self.tmp.cleanup()
    async def test_combat_never_locks_other_game(self):
        self.d.peer['battle']=True;self.d.peer['safe']=False;self.d.requested=True
        await self.d.coordinate();self.net.local_send.assert_not_awaited()
        self.assertEqual('waiting',self.d.status()[0])
        self.assertIn('battle',self.d.status()[1])
        self.d.peer['battle']=False;self.d.peer['safe']=True
        await self.d.coordinate();self.net.local_send.assert_awaited_once()
    async def test_disconnected_partner_retains_reading_but_cannot_apply_or_propose(self):
        old=self.d.peer
        await self.d.message(dict(type='pong',peerConnected=False,peerActive=False))
        self.assertIs(self.d.peer,old)
        self.assertIsNone(self.d.peer_data)
        await self.d.coordinate();self.net.local_send.assert_not_awaited()
        self.d.write_state();state=json.loads((Path(self.tmp.name)/'compagnon-etat.json').read_text())
        self.assertFalse(state['canPropose']);self.assertFalse(state['peerActive'])
        self.assertEqual(state['peer']['run'],old['run'])
    async def test_relay_reconnect_waits_for_a_fresh_view_even_after_active_pong(self):
        old=self.d.peer;self.d.metrics.add('relayRoundTrip',12);self.d.relay_disconnected()
        self.assertIsNone(self.d.metrics.report()['timings']['relayRoundTrip']['ageMs'])
        self.assertIs(old,self.d.peer);self.assertFalse(self.d.peer_active)
        await self.d.message(dict(type='pong',peerConnected=True,peerActive=True))
        await self.d.coordinate();self.net.local_send.assert_not_awaited()
        self.d.write_state();state=json.loads((self.d.root/'compagnon-etat.json').read_text())
        self.assertFalse(state['canPropose']);self.assertFalse(state['peerActive'])
        self.assertIn('fresh',state['title'])
        await self.d.message(dict(type='view',data=base64.b64encode(snapshot(1)).decode(),build=self.d.build))
        self.assertFalse(self.d.peer_refresh_required);self.assertTrue(self.d.peer_active)
        self.d.write_state();self.assertTrue(json.loads((self.d.root/'compagnon-etat.json').read_text())['canPropose'])

    async def test_partner_restart_also_requires_new_view_before_resuming(self):
        await self.d.message(dict(type='pong',peerConnected=False,peerActive=False))
        await self.d.message(dict(type='pong',peerConnected=True,peerActive=True))
        await self.d.coordinate();self.net.local_send.assert_not_awaited()
        self.assertTrue(self.d.peer_refresh_required);self.assertFalse(self.d.peer_active)
        await self.d.message(dict(type='view',data=base64.b64encode(snapshot(1)).decode(),build=self.d.build))
        self.assertFalse(self.d.peer_refresh_required)

    async def test_background_apply_does_not_wait_for_partners_battle(self):
        data=bytearray(snapshot(1,battle=True,digest=101));struct.pack_into('<I',data,48,1)
        await self.d.message(dict(type='view',data=base64.b64encode(data).decode(),build=self.d.build))
        await self.d.coordinate()
        self.assertEqual(self.d.pending['kind'],5)
        calls=self.net.local_send.await_args_list
        self.assertEqual(len(calls),63)
        self.assertEqual(calls[-1].args[0],10)
        chunks=calls[:-1]
        self.assertTrue(all(call.args[0]==11 for call in chunks))
        self.assertEqual(b''.join(call.args[1][8:] for call in chunks),bytes(data[44:]))
        self.assertEqual('waiting',self.d.status()[0])
    async def test_background_apply_waits_only_for_local_safety(self):
        data=bytearray(snapshot(1,digest=101));struct.pack_into('<I',data,48,1)
        await self.d.message(dict(type='view',data=base64.b64encode(data).decode(),build=self.d.build))
        self.d.local['safe']=False
        await self.d.coordinate();self.net.local_send.assert_not_awaited()
        self.d.local['safe']=True;await self.d.coordinate()
        self.assertEqual(self.d.pending['kind'],5)
    async def test_same_background_reading_does_not_loop_after_rejection(self):
        self.d.peer_data=snapshot(1,digest=101);self.d.peer['digest']=101
        await self.d.coordinate();self.assertEqual(self.d.pending['kind'],5)
        self.d.pending=None;self.net.local_send.reset_mock()
        await self.d.coordinate();self.net.local_send.assert_not_awaited()
        self.d.peer['digest']=102;await self.d.coordinate();self.assertEqual(self.d.pending['kind'],5)

    async def test_local_disconnect_during_background_upload_can_retry(self):
        self.d.peer_data=snapshot(1,digest=101);self.d.peer['digest']=101
        self.net.local_send.side_effect=[None,None,ConnectionError('socket closed')]
        await self.d.coordinate()
        self.assertIsNone(self.d.pending);self.assertIsNone(self.d.async_attempt)
        self.net.local_send.side_effect=None;self.d.async_until=0
        await self.d.coordinate();self.assertEqual(self.d.pending['kind'],5)

    async def test_unchanged_view_does_not_start_periodic_full_sync(self):
        self.d.synced=self.d.pair_signature();self.net.wan_send.reset_mock()
        await self.d.snapshot(snapshot());await self.d.coordinate()
        self.net.wan_send.assert_not_awaited();self.net.local_send.assert_not_awaited()
        self.d.last_view_sent-=11;await self.d.snapshot(snapshot());await self.d.coordinate()
        self.net.wan_send.assert_awaited_once();self.net.local_send.assert_not_awaited()
    async def test_local_gui_sync_requests_both_games(self):
        p=self.d.commands/'one.json';p.write_text(json.dumps(dict(action='sync',session=self.d.command_session,issued=time.time(),duo=[self.d.local['run'],self.d.local['participants']])))
        await self.d.actions();self.assertFalse(p.exists());self.net.wan_send.assert_any_await('sync_request')
    async def test_cleanup_failure_never_replays_a_consumed_command(self):
        self.net.wan_send.reset_mock()
        from unittest.mock import patch
        for action in ['sync','propose','accept','reject']:
            self.net.wan_send.reset_mock()
            self.d.proposal=dict(id='a'*32,ids=[1],digest=100,run=[1,2],author=1)
            request=dict(action=action,ids=[1],id='a'*32,session=self.d.command_session,issued=time.time(),duo=[self.d.local['run'],self.d.local['participants']])
            p=self.d.commands/(action+'.json');p.write_text(json.dumps(request))
            with patch.object(Path,'unlink',side_effect=PermissionError('locked')):
                await self.d.actions();await self.d.actions()
            self.assertEqual(1,self.net.wan_send.await_count,action)
            self.assertFalse(p.exists())
        self.assertEqual(4,len(list(self.d.commands.glob('*.processing'))))
        other=DesktopBridge(self.net,self.d.root);other.local=self.d.local
        await other.actions();self.assertEqual(1,self.net.wan_send.await_count)

    async def test_claim_failure_does_not_execute_or_delete_pending_command(self):
        self.net.wan_send.reset_mock()
        from unittest.mock import patch
        p=self.d.commands/'locked.json';p.write_text(json.dumps(dict(action='sync',session=self.d.command_session,issued=time.time(),duo=[self.d.local['run'],self.d.local['participants']])))
        with patch.object(Path,'rename',side_effect=PermissionError('locked')):
            await self.d.actions()
        self.assertTrue(p.exists());self.net.wan_send.assert_not_awaited()

    async def test_current_session_proposals_and_votes_still_reach_relay(self):
        envelope=dict(session=self.d.command_session,issued=time.time(),duo=[self.d.local['run'],self.d.local['participants']])
        for action in ['propose','accept','reject']:
            self.d.proposal=dict(id='a'*32,ids=[1],digest=100,run=[1,2],author=1)
            request=dict(envelope,action=action,ids=[1],id='a'*32)
            p=self.d.commands/'current.json';p.write_text(json.dumps(request));self.net.wan_send.reset_mock()
            await self.d.actions();self.assertFalse(p.exists())
            self.assertEqual('propose' if action=='propose' else 'vote',self.net.wan_send.await_args.args[0])

    async def test_queued_commands_cannot_cross_session_age_or_duo(self):
        valid=dict(action='sync',session=self.d.command_session,issued=time.time(),duo=[self.d.local['run'],self.d.local['participants']])
        for patch in [dict(session='old'),dict(issued=1),dict(issued=time.time()+60),dict(issued=True),dict(issued=float('nan')),dict(duo=[[9,9],[10,20]])]:
            p=self.d.commands/'old.json';p.write_text(json.dumps(dict(valid,**patch)))
            self.net.wan_send.reset_mock();self.d.requested=False
            await self.d.actions();self.assertFalse(p.exists());self.net.wan_send.assert_not_awaited();self.assertFalse(self.d.requested)
        p=self.d.commands/'restart.json';p.write_text(json.dumps(valid))
        other=DesktopBridge(self.net,self.d.root);other.local=self.d.local
        await other.actions();self.net.wan_send.assert_not_awaited();self.assertFalse(p.exists())

    async def test_partner_already_waiting_can_join_despite_cooldown(self):
        self.d.peer.update(safe=False,status=1);self.d.last_attempt=time.monotonic()
        await self.d.coordinate();self.net.local_send.assert_awaited_once()
    async def test_new_capture_does_not_wait_ten_seconds_after_last_sync(self):
        self.d.synced=self.d.pair_signature()
        self.d.last_attempt=time.monotonic()-1.1
        self.d.local['digest']+=1
        await self.d.coordinate();self.net.local_send.assert_awaited_once()
    async def test_repeat_attempt_is_bounded(self):
        self.d.last_attempt=time.monotonic()
        await self.d.coordinate();self.net.local_send.assert_not_awaited()
    async def test_final_views_catch_up_without_duplicate_sync(self):
        self.d.awaiting_completion=True;self.d.completion_until=time.monotonic()+2
        self.d.last_attempt=time.monotonic()-3
        await self.d.coordinate();self.net.local_send.assert_not_awaited()
        self.d.completion_until=time.monotonic()-1
        await self.d.coordinate();self.net.local_send.assert_awaited_once()
    async def test_init_ack_waits_for_run_view_instead_of_repeating(self):
        self.d.pending=dict(id=55,kind=2,time=time.monotonic())
        ack=bytearray(snapshot());struct.pack_into('<I',ack,8,55);ack[13]=1
        struct.pack_into('<2I',ack,60,0,0)
        await self.d.snapshot(bytes(ack));await self.d.coordinate()
        self.net.local_send.assert_not_awaited()
    async def test_changed_view_wakes_idle_coordinator(self):
        self.d.wakeup.clear();await self.d.snapshot(snapshot(digest=101))
        self.assertTrue(self.d.wakeup.is_set())
        self.d.wakeup.clear();await self.d.message(dict(type='sync_request'))
        self.assertTrue(self.d.wakeup.is_set())
    async def test_initial_tracking_does_not_wait_for_partners_battle(self):
        self.d.local.update(run=[0,0],enabled=False)
        self.d.peer.update(safe=False,battle=True)
        await self.d.coordinate()
        self.assertEqual(self.d.pending['kind'],2)
        self.assertEqual(struct.unpack_from('<2I',self.net.local_send.call_args.args[1],8),(1,2))
    async def test_initial_tracking_starts_before_partner_joins(self):
        self.d.local.update(run=[0,0],enabled=False)
        self.d.peer=None;self.d.peer_active=False
        await self.d.coordinate()
        self.assertEqual(self.d.pending['kind'],2)
        first=self.net.local_send.call_args.args[1][8:24]
        self.d.pending=None;await self.d.coordinate()
        self.assertEqual(first,self.net.local_send.call_args.args[1][8:24])
    async def test_accepted_team_survives_pending_command_and_busy_retry(self):
        proposal=dict(id='c'*32,ids=[1],digest=100,run=[1,2])
        self.d.pending=dict(id=55,kind=1,time=time.monotonic())
        await self.d.message(dict(type='apply_team',proposal=proposal))
        self.net.local_send.assert_not_awaited();self.assertEqual(self.d.accepted_team,proposal)
        self.d.pending=None;await self.d.coordinate()
        self.assertEqual(self.d.pending['kind'],3);self.assertEqual(self.d.accepted_team,proposal)
        ack=bytearray(snapshot());struct.pack_into('<I',ack,8,self.d.pending['id']);ack[13]=3
        await self.d.snapshot(bytes(ack));self.assertIsNotNone(self.d.accepted_team)
        self.d.team_retry_at=0
        await self.d.coordinate();self.assertEqual(self.net.local_send.await_count,2)
        struct.pack_into('<I',ack,8,self.d.pending['id']);ack[13]=1
        await self.d.snapshot(bytes(ack));self.assertIsNotNone(self.d.accepted_team)
        for view in (self.d.local,self.d.peer):
            view.update(team=[1],locked=True,party=[dict(pair=1)])
        await self.d.coordinate();self.assertIsNone(self.d.accepted_team)
    async def test_death_applies_with_partner_in_dialogue_restarted_or_disabled(self):
        for scenario in ('dialogue','restart','disabled'):
            self.d.pending=None;self.d.synced=self.d.pair_signature();self.net.local_send.reset_mock()
            dead=bytearray(snapshot(1,state=3,digest=101));dead[53]=0
            # Fixture: remote player's recorded encounter is dead.
            dead[180+29]=3
            await self.d.message(dict(type='view',data=base64.b64encode(dead).decode(),build=self.d.build))
            if scenario=='restart':
                await self.d.message(dict(type='view',data=base64.b64encode(snapshot(1)).decode(),build=self.d.build))
                await self.d.message(dict(type='pong',peerConnected=False,peerActive=False))
            elif scenario=='disabled': self.d.peer['enabled']=False
            await self.d.coordinate()
            self.assertEqual(self.d.pending['kind'],4,scenario)
            self.assertEqual(struct.unpack_from('<H',self.net.local_send.call_args.args[1],24)[0],1)
        other=DesktopBridge(self.net,self.d.root);self.assertEqual(other.death_backlog['ids'],[1])
    async def test_own_death_is_replayed_after_restart_and_reconnect(self):
        dead=bytearray(snapshot(state=3,digest=101));dead[180+28]=3
        self.net.joined=False;await self.d.snapshot(bytes(dead))
        fresh=DesktopBridge(self.net,self.d.root)
        self.net.joined=True;self.net.wan_send.reset_mock()
        await fresh.coordinate()
        self.net.wan_send.assert_awaited_once_with('death_notice',event=fresh.local_deaths,build=fresh.build)
        self.assertEqual(fresh.local_deaths['run'],[1,2])
        self.assertEqual(fresh.local_deaths['ids'],[1])
    async def test_bad_readonly_view_does_not_disconnect_the_game(self):
        c=Companion('127.0.0.1',1,0,'test',ui_root=self.d.root)
        server=await asyncio.start_server(c.local_handle,'127.0.0.1',0)
        reader,writer=await asyncio.open_connection('127.0.0.1',server.sockets[0].getsockname()[1])
        try:
            for key,data in ((1,bytes(SIZE)),(2,snapshot())):
                for offset in range(0,SIZE,48):
                    writer.write(local_frame(9,0,0,struct.pack('<IHH',key,offset,SIZE)+data[offset:offset+48]))
            writer.write(local_frame(8,0,0));await writer.drain()
            response=await asyncio.wait_for(reader.readexactly(LOCAL.size),2)
            self.assertEqual(response[4],8);self.assertIsNotNone(c.local)
            self.assertEqual(c.desktop.local['pairs'][0]['species'],[1,19])
        finally:
            writer.close();await writer.wait_closed();server.close();await server.wait_closed()
            await asyncio.sleep(.02)
    async def test_b_proposal_waits_for_a_then_death_cancels(self):
        relay=Relay('test');relay.peers={i:Peer(i,None) for i in range(2)}
        messages=[[],[]]
        class Writer:
            def __init__(self,role): self.role=role
            def write(self,data): messages[self.role].append(json.loads(data[4:]))
            async def drain(self): pass
        for i,p in relay.peers.items(): p.writer=Writer(i);p.view=decode_view(snapshot(i));p.view_build='12345678';p.active=True
        proposal=dict(id='b'*32,ids=[1],digest=100,run=[1,2])
        await relay.desktop_message(relay.peers[1],dict(type='propose',proposal=proposal))
        self.assertEqual(relay.proposal['votes'],[False,True]);self.assertFalse(any(m['type']=='apply_team' for m in messages[0]))
        await relay.desktop_message(relay.peers[0],dict(type='vote',id=proposal['id'],accept=True))
        self.assertEqual(messages[0][-1]['type'],'apply_team');self.assertEqual(messages[1][-1]['type'],'apply_team')
        await relay.desktop_message(relay.peers[1],dict(type='propose',proposal=proposal))
        relay.peers[0].view=decode_view(snapshot(state=3));await relay.apply_proposal()
        self.assertIsNone(relay.proposal);self.assertEqual(messages[0][-1]['type'],'proposal_closed')

if __name__=='__main__': unittest.main()
