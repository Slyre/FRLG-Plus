"""Optional smoke test against the deployed TLS relay; creates temporary rooms."""
import asyncio,base64,secrets,ssl,sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[2]))
from remote.network import send,receive
from test_companion import snapshot
async def main():
    context=ssl.create_default_context(cafile=Path(__file__).resolve().parents[2]/'remote/relay-ca.pem')
    secret=secrets.token_hex(24);clients=[]
    try:
        for role in (0,1):
            reader,writer=await asyncio.open_connection('gbfbc.com',8443,ssl=context,server_hostname='gbfbc.com')
            clients.append((reader,writer));await send(writer,dict(type='join',version=1,role=role,secret=secret))
            assert (await asyncio.wait_for(receive(reader),5))['type']=='joined'
        for role in (0,1):
            await send(clients[role][1],dict(type='view',data=base64.b64encode(snapshot(role)).decode(),build='12345678'))
            assert (await asyncio.wait_for(receive(clients[role^1][0]),5))['type']=='view'
        proposal=dict(id=secrets.token_hex(16),ids=[1],digest=100,run=[1,2])
        await send(clients[1][1],dict(type='propose',proposal=proposal))
        for reader,_ in clients:
            response=await asyncio.wait_for(receive(reader),5);assert response['type']=='proposal' and response['proposal']['author']==1
        await send(clients[0][1],dict(type='vote',id=proposal['id'],accept=True))
        for reader,_ in clients: assert (await asyncio.wait_for(receive(reader),5))['type']=='apply_team'
        await send(clients[1][1],dict(type='sync_request'))
        for reader,_ in clients: assert (await asyncio.wait_for(receive(reader),5))['type']=='sync_request'
        await send(clients[0][1],dict(type='ping'))
        assert await asyncio.wait_for(receive(clients[0][0]),5)=={'type':'pong'}
        death=dict(role=1,run=[1,2],participants=[10,20],ids=[1])
        await send(clients[1][1],dict(type='death_notice',event=death,build='12345678'))
        response=await asyncio.wait_for(receive(clients[0][0]),5)
        assert response==dict(type='death_notice',event=death,build='12345678')
        print('PASS public VPS: verified TLS, both snapshots, B proposal, A approval, shared sync, death notice and legacy ping.')
    finally:
        for _,writer in clients: writer.close()
        for _,writer in clients:
            try: await writer.wait_closed()
            except ConnectionError: pass
if __name__=='__main__':asyncio.run(main())
