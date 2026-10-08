"""Explicit public TLS failure injection; creates an isolated temporary room."""
import argparse
import asyncio
import json
from pathlib import Path
import secrets
import sys
import time
sys.path.insert(0,str(Path(__file__).resolve().parents[2]))
from remote.network import Companion

async def check(host,port):
    secret=secrets.token_hex(24)
    clients=[Companion(host,port,role,secret,tls=True) for role in range(2)]
    failure=asyncio.Event();healthy=asyncio.Event();original=clients[0].heartbeat
    calls=0
    async def fail_once():
        nonlocal calls
        calls+=1
        if calls==1:
            await failure.wait()
            raise ConnectionError('test heartbeat write failure')
        healthy.set()
        await original()
    clients[0].heartbeat=fail_once
    tasks=[asyncio.create_task(client.connect_forever()) for client in clients]
    async def until(predicate):
        async def wait():
            while not predicate():
                for task in tasks:
                    if task.done(): await task;raise RuntimeError('connection task stopped')
                await asyncio.sleep(.01)
        await asyncio.wait_for(wait(),30)
    try:
        await until(lambda:all(c.joined for c in clients))
        before=[c.connections for c in clients];started=time.monotonic();failure.set()
        await until(lambda:clients[0].joined and clients[0].connections>before[0] and healthy.is_set())
        milliseconds=round((time.monotonic()-started)*1000,1)
        assert clients[1].joined and clients[1].connections==before[1]
        assert all(c.tls_context.check_hostname for c in clients)
        return dict(host=host,port=port,recoveredMs=milliseconds,heartbeatCalls=calls,
                    unaffectedPartner=True,certificateVerification=True,
                    scope='Two companion TLS sockets on one PC; no native game or separate-PC qualification')
    finally:
        for task in tasks:task.cancel()
        await asyncio.gather(*tasks,return_exceptions=True)

if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--host',required=True);parser.add_argument('--port',type=int,default=8443)
    parser.add_argument('--report',type=Path,required=True);args=parser.parse_args()
    result=asyncio.run(check(args.host,args.port));args.report.parent.mkdir(parents=True,exist_ok=True)
    args.report.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result))
