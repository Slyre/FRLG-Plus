"""Soul Link WAN companion. Python 3.10+, standard library only.

Public VPS connections use verified TLS; public_relay.py supplies the public
listener. The mGBA bridge is loopback. Desktop views and votes stay room-local.
"""
import argparse
import asyncio
import base64
from collections import deque
import contextlib
import hmac
import ipaddress
import json
import logging
import os
from pathlib import Path
import secrets
import socket
import ssl
import struct
import sys
import time

if __package__:
    from .companion_core import DesktopBridge, ViewAssembler, decode_view, proposal_valid, valid_death_notice
else:
    # The Windows embeddable runtime uses an isolated ._pth file: unlike
    # regular Python, it does not add the executed script's directory.
    sys.path.insert(0, str(Path(__file__).resolve().parent))
    from companion_core import DesktopBridge, ViewAssembler, decode_view, proposal_valid, valid_death_notice

LOG = logging.getLogger("soul-link")
LOCAL = struct.Struct("<4sBBHI64s")
START, DATA, FINISH, CANCEL, READY, DISCONNECT, ACK, HEARTBEAT = range(1, 9)
MAX_MESSAGE = 16384
WINDOW = 32


def local_frame(kind, role, epoch, payload=b""):
    if len(payload) > 64:
        raise ValueError("oversized local payload")
    return LOCAL.pack(b"SLN1", kind, role, len(payload), epoch, payload)


def unpack_local(frame):
    magic, kind, role, size, epoch, data = LOCAL.unpack(frame)
    if magic != b"SLN1" or role not in (0, 1) or size > 64 or kind not in range(1, 12):
        raise ValueError("invalid local frame")
    if any(data[size:]):
        raise ValueError("nonzero local padding")
    return kind, role, epoch, data[:size]


def packet_sequence(packet):
    if len(packet) != 64:
        raise ValueError("invalid packet length")
    return struct.unpack_from("<H", packet, 12)[0]


def encode(message):
    body = json.dumps(message, separators=(",", ":")).encode("ascii")
    if len(body) > MAX_MESSAGE:
        raise ValueError("oversized network message")
    return struct.pack("!I", len(body)) + body


async def receive(reader):
    size = struct.unpack("!I", await reader.readexactly(4))[0]
    if not 0 < size <= MAX_MESSAGE:
        raise ValueError("invalid network size")
    message = json.loads(await reader.readexactly(size))
    if not isinstance(message, dict) or not isinstance(message.get("type"), str):
        raise ValueError("invalid network message")
    return message


async def send(writer, message):
    writer.write(encode(message))
    await asyncio.wait_for(writer.drain(), 10)


def tune(writer):
    sock = writer.get_extra_info("socket")
    if sock:
        sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_KEEPALIVE, 1)


class Peer:
    def __init__(self, role, writer):
        self.role, self.writer = role, writer
        self.epoch = 0
        self.sequence = 0
        self.pending = deque()
        self.stream_type = None
        self.finished = False
        self.build = None
        self.view = None
        self.view_data = None
        self.view_build = None
        self.active = False
        self.view_time = 0


class Relay:
    def __init__(self, secret, progress_timeout=120, join_timeout=300):
        self.secret = secret
        self.peers = {}
        self.session = None
        self.progress = time.monotonic()
        self.started = None
        self.progress_timeout, self.join_timeout = progress_timeout, join_timeout
        self.lock = asyncio.Lock()
        self.proposal = None

    async def reset(self, reason):
        self.session = None
        self.started = None
        if self.proposal: await self.close_proposal('Connection interrupted. Propose your team again after reconnecting.')
        for peer in list(self.peers.values()):
            epoch = peer.epoch
            peer.epoch, peer.finished, peer.sequence = 0, False, 0
            peer.pending.clear()
            peer.stream_type = None
            if epoch:
                with contextlib.suppress(ConnectionError, asyncio.TimeoutError):
                    await send(peer.writer, {"type": "abort", "epoch": epoch, "reason": reason})
        LOG.info("Sync interrupted: %s", reason)

    async def handle(self, reader, writer, hello=None):
        peer = None
        tune(writer)
        try:
            if hello is None:
                hello = await asyncio.wait_for(receive(reader), 10)
            role = hello.get("role")
            supplied = hello.get("secret", "")
            if (hello.get("type") != "join" or hello.get("version") != 1
                    or type(role) is not int or role not in (0, 1)
                    or not isinstance(supplied, str)
                    or not hmac.compare_digest(supplied.encode(), self.secret.encode())):
                raise ValueError("authentication failed")
            async with self.lock:
                if role in self.peers:
                    raise ValueError("role already connected")
                peer = Peer(role, writer)
                self.peers[role] = peer
                await send(writer, {"type": "joined"})
                other = self.peers.get(role ^ 1)
                if other and other.view_data:
                    await send(writer, {"type": "view", "data": other.view_data, "build": other.view_build})
            LOG.info("Player %s connected", "AB"[role])
            while True:
                message = await asyncio.wait_for(receive(reader), 20)
                async with self.lock:
                    await self.message(peer, message)
        except (ConnectionError, asyncio.IncompleteReadError, asyncio.TimeoutError, ValueError,
                KeyError, TypeError, UnicodeError) as error:
            LOG.info("Connection closed (%s): %s", type(error).__name__, error)
        finally:
            async with self.lock:
                if peer and self.peers.get(peer.role) is peer:
                    del self.peers[peer.role]
                    await self.reset("connection lost")
            writer.close()
            with contextlib.suppress(ConnectionError):
                await writer.wait_closed()

    async def message(self, peer, message):
        kind = message["type"]
        if kind == "ping":
            peer.active = message.get('active') is True
            other = self.peers.get(peer.role ^ 1)
            response = {"type": "pong"}
            if 'active' in message: response.update(peerConnected=other is not None,peerActive=other.active if other else False)
            await send(peer.writer, response)
            await self.apply_proposal()
            return
        if kind in ('view','sync_request','propose','vote','death_notice'):
            await self.desktop_message(peer,message)
            return
        if kind == "unavailable":
            peer.view = None
            peer.view_data = None
            peer.active = False
            await self.reset("emulator disconnected")
            return
        if kind == "start":
            epoch = message.get("epoch")
            build = message.get("build")
            if (type(epoch) is not int or not 0 < epoch <= 0xFFFFFFFF
                    or not isinstance(build, str) or len(build) != 8
                    or any(c not in "0123456789abcdef" for c in build)):
                raise ValueError("invalid epoch")
            if self.session or peer.epoch:
                await self.reset("nouvelle tentative")
            peer.epoch, peer.sequence, peer.finished = epoch, 0, False
            peer.pending.clear()
            peer.stream_type = None
            peer.build = build
            self.progress = time.monotonic()
            if len(self.peers) == 2 and all(p.epoch for p in self.peers.values()):
                if self.peers[0].build != self.peers[1].build:
                    await self.reset("different ROM versions")
                    return
                self.session = secrets.token_hex(16)
                self.started = time.monotonic()
                for p in self.peers.values():
                    await send(p.writer, {"type": "begin", "epoch": p.epoch, "session": self.session})
                LOG.info("Both players ready; sync started")
            return
        # Stale frames from a previous epoch/session can never enter the ROM.
        if message.get("epoch") != peer.epoch or not peer.epoch:
            return
        if kind == "cancel":
            await self.reset("annulation locale")
            return
        if not self.session or message.get("session") != self.session:
            return
        other = self.peers[peer.role ^ 1]
        if kind == "data":
            packet = base64.b64decode(message["packet"], validate=True)
            seq = packet_sequence(packet)
            packet_type = packet[17]
            if (seq != peer.sequence or len(peer.pending) >= WINDOW or peer.finished
                    or (peer.pending and (packet_type not in (2, 4) or packet_type != peer.stream_type))):
                raise ValueError("out of order packet")
            if packet[16] != peer.role:
                raise ValueError("wrong ROM player role")
            peer.pending.append(seq)
            peer.sequence += 1
            peer.stream_type = packet_type
            await send(other.writer, {"type": "data", "epoch": other.epoch,
                                     "session": self.session, "packet": message["packet"]})
        elif kind == "delivered":
            seq = message.get("sequence")
            if type(seq) is not int or not other.pending or other.pending[0] != seq:
                raise ValueError("unexpected delivery acknowledgement")
            other.pending.popleft()
            await send(other.writer, {"type": "ack", "epoch": other.epoch,
                                     "session": self.session, "sequence": seq})
        elif kind == "finish":
            if peer.pending:
                raise ValueError("undelivered final packet")
            peer.finished = True
            if other.finished:
                LOG.info("Both games synced (%.2f s)", time.monotonic() - self.started)
                self.session = None
                self.started = None
                for p in self.peers.values():
                    p.epoch, p.sequence, p.finished = 0, 0, False
                    p.pending.clear()
                    p.stream_type = None
        else:
            raise ValueError("unsupported network message")
        self.progress = time.monotonic()

    async def close_proposal(self, reason):
        self.proposal = None
        for p in list(self.peers.values()):
            await send(p.writer, {'type':'proposal_closed','reason':reason})

    async def apply_proposal(self):
        if not self.proposal: return
        if time.monotonic()-self.proposal['created']>120:
            await self.close_proposal('The proposal has expired. You can make a new one.'); return
        views=[self.peers.get(i).view if i in self.peers else None for i in (0,1)]
        if not proposal_valid(self.proposal['data'],views):
            await self.close_proposal('The Pokémon have changed. Check the pairs and propose a new team.'); return
        if all(self.proposal['votes']) and not self.session and all(p.active and p.view['safe'] for p in self.peers.values()):
            data=self.proposal['data']; self.proposal=None
            for p in list(self.peers.values()): await send(p.writer,{'type':'apply_team','proposal':data})

    async def desktop_message(self, peer, message):
        kind=message['type']; other=self.peers.get(peer.role ^ 1)
        if kind=='death_notice':
            event=message.get('event');build=message.get('build')
            if not valid_death_notice(event) or event['role']!=peer.role or not isinstance(build,str) or len(build)!=8 or any(x not in '0123456789abcdef' for x in build): raise ValueError('invalid death notification')
            if other: await send(other.writer,dict(type=kind,event=event,build=build))
        elif kind=='view':
            data=message.get('data'); build=message.get('build')
            if not isinstance(data,str) or len(data)>4000 or not isinstance(build,str) or len(build)!=8 or any(x not in '0123456789abcdef' for x in build): raise ValueError('invalid desktop view')
            view=decode_view(base64.b64decode(data,validate=True))
            if view['player'] not in (peer.role,255) and any(view['run']):
                # Report a wrong save role to the UI; native protocol still refuses it.
                pass
            peer.view,peer.view_data,peer.view_build,peer.active,peer.view_time=view,data,build,True,time.monotonic()
            if other: await send(other.writer,{'type':'view','data':data,'build':build})
            await self.apply_proposal()
        elif kind=='sync_request':
            for p in list(self.peers.values()): await send(p.writer,{'type':'sync_request'})
        elif kind=='propose':
            if self.proposal:
                await send(peer.writer,{'type':'notice','reason':'A proposal is already waiting. Accept or decline it before making another.'}); return
            proposal=message.get('proposal')
            views=[self.peers.get(i).view if i in self.peers else None for i in (0,1)]
            if not other or peer.view_build!=other.view_build or not proposal_valid(proposal,views):
                await send(peer.writer,{'type':'notice','reason':'This team cannot be proposed. Click Sync, then pick one to six living pairs.'}); return
            proposal=dict(proposal); proposal['author']=peer.role
            self.proposal=dict(data=proposal,votes=[False,False],created=time.monotonic()); self.proposal['votes'][peer.role]=True
            for p in list(self.peers.values()): await send(p.writer,{'type':'proposal','proposal':proposal})
        elif kind=='vote':
            if not self.proposal or message.get('id')!=self.proposal['data']['id'] or type(message.get('accept'))!=bool:
                await send(peer.writer,{'type':'notice','reason':'The proposal has expired.'}); return
            if not message['accept']:
                await self.close_proposal('The proposal was declined. Both teams stay as they are.'); return
            self.proposal['votes'][peer.role]=True
            await self.apply_proposal()

    async def watchdog(self):
        while True:
            await asyncio.sleep(1)
            async with self.lock:
                deadline = self.progress_timeout if self.session else self.join_timeout
                if any(p.epoch for p in self.peers.values()) and time.monotonic() - self.progress > deadline:
                    await self.reset("progress timeout")


class Companion:
    def __init__(self, host, port, role, secret, local_timeout=20, tls=False, ui_root=None):
        self.host, self.port, self.role, self.secret = host, port, role, secret
        self.local = None
        self.wan = None
        self.desktop = DesktopBridge(self, ui_root) if ui_root else None
        self.view_assembler = ViewAssembler()
        self.last_bad_view_log = 0
        self.joined = False
        self.epoch = 0
        self.session = None
        self.pending_tx = deque()
        self.pending_rx = deque()
        self.local_timeout = local_timeout
        self.ping_started = None
        self.connections = 0
        self.join_started = 0
        self.join_timeout = 10
        # Public relays always verify the certificate and its hostname.
        self.tls_context = ssl.create_default_context(
            cafile=Path(__file__).with_name('relay-ca.pem')) if tls else None

    def clear_attempt(self):
        self.epoch, self.session = 0, None
        self.join_started = 0
        self.pending_tx.clear()
        self.pending_rx.clear()

    async def local_send(self, kind, payload=b""):
        if self.local:
            self.local.write(local_frame(kind, self.role, self.epoch, payload))
            await asyncio.wait_for(self.local.drain(), 10)

    async def abort(self):
        await self.local_send(DISCONNECT)
        self.clear_attempt()

    async def local_handle(self, reader, writer):
        if self.local:
            writer.close()
            return
        self.local = writer
        tune(writer)
        LOG.info("mGBA script connected")
        try:
            while True:
                try:
                    frame = await asyncio.wait_for(reader.readexactly(LOCAL.size), self.local_timeout)
                except asyncio.TimeoutError:
                    # Lua frame callbacks stop while mGBA is paused or its
                    # settings are open. Idle local silence is not a lost peer.
                    # An active sync still aborts on the same finite deadline.
                    if self.epoch:
                        raise
                    continue
                kind, role, epoch, payload = unpack_local(frame)
                if role != self.role:
                    raise ValueError("wrong script role")
                if kind == 9:
                    # A read-only UI snapshot must not abort the native exchange.
                    # Discard an incomplete/transient view and await a fresh one.
                    try:
                        snapshot = self.view_assembler.add(payload)
                        if snapshot is not None and self.desktop: await self.desktop.snapshot(snapshot)
                    except ValueError as error:
                        self.view_assembler = ViewAssembler()
                        if time.monotonic() - self.last_bad_view_log > 10:
                            LOG.warning("Companion reading ignored: %s", error)
                            self.last_bad_view_log = time.monotonic()
                    continue
                if kind == HEARTBEAT:
                    if self.desktop: self.desktop.last_local_beat = time.monotonic()
                    await self.local_send(HEARTBEAT)
                    continue
                if kind == START:
                    if len(payload) != 4:
                        raise ValueError("missing ROM fingerprint")
                    if self.epoch:
                        await self.wan_send("cancel")
                        await self.abort()
                    self.epoch = epoch
                    self.join_started = time.monotonic()
                    if self.desktop: self.desktop.metrics.mark("startToBegin")
                    if not self.joined:
                        await self.abort()
                    else:
                        await self.wan_send("start", build=payload.hex())
                    continue
                if epoch != self.epoch or not self.epoch:
                    continue
                if kind == DATA:
                    seq = packet_sequence(payload)
                    if not self.session or len(self.pending_tx) >= WINDOW:
                        raise ValueError("unexpected local packet")
                    self.pending_tx.append(seq)
                    await self.wan_send("data", packet=base64.b64encode(payload).decode("ascii"))
                elif kind == ACK:
                    if (len(payload) != 4 or not self.pending_rx
                            or struct.unpack("<I", payload)[0] != self.pending_rx[0]):
                        raise ValueError("unexpected local delivery")
                    await self.wan_send("delivered", sequence=self.pending_rx.popleft())
                elif kind in (FINISH, CANCEL):
                    if kind == FINISH and self.desktop:
                        self.desktop.metrics.finish("transferToFinish")
                        self.desktop.awaiting_completion = True
                        self.desktop.completion_until = time.monotonic()+2
                    await self.wan_send("finish" if kind == FINISH else "cancel")
                    self.clear_attempt()
                else:
                    raise ValueError("unsupported local message")
        except (ConnectionError, asyncio.IncompleteReadError, asyncio.TimeoutError, ValueError) as error:
            LOG.info("mGBA bridge closed (%s): %s", type(error).__name__, error)
            with contextlib.suppress(ConnectionError, asyncio.TimeoutError):
                if self.epoch:
                    await self.wan_send("cancel")
        finally:
            if self.joined:
                with contextlib.suppress(ConnectionError, asyncio.TimeoutError):
                    await self.wan_send("unavailable")
            self.local = None
            self.clear_attempt()
            writer.close()

    async def wan_send(self, kind, **fields):
        if not self.wan or not self.joined:
            raise ConnectionError("relay unavailable")
        message={"type": kind, "epoch": self.epoch, "session": self.session, **fields}
        await send(self.wan, message)
        if self.desktop: self.desktop.metrics.sent_bytes+=len(encode(message))

    async def heartbeat(self):
        last_ping=time.monotonic()
        while True:
            await asyncio.sleep(1)
            await self.check_join_deadline()
            if time.monotonic()-last_ping>=5:
                self.ping_started=time.monotonic()
                await self.wan_send("ping", active=self.desktop.active() if self.desktop else False)
                last_ping=time.monotonic()

    async def connection_heartbeat(self, writer):
        # A failed heartbeat must also wake the receive loop. Closing this
        # connection immediately avoids waiting for the receive timeout (or TLS shutdown).
        try: await self.heartbeat()
        finally: writer.transport.abort()

    async def check_join_deadline(self):
        if (self.epoch and self.session is None and self.join_started
                and time.monotonic()-self.join_started>=self.join_timeout):
            LOG.info('Partner did not join sync within %ss; releasing the game',self.join_timeout)
            try: await self.wan_send('cancel')
            finally: await self.abort()

    async def connect_forever(self):
        delay = 1
        while True:
            heartbeat = None
            writer = None
            try:
                reader, writer = await asyncio.wait_for(asyncio.open_connection(
                    self.host, self.port, ssl=self.tls_context), 10)
                tune(writer)
                self.wan = writer
                await send(writer, {"type": "join", "version": 1, "role": self.role, "secret": self.secret})
                if (await asyncio.wait_for(receive(reader), 10))["type"] != "joined":
                    raise ValueError("join refused")
                self.joined = True
                self.connections += 1
                if self.desktop: self.desktop.metrics.reconnects=max(0,self.connections-1)
                if self.desktop:
                    self.desktop.last_wan_view = None
                    self.desktop.last_death_sent = 0
                    if self.desktop.accepted_team:
                        self.desktop.team_recovery=True
                        self.desktop.team_recover_at=time.monotonic()+(0 if self.role==0 else 3)
                    self.desktop.wakeup.set()
                delay = 1
                LOG.info("Relay connected; player %s. Companion ready.", "AB"[self.role])
                heartbeat = asyncio.create_task(self.connection_heartbeat(writer))
                while True:
                    message = await asyncio.wait_for(receive(reader), 20)
                    if self.desktop: self.desktop.metrics.received_bytes+=len(encode(message))
                    kind = message["type"]
                    if kind == "pong" and self.ping_started is not None:
                        if self.desktop: self.desktop.metrics.add("relayRoundTrip",(time.monotonic()-self.ping_started)*1000)
                        self.ping_started=None
                    if kind in ('pong','view','sync_request','proposal','proposal_closed','apply_team','notice','death_notice'):
                        if self.desktop: await self.desktop.message(message)
                        continue
                    if message.get("epoch") != self.epoch or not self.epoch:
                        continue
                    if kind == "abort":
                        LOG.info("Rejected/interrupted: %s", message.get("reason", ""))
                        await self.abort()
                    elif kind == "begin":
                        if not isinstance(message.get("session"), str) or len(message["session"]) != 32:
                            raise ValueError("invalid session token")
                        self.session = message["session"]
                        if self.desktop:
                            self.desktop.metrics.finish("startToBegin")
                            self.desktop.metrics.mark("transferToFinish")
                        await self.local_send(READY)
                    elif message.get("session") == self.session and self.session:
                        if kind == "data":
                            packet = base64.b64decode(message["packet"], validate=True)
                            if len(self.pending_rx) >= WINDOW:
                                raise ValueError("inbox overflow")
                            self.pending_rx.append(packet_sequence(packet))
                            await self.local_send(DATA, packet)
                        elif kind == "ack":
                            seq = message.get("sequence")
                            if type(seq) is not int or not self.pending_tx or seq != self.pending_tx[0]:
                                raise ValueError("invalid sender acknowledgement")
                            self.pending_tx.popleft()
                            await self.local_send(ACK, struct.pack("<I", seq))
                        else:
                            raise ValueError("unsupported relay message")
            except (ConnectionError, asyncio.IncompleteReadError, asyncio.TimeoutError,
                    ValueError, KeyError, TypeError, UnicodeError, OSError) as error:
                LOG.info("Relay unavailable (%s), reconnecting in %ss", type(error).__name__, delay)
            finally:
                self.joined = False
                if self.desktop: self.desktop.relay_disconnected()
                self.ping_started = None
                if self.desktop:
                    for stage in ("startToBegin","transferToFinish","commandToAcknowledgement"):
                        self.desktop.metrics.started.pop(stage,None)
                self.wan = None
                if heartbeat:
                    heartbeat.cancel()
                    with contextlib.suppress(asyncio.CancelledError, OSError, asyncio.TimeoutError):
                        await heartbeat
                with contextlib.suppress(ConnectionError, asyncio.TimeoutError):
                    await self.abort()
                if writer:
                    writer.close()
            await asyncio.sleep(delay)
            delay = min(delay * 2, 8)


async def watch_launcher(pid):
    """Hold a Windows process handle so parent PID reuse cannot keep us alive."""
    if sys.platform != 'win32' or pid <= 0 or pid == os.getpid():
        raise ValueError('Invalid Windows launcher PID.')
    import ctypes
    from ctypes import wintypes
    kernel = ctypes.WinDLL('kernel32', use_last_error=True)
    kernel.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
    kernel.OpenProcess.restype = wintypes.HANDLE
    kernel.WaitForSingleObject.argtypes = [wintypes.HANDLE, wintypes.DWORD]
    kernel.WaitForSingleObject.restype = wintypes.DWORD
    kernel.CloseHandle.argtypes = [wintypes.HANDLE]
    kernel.CloseHandle.restype = wintypes.BOOL
    handle = kernel.OpenProcess(0x100000 | 0x1000, False, pid)
    if not handle:
        if ctypes.get_last_error() == 87: return
        raise ctypes.WinError(ctypes.get_last_error())
    try:
        while True:
            status = kernel.WaitForSingleObject(handle, 0)
            if status == 0:
                LOG.info('Launcher closed; stopping the network companion')
                return
            if status != 258: raise ctypes.WinError(ctypes.get_last_error())
            await asyncio.sleep(.2)
    finally:
        kernel.CloseHandle(handle)


async def run(args):
    if args.tls:
        if args.host:
            raise ValueError("The public relay runs on the VPS, not on the player PC.")
    else:
        address = ipaddress.ip_address(args.server)
        if not (address.is_loopback or address in ipaddress.ip_network("100.64.0.0/10")):
            raise ValueError("A public relay requires --tls.")
    secret = args.secret_file.read_text(encoding="utf-8").strip()
    if not 20 <= len(secret) <= 128 or not secret.isascii():
        raise ValueError("The secret file needs an ASCII shared code of 20 to 128 characters.")
    role = "AB".index(args.role)
    companion = Companion(args.server, args.port, role, secret, tls=args.tls, ui_root=args.ui_root)
    tasks = [asyncio.create_task(companion.connect_forever())]
    if companion.desktop: tasks.append(asyncio.create_task(companion.desktop.run()))
    servers = [await asyncio.start_server(companion.local_handle, "127.0.0.1", args.local_port)]
    web = None
    if companion.desktop and (args.ui_root / 'companion-web/index.html').is_file():
        try:
            from .companion_web import CompanionWeb
        except ImportError:
            from companion_web import CompanionWeb
        web = await CompanionWeb(args.ui_root, role).start()
    if args.host:
        relay = Relay(secret)
        servers.append(await asyncio.start_server(relay.handle, args.server, args.port))
        tasks.append(asyncio.create_task(relay.watchdog()))
    LOG.info("Local mGBA bridge: 127.0.0.1:%s", args.local_port)
    if getattr(args, 'launcher_pid', None): tasks.append(asyncio.create_task(watch_launcher(args.launcher_pid)))
    try:
        done, _ = await asyncio.wait(tasks, return_when=asyncio.FIRST_COMPLETED)
        for task in done: task.result()
    finally:
        if web: await web.close()
        for task in tasks:
            task.cancel()
        await asyncio.gather(*tasks, return_exceptions=True)
        if companion.local: companion.local.close()
        for server in servers:
            server.close()
            await server.wait_closed()


def main():
    from pathlib import Path
    import sys
    for stream in (sys.stdout, sys.stderr):
        if hasattr(stream, "reconfigure"):
            stream.reconfigure(encoding="utf-8", errors="backslashreplace")
    parser = argparse.ArgumentParser(description="Soul Link — mGBA network companion")
    parser.add_argument("--ui-root", type=Path, help="private companion folder")
    parser.add_argument("--host", action="store_true", help="also host the relay")
    parser.add_argument("--tls", action="store_true", help="VPS relay with verified TLS certificate")
    parser.add_argument("--server", required=True, help="VPS relay address or hostname")
    parser.add_argument("--role", choices=("A", "B"), required=True)
    parser.add_argument("--secret-file", type=Path, required=True)
    parser.add_argument("--port", type=int, default=8765)
    parser.add_argument("--local-port", type=int, default=8766)
    parser.add_argument("--launcher-pid", type=int, help="stop when this Windows launcher process exits")
    args = parser.parse_args()
    logging.basicConfig(level=logging.INFO, format="%(asctime)s %(message)s", datefmt="%H:%M:%S")
    try:
        asyncio.run(run(args))
    except KeyboardInterrupt:
        pass
    except (ValueError, OSError) as error:
        LOG.error("Cannot configure: %s", error)
        raise SystemExit(1)


if __name__ == "__main__":
    main()
