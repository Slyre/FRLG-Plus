import asyncio
import json
from pathlib import Path
import tempfile
import time
import unittest
from remote.companion_web import CompanionWeb


class WebTests(unittest.IsolatedAsyncioTestCase):
    async def asyncSetUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)
        (self.root/'companion-web').mkdir()
        (self.root/'companion-assets').mkdir()
        for name in ('index.html','style.css','app.js'):
            (self.root/'companion-web'/name).write_text('fixture')
        (self.root/'companion-assets/species.json').write_text('{}')
        self.state=dict(commandSession="b"*32,updated=time.time(),canPropose=True,local=dict(pairs=[dict(id=1,state=2),dict(id=2,state=3)]),proposal=dict(id='a'*32,author=1))
        self.write_state()
        self.web=await CompanionWeb(self.root,0).start()

    def write_state(self):
        (self.root/'compagnon-etat.json').write_text(json.dumps(self.state),encoding='utf-8')

    async def asyncTearDown(self):
        await self.web.close()
        self.tmp.cleanup()

    async def http(self, route='', method='GET', body=None, headers=None):
        payload=json.dumps(body).encode() if body is not None else b''
        fields={'Host':self.web.host}
        if body is not None: fields.update({'Content-Type':'application/json','Content-Length':str(len(payload))})
        fields.update(headers or {})
        head=f'{method} {route if route.startswith("/") else self.web.prefix+route} HTTP/1.1\r\n'+''.join(f'{k}: {v}\r\n' for k,v in fields.items())+'\r\n'
        reader,writer=await asyncio.open_connection('127.0.0.1',int(self.web.host.split(':')[1]))
        writer.write(head.encode()+payload);await writer.drain()
        response=await reader.read();writer.close();await writer.wait_closed()
        status=int(response.split(b' ')[1]);header,data=response.split(b'\r\n\r\n',1)
        return status,header,data

    async def test_diagnostic_export_omits_game_and_session_data(self):
        self.state.update(secret="private-room",local={"pairs":[{"id":9}]},metrics={"reconnects":2})
        self.write_state()
        status,_,data=await self.http("diagnostics")
        self.assertEqual(status,200)
        self.assertEqual(json.loads(data),{"version":1,"metrics":{"reconnects":2}})
        self.assertNotIn(b"private-room",data)
        self.assertNotIn(self.web.token.encode(),data)

    async def test_local_scope_and_capability(self):
        self.assertEqual('127.0.0.1',self.web.server.sockets[0].getsockname()[0])
        self.assertEqual(404,(await self.http('/state'))[0])
        self.assertEqual(404,(await self.http('connexion-secret.txt'))[0])
        self.assertEqual(404,(await self.http('../connexion-secret.txt'))[0])
        self.assertEqual(403,(await self.http('state',headers={'Host':'evil.example'}))[0])
        self.assertEqual(403,(await self.http('state',headers={'Origin':'https://evil.example'}))[0])
        self.assertEqual(403,(await self.http('state',headers={'Sec-Fetch-Site':'cross-site'}))[0])
        status,headers,_=await self.http()
        self.assertEqual(200,status);self.assertIn(b"frame-ancestors 'none'",headers)

    async def test_state_and_atomic_commands(self):
        self.assertEqual(self.state,json.loads((await self.http('state'))[2]))
        self.state['proposal']=None;self.write_state()
        self.assertEqual(200,(await self.http('actions','POST',dict(action='propose',ids=[1])))[0])
        files=list((self.root/'compagnon-commandes').glob('*.json'))
        self.assertEqual(1,len(files));queued=json.loads(files[0].read_text());self.assertEqual('propose',queued['action']);self.assertEqual([1],queued['ids']);self.assertEqual('b'*32,queued['session']);self.assertEqual([None,None],queued['duo']);self.assertLess(abs(time.time()-queued['issued']),2)
        self.assertEqual(400,(await self.http('actions','POST',dict(action='propose',ids=[2])))[0])
        self.assertEqual(400,(await self.http('actions','POST',dict(action='propose',ids=[1,1])))[0])
        self.assertEqual(400,(await self.http('actions','POST',dict(action='propose',ids=[True])))[0])
        self.assertEqual(400,(await self.http('actions','POST',dict(action='propose',ids=[{}])))[0])
        self.assertEqual(400,(await self.http('actions','POST',dict(action='unknown')))[0])
        self.assertEqual(400,(await self.http('actions','POST',dict(action='sync'),{'Content-Type':'text/plain'}))[0])

    async def test_stale_and_partner_vote(self):
        self.assertEqual(200,(await self.http('actions','POST',dict(action='accept',id='a'*32)))[0])
        self.state['proposal']['author']=0;self.write_state()
        self.assertEqual(400,(await self.http('actions','POST',dict(action='accept',id='a'*32)))[0])
        self.assertEqual(200,(await self.http('actions','POST',dict(action='reject',id='a'*32)))[0])
        self.state['updated']=1;self.write_state()
        self.assertEqual(400,(await self.http('actions','POST',dict(action='sync')))[0])

    async def test_invalid_snapshot_timestamps_and_missing_session_cannot_queue(self):
        for updated in [None,True,'100',float('nan'),float('inf'),time.time()+60]:
            self.state['updated']=updated;self.write_state()
            self.assertEqual(400,(await self.http('actions','POST',dict(action='sync')))[0])
        self.state['updated']=time.time();self.state.pop('commandSession');self.write_state()
        self.assertEqual(400,(await self.http('actions','POST',dict(action='sync')))[0])
        self.assertFalse((self.root/'compagnon-commandes').exists())

    async def test_http_queue_is_consumed_by_current_bridge_but_not_after_restart(self):
        from types import SimpleNamespace
        from unittest.mock import AsyncMock
        from remote.companion_core import DesktopBridge,decode_view
        from test_companion import snapshot
        net=SimpleNamespace(role=0,secret='test',local=object(),joined=True,epoch=0,wan_send=AsyncMock(),local_send=AsyncMock())
        (self.root/'lgfr-sl.gba').write_bytes(b'fixture')
        bridge=DesktopBridge(net,self.root);bridge.local=decode_view(snapshot());bridge.write_state()
        self.assertEqual(200,(await self.http('actions','POST',dict(action='sync')))[0])
        await bridge.actions();net.wan_send.assert_awaited_once_with('sync_request')
        net.wan_send.reset_mock()
        self.assertEqual(200,(await self.http('actions','POST',dict(action='sync')))[0])
        restarted=DesktopBridge(net,self.root);restarted.local=bridge.local
        await restarted.actions();net.wan_send.assert_not_awaited()
        self.assertFalse(list((self.root/'compagnon-commandes').glob('*.json')))

    async def test_disk_failure_returns_an_actionable_error_and_server_recovers(self):
        from unittest.mock import patch
        with patch('remote.companion_web.os.replace',side_effect=PermissionError('locked')):
            status,_,body=await self.http('actions','POST',dict(action='sync'))
        self.assertEqual(400,status);self.assertIn('folder',json.loads(body)['error'])
        self.assertFalse(list((self.root/'compagnon-commandes').glob('*.json')))
        self.assertEqual(200,(await self.http('actions','POST',dict(action='sync')))[0])

    async def test_cleanup(self):
        self.assertEqual(None,json.loads((await self.http('theme'))[2])['theme'])
        self.assertEqual(200,(await self.http('theme','POST',dict(theme='dark')))[0])
        self.assertEqual('dark',json.loads((await self.http('theme'))[2])['theme'])
        self.assertEqual(400,(await self.http('theme','POST',dict(theme='yellow')))[0])
        self.assertEqual(404,(await self.http('invite'))[0])
        await self.web.close();self.assertFalse((self.root/'compagnon-web.json').exists())

if __name__=='__main__': unittest.main()
