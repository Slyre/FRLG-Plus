"""Optional VPS recovery check with synthetic game readings, not two live PCs."""
import asyncio
import base64
import secrets
import ssl
import struct
import sys
import tempfile
import time
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import AsyncMock

sys.path.insert(0,str(Path(__file__).resolve().parents[2]))
from remote.companion_core import DesktopBridge,decode_view
from remote.network import send,receive
from test_companion import snapshot

async def main():
    context=ssl.create_default_context(cafile=Path(__file__).resolve().parents[2]/'remote/relay-ca.pem')
    secret=secrets.token_hex(24); clients=[];bridges=[]
    with tempfile.TemporaryDirectory() as directory:
        try:
            proposal=dict(id=secrets.token_hex(16),ids=[1],run=[1,2],digest=100,author=1)
            for role in (0,1):
                root=Path(directory)/str(role);root.mkdir();(root/'lgfr-sl.gba').write_bytes(b'test ROM')
                net=SimpleNamespace(role=role,secret=secret,local=object(),joined=True,epoch=0,
                                    wan_send=AsyncMock(),local_send=AsyncMock())
                d=DesktopBridge(net,root)
                d.local=decode_view(snapshot(role));d.peer=decode_view(snapshot(role^1));d.peer_build=d.build
                d.peer_active=d.peer_connected=True;d.last_local_beat=time.monotonic()
                await d.message(dict(type='apply_team',proposal=proposal))
                # A new companion only has its durable agreement; the relay
                # room is also new. A's synthetic party has already changed.
                d=DesktopBridge(net,root);net.local_send.reset_mock()
                reader,writer=await asyncio.open_connection('gbfbc.com',8443,ssl=context,server_hostname='gbfbc.com')
                clients.append((reader,writer));bridges.append(d)
                await send(writer,dict(type='join',version=1,role=role,secret=secret))
                assert (await asyncio.wait_for(receive(reader),5))['type']=='joined'
                async def wan(kind,writer=writer,**values): await send(writer,dict(type=kind,**values))
                net.wan_send=AsyncMock(side_effect=wan)
            for role,d in enumerate(bridges):
                data=bytearray(snapshot(role))
                if role==0:
                    struct.pack_into('<H',data,76,1);data[88]=1;data[89]=1
                    struct.pack_into('<4HB',data,108,1,1,20,20,5)
                await d.snapshot(data)
                await bridges[role^1].message(await asyncio.wait_for(receive(clients[role^1][0]),5))
            await bridges[0].coordinate()
            async def pump(role):
                while not (bridges[role].pending and bridges[role].pending['kind']==3):
                    await bridges[role].message(await asyncio.wait_for(receive(clients[role][0]),5))
            await asyncio.gather(pump(0),pump(1))
            for d in bridges:
                assert d.accepted_team and not d.team_recovery
                assert d.team_record['state']=='pending'
                d.net.local_send.assert_awaited_once()
                assert d.status()[1]=='Team change partly applied'
            print('PASS public TLS relay: persisted agreement replay, prior approval reused, both native commands queued; synthetic one-sided parties remain unconfirmed.')
        finally:
            for _,writer in clients: writer.close()
            for _,writer in clients:
                try: await writer.wait_closed()
                except ConnectionError: pass

if __name__=='__main__': asyncio.run(main())
