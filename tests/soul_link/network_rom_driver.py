"""Real TCP relay/companions, optional WAN impairment, and two mGBA cores."""
import asyncio
import time
import base64
import secrets
import contextlib
import os
import logging
from pathlib import Path
import random
import shutil
import json
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from remote.companion_web import CompanionWeb
from remote.network import Relay, Companion, DATA
from remote.companion_core import team_applied


class MeasuredCompanion(Companion):
    async def local_send(self, kind, payload=b''):
        # Test-only loss at the process boundary: the restarted instance has no hold.
        if getattr(self, 'hold_team_ready', False) and kind == DATA and len(payload) == 64 and payload[17] == 7:
            self.held_team_ready = getattr(self, 'held_team_ready', 0) + 1
            # Acknowledge transport receipt while delaying native consumption.
            # This distinguishes delivery from the native party commit.
            await self.wan_send('delivered', sequence=self.pending_rx.popleft())
            return
        await super().local_send(kind, payload)

    async def wan_send(self, kind, **fields):
        if kind == 'start': self.row_packets = [0,0]; self.baseline_advertised = None
        if kind == 'data':
            packet=base64.b64decode(fields['packet'])
            if packet[17] == 1: self.baseline_advertised=packet[42]
            if packet[17] == 2: self.row_packets[0] += 1
            if packet[17] == 4: self.row_packets[1] += 1
        if kind == 'finish':
            if not hasattr(self,'completed_transfers'): self.completed_transfers=[]
            self.completed_transfers.append(dict(baseline=self.baseline_advertised,stateRows=self.row_packets[0],proposalRows=self.row_packets[1]))
            logging.info('TEST player %s: baseline=%s, state rows=%s, proposal rows=%s',
                         'AB'[self.role],self.baseline_advertised,*self.row_packets)
        await super().wan_send(kind, **fields)


async def main():
    public_host=os.environ.get("SOUL_LINK_TEST_PUBLIC_RELAY")
    secret = secrets.token_hex(24) if public_host else "integration-test-secret-local-only-123456"
    relay = Relay(secret)
    server = await asyncio.start_server(relay.handle, "127.0.0.1", 18765)
    delay = float(os.environ.get("SOUL_LINK_TEST_DELAY", "0"))
    rng = random.Random(12)

    async def proxy(reader, writer):
        remote_reader, remote_writer = await asyncio.open_connection("127.0.0.1", 18765)

        async def pipe(source, target):
            try:
                while True:
                    data = await source.read(2048)
                    if not data:
                        break
                    await asyncio.sleep(delay * (0.5 + rng.random()))
                    # TCP can split any application frame at any byte.
                    for offset in range(0, len(data), 13):
                        target.write(data[offset:offset+13])
                        await target.drain()
            except ConnectionError:
                # Deliberate WAN cuts can reset the opposite half mid-write.
                pass
            finally:
                target.close()

        await asyncio.gather(pipe(reader, remote_writer), pipe(remote_reader, writer))

    impairment = await asyncio.start_server(proxy, "127.0.0.1", 18764)
    desktop_test = os.environ.get('SOUL_LINK_COMPANION_TEST')=='1'
    roots=[]
    partial_test=os.environ.get('SOUL_LINK_PARTIAL_TEAM_TEST')=='1'
    if desktop_test:
        for role in 'ab':
            root=Path('build/tests/desktop-'+role);root.mkdir(exist_ok=True)
            shutil.copyfile('pokefirered.gba',root/'lgfr-sl.gba')
            (root/'compagnon-commandes').mkdir(exist_ok=True)
            for command in (root/'compagnon-commandes').glob('*.json'): command.unlink()
            (root/'morts-en-attente.json').unlink(missing_ok=True)
            (root/'morts-signalees.json').unlink(missing_ok=True)
            if partial_test:
                (root/'team-recovery.json').unlink(missing_ok=True)
                (root/'duo-history.json').unlink(missing_ok=True)
            for directory in ['companion-web','companion-assets']: (root/directory).mkdir(exist_ok=True)
            for name in ['index.html','app.js','style.css']:
                shutil.copyfile(Path('remote/companion-web')/name,root/'companion-web'/name)
            shutil.copyfile('remote/companion-assets/species.json',root/'companion-assets/species.json')
            Path('build/tests/desktop-command-'+role+'.request').unlink(missing_ok=True)
            roots.append(root)
        Path('build/tests/desktop-accept.request').unlink(missing_ok=True)
    companions = [MeasuredCompanion(public_host or "127.0.0.1", 8443 if public_host else 18764, role, secret,tls=bool(public_host),ui_root=roots[role] if desktop_test else None) for role in range(2)]
    if partial_test and os.environ.get('SOUL_LINK_PARTIAL_FIRST') == '1':
        companions[0].hold_team_ready = True
        print('Test injection: hold final readiness delivery to A until both companions restart', flush=True)
    servers = [server, impairment]
    tasks = [asyncio.create_task(relay.watchdog())]
    client_tasks=[];local_servers=[]
    async def start_clients():
        for role, companion in enumerate(companions):
            local_servers.append(await asyncio.start_server(companion.local_handle, "127.0.0.1", 18766+role))
            client_tasks.append(asyncio.create_task(companion.connect_forever()))
            if desktop_test: client_tasks.append(asyncio.create_task(companion.desktop.run()))
    await start_clients()
    cut = Path("build/tests/network-drop.request")
    cut.unlink(missing_ok=True)

    async def force_cut():
        while True:
            await asyncio.sleep(0.02)
            if cut.exists():
                cut.unlink()
                print("Injecting a real WAN TCP disconnect", flush=True)
                if companions[1].wan:
                    companions[1].wan.close()

    tasks.append(asyncio.create_task(force_cut()))
    restart=Path('build/tests/desktop-restart.request');restarted=Path('build/tests/desktop-restart.done')
    partial_seen=Path('build/tests/desktop-partial-observed.done')
    for flag in (restart,restarted,partial_seen):flag.unlink(missing_ok=True)
    restart_count=0
    async def recovery_control():
        nonlocal companions,restart_count
        while True:
            await asyncio.sleep(.02)
            if restart.exists():
                restart.unlink()
                print('Restarting both companion instances and closing their real TCP connections',flush=True)
                if os.environ.get('SOUL_LINK_PARTIAL_FIRST') == '1':
                    assert companions[0].held_team_ready > 0
                for c in companions:
                    if c.local:c.local.close()
                    if c.wan:c.wan.close()
                for task in client_tasks:task.cancel()
                await asyncio.gather(*client_tasks,return_exceptions=True);client_tasks.clear()
                for s in local_servers:s.close();await s.wait_closed()
                local_servers.clear()
                companions=[MeasuredCompanion(public_host or '127.0.0.1',8443 if public_host else 18764,
                    role,secret,tls=bool(public_host),ui_root=roots[role]) for role in range(2)]
                assert all(c.desktop.team_record for c in companions)
                await start_clients()
                for _ in range(250):
                    if all(c.joined for c in companions):break
                    await asyncio.sleep(.02)
                assert all(c.joined for c in companions)
                restart_count+=1;restarted.write_text('fresh companions loaded the saved agreements')
            if restart_count and not partial_seen.exists():
                for c in companions:
                    d=c.desktop
                    if d.accepted_team and d.local and d.peer:
                        applied=[team_applied(v,d.accepted_team['ids']) for v in (d.local,d.peer)]
                        if any(applied) and not all(applied):
                            assert d.status()[1]=='Team change partly applied'
                            assert d.team_record['state']!='confirmed'
                            partial_seen.write_text('actual game readings keep the partial change unconfirmed')
                            print('PASS fresh companions see the actual one-sided native party and keep it unconfirmed',flush=True)
    if partial_test:tasks.append(asyncio.create_task(recovery_control()))
    desktop_web=[CompanionWeb(root,role) for role,root in enumerate(roots)]
    async def accept_proposal():
        flag=Path('build/tests/desktop-accept.request')
        while True:
            await asyncio.sleep(.1)
            for role,web in enumerate(desktop_web):
                request_file=Path('build/tests/desktop-command-'+'ab'[role]+'.request')
                if request_file.exists():
                    try: web.queue(json.loads(request_file.read_text()))
                    except ValueError: continue
                    request_file.unlink()
            proposal=companions[0].desktop.proposal
            if flag.exists() and proposal:
                try: desktop_web[0].queue(dict(action='accept',id=proposal['id']))
                except ValueError: continue
                flag.unlink()
    if desktop_test: tasks.append(asyncio.create_task(accept_proposal()))
    try:
        for _ in range(200):
            if all(c.joined for c in companions):
                break
            await asyncio.sleep(0.02)
        assert all(c.joined for c in companions)
        package = Path("build/tests/network-rom-package")
        process = await asyncio.create_subprocess_exec(
            os.environ.get("SOUL_LINK_COMPANION_BINARY","build/tests/soul_link_companion_rom") if desktop_test else "build/tests/soul_link_network_rom", "pokefirered.gba", "build/tests/network-rom-symbols.txt",
            str(package / "player-a.lua"), str(package / "player-b.lua"))
        code = await process.wait()
        assert code == 0, f"ROM test exit {code}"
        if partial_test:
            assert restart_count==1 and partial_seen.exists()
            for c in companions:
                assert c.desktop.team_record['state']=='confirmed'
                assert not c.desktop.accepted_team
            print('PASS native team recovery: fresh companion instances, durable agreement reused, both actual parties confirmed after reconnection',flush=True)
        if not desktop_test:
            for companion in companions:
                assert companion.completed_transfers[-1] == dict(baseline=1,stateRows=1,proposalRows=0), companion.completed_transfers[-1]
            print("Verified actual Lua/TCP stream: one changed row per player, no complete proposal")
        print(f"Complete Lua/TCP/ROM test passed ({'public TLS relay, both clients on this PC' if public_host else f'per-hop delay/jitter: {delay}s'})")
    finally:
        for task in tasks+client_tasks:
            task.cancel()
        await asyncio.gather(*tasks,*client_tasks, return_exceptions=True)
        for companion in companions:
            if companion.wan:
                companion.wan.close()
        for server in servers+local_servers:
            server.close()
            await server.wait_closed()


if __name__ == "__main__":
    logging.basicConfig(level=logging.INFO, format="%(asctime)s %(message)s", datefmt="%H:%M:%S")
    asyncio.run(main())
