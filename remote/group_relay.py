"""Isolated schema-2 four-player relay; no listeners or TLS policy at import.

The public TLS entry point can dispatch validated v2 joins to GroupRooms while
keeping its v1 two-player implementation unchanged. Join fields are exactly
type/version/players/role/secret/run/participants/build/client. Role -1 requests
automatic assignment; the local installation credential retains its seat. Every later message
carries version=2 and the fresh per-connection session token returned by joined.

Commands: ping(active), view(sequence,snapshot), sync(),
propose(id,ids,context), vote(id,accept,context), confirm(id,context).
Snapshots are owner-only group_core schema 2. Full state updates coalesce, so a
slow recipient cannot hold the group lock or delay the other three players.
Votes survive disconnects in the live room. Durable client receipts and the
native confirmation check are responsibilities of the integrating companion.
"""
import asyncio
from collections import deque
import contextlib
import copy
import hmac
import re
import secrets
import struct
import time

try:
    from .group_core import GroupState, VERSION, MAX_RECORD, canonical, parse_json, digest
except ImportError:
    from group_core import GroupState, VERSION, MAX_RECORD, canonical, parse_json, digest

MAX_MESSAGE = MAX_RECORD


def encode(message):
    body = canonical(message)
    if not 0 < len(body) <= MAX_MESSAGE:
        raise ValueError('invalid four-player message size')
    return struct.pack('!I', len(body)) + body


async def receive(reader):
    size = struct.unpack('!I', await reader.readexactly(4))[0]
    if not 0 < size <= MAX_MESSAGE:
        raise ValueError('invalid four-player message size')
    message = parse_json(await reader.readexactly(size), MAX_MESSAGE)
    if not isinstance(message, dict) or not isinstance(message.get('type'), str):
        raise ValueError('invalid four-player message')
    return message


def validate_join(hello):
    required = {'type', 'version', 'players', 'role', 'secret', 'run', 'participants', 'build', 'client'}
    if (not isinstance(hello, dict) or set(hello) != required or hello['type'] != 'join'
            or type(hello['version']) is not int or hello['version'] != VERSION
            or type(hello['players']) is not int or hello['players'] != 4
            or type(hello['role']) is not int or hello['role'] not in (-1, 0, 1, 2, 3)
            or not isinstance(hello['secret'], str) or re.fullmatch('[0-9a-f]{48}', hello['secret']) is None):
        raise ValueError('invalid four-player invitation')
    if (not isinstance(hello['client'], str) or re.fullmatch('[0-9a-f]{32}', hello['client']) is None):
        raise ValueError('invalid local seat credential')
    # Centralized validation of run, participants and build, without I/O.
    GroupState(hello['secret'], hello['run'], hello['participants'], hello['build'])
    return hello


class Outbox:
    def __init__(self, writer, limit=16, drain_timeout=10):
        self.writer, self.limit, self.drain_timeout = writer, limit, drain_timeout
        self.control = deque()
        self.latest_state = None
        self.ready = asyncio.Event()
        self.closed = False

    def put(self, frame, state=False):
        if self.closed:
            return False
        if state:
            self.latest_state = frame
        elif len(self.control) < self.limit:
            self.control.append(frame)
        else:
            self.close()
            return False
        self.ready.set()
        return True

    def close(self):
        self.closed = True
        self.control.clear()
        self.latest_state = None
        self.ready.set()
        self.writer.transport.abort()

    async def run(self):
        try:
            while not self.closed:
                if self.control:
                    frame = self.control.popleft()
                elif self.latest_state is not None:
                    frame, self.latest_state = self.latest_state, None
                else:
                    self.ready.clear()
                    await self.ready.wait()
                    continue
                self.writer.write(frame)
                await asyncio.wait_for(self.writer.drain(), self.drain_timeout)
        except (OSError, asyncio.TimeoutError):
            pass
        finally:
            self.close()  # Also wakes the matching receive loop on failure.


class GroupPeer:
    def __init__(self, role, writer, drain_timeout=10):
        self.role = role
        self.session = secrets.token_hex(16)
        self.outbox = Outbox(writer, drain_timeout=drain_timeout)
        self.sequence, self.snapshot_digest = -1, None
        self.fresh, self.active = False, False


class GroupRelay:
    def __init__(self, secret, run, participants, build, species_types=None, receive_timeout=20, drain_timeout=10):
        self.secret = secret
        self.domain = GroupState(secret, run, participants, build, species_types)
        self.peers = {}
        self.claims = {}
        self.lock = asyncio.Lock()
        self.receive_timeout, self.drain_timeout = receive_timeout, drain_timeout

    def state(self):
        result = self.domain.state()
        for player in result['players']:
            peer = self.peers.get(player['slot'])
            player['connected'] = peer is not None
            player['active'] = bool(peer and peer.fresh and peer.active)
        return result

    def broadcast(self):
        # Encode once; each outbox only retains the most recent shared bytes.
        frame = encode(dict(type='state', version=VERSION, state=self.state()))
        for peer in self.peers.values():
            peer.outbox.put(frame, state=True)

    def message(self, peer, message):
        if (type(message.get('version')) is not int or message['version'] != VERSION
                or message.get('session') != peer.session):
            raise ValueError('stale or foreign four-player session')
        kind = message['type']
        fields = {'ping': {'active'}, 'view': {'sequence', 'snapshot'}, 'sync': set(),
                  'propose': {'id', 'ids', 'context'}, 'vote': {'id', 'accept', 'context'},
                  'confirm': {'id', 'context'}}
        if kind not in fields or set(message) != {'type', 'version', 'session'} | fields[kind]:
            raise ValueError('invalid four-player command fields')
        if kind == 'ping':
            if type(message['active']) is not bool:
                raise ValueError('invalid player activity')
            peer.active = message['active']
            peer.outbox.put(encode(dict(type='pong', version=VERSION)))
        elif kind == 'view':
            sequence = message['sequence']
            if type(sequence) is not int or not 0 <= sequence <= 0xFFFFFFFF:
                raise ValueError('invalid owner sequence')
            fingerprint = digest(message['snapshot'])
            if sequence < peer.sequence:
                raise ValueError('out-of-order owner reading')
            if sequence == peer.sequence:
                if fingerprint != peer.snapshot_digest:
                    raise ValueError('owner sequence reused for different content')
                return
            self.domain.observe(peer.role, message['snapshot'])
            peer.sequence, peer.snapshot_digest = sequence, fingerprint
            peer.fresh, peer.active = True, True
        elif kind in ('propose', 'vote', 'confirm'):
            if message['context'] != self.domain.context():
                raise ValueError('the group context has changed')
            if kind in ('propose', 'vote') and not all(p in self.peers and self.peers[p].fresh for p in range(4)):
                raise ValueError('fresh readings from all four players are required')
            if kind == 'propose':
                self.domain.propose(peer.role, message['id'], message['ids'])
            elif kind == 'vote':
                self.domain.vote(peer.role, message['id'], message['accept'])
            else:
                if not peer.fresh:
                    raise ValueError('a fresh owner reading is required for confirmation')
                self.domain.confirm(peer.role, message['id'])
        self.broadcast()

    async def handle(self, reader, writer, hello=None):
        peer = None
        sender = None
        try:
            if hello is None:
                hello = await asyncio.wait_for(receive(reader), 5)
            validate_join(hello)
            if (not hmac.compare_digest(hello['secret'], self.secret)
                    or hello['run'] != self.domain.run or hello['participants'] != self.domain.participants
                    or hello['build'] != self.domain.build):
                raise ValueError('another room, run or build')
            async with self.lock:
                role = hello['role']
                credential = hello['client']
                known = next((slot for slot, claim in self.claims.items() if claim == credential), None)
                if role >= 0 and known is not None and role != known:
                    raise ValueError('installation cannot claim another trainer seat')
                if role == -1:
                    role = next((slot for slot, claim in self.claims.items() if claim == credential), -1)
                    if role == -1:
                        role = next((slot for slot in range(1, 4) if slot not in self.claims and slot not in self.peers), -1)
                    if role == -1:
                        raise ValueError('the four trainer seats are already assigned')
                if role in self.claims and self.claims[role] != credential:
                    raise ValueError('this trainer seat belongs to another installation')
                if role in self.peers:
                    raise ValueError('player slot is already connected')
                if credential is not None:
                    self.claims[role] = credential
                peer = GroupPeer(role, writer, self.drain_timeout)
                self.peers[peer.role] = peer
                peer.outbox.put(encode(dict(type='joined', version=VERSION, players=4,
                                           role=peer.role, session=peer.session)))
                self.broadcast()
            sender = asyncio.create_task(peer.outbox.run())
            while True:
                message = await asyncio.wait_for(receive(reader), self.receive_timeout)
                async with self.lock:
                    try:
                        self.message(peer, message)
                    except ValueError as error:
                        # An expired UI action need not disconnect the game.
                        peer.outbox.put(encode(dict(type='error', version=VERSION, reason=str(error))))
        except (OSError, asyncio.IncompleteReadError, asyncio.TimeoutError, ValueError):
            pass
        finally:
            async with self.lock:
                if peer and self.peers.get(peer.role) is peer:
                    del self.peers[peer.role]
                    self.broadcast()
            if sender:
                sender.cancel()
                with contextlib.suppress(asyncio.CancelledError):
                    await sender
            writer.close()
            with contextlib.suppress(OSError, asyncio.TimeoutError):
                await asyncio.wait_for(writer.wait_closed(), 2)

    def close(self):
        for peer in list(self.peers.values()):
            peer.outbox.close()


class GroupRooms:
    """Standalone v2 dispatcher for later composition with the public TLS server."""
    def __init__(self, max_rooms=128, max_connections=256, idle_timeout=600, species_types=None):
        self.max_rooms, self.max_connections, self.idle_timeout = max_rooms, max_connections, idle_timeout
        self.species_types = species_types
        self.connections = 0
        self.rooms = {}

    async def handle(self, reader, writer, hello=None):
        if self.connections >= self.max_connections:
            writer.close()
            return
        self.connections += 1
        entry = None
        try:
            if hello is None:
                hello = await asyncio.wait_for(receive(reader), 5)
            validate_join(hello)
            entry = self.rooms.get(hello['secret'])
            if entry is None:
                if hello['role'] != 0 or len(self.rooms) >= self.max_rooms:
                    raise ValueError('the creator must open an available room')
                entry = dict(relay=GroupRelay(hello['secret'], hello['run'], hello['participants'],
                                              hello['build'], self.species_types), handlers=0, last=time.monotonic())
                self.rooms[hello['secret']] = entry
            entry['handlers'] += 1
            await entry['relay'].handle(reader, writer, hello)
        except (OSError, asyncio.IncompleteReadError, asyncio.TimeoutError, ValueError):
            pass
        finally:
            if entry is not None:
                entry['handlers'] -= 1
                entry['last'] = time.monotonic()
            self.connections -= 1
            writer.close()
            with contextlib.suppress(OSError, asyncio.TimeoutError):
                await asyncio.wait_for(writer.wait_closed(), 2)

    def expire(self):
        now = time.monotonic()
        for secret, entry in list(self.rooms.items()):
            if entry['handlers'] == 0 and now - entry['last'] >= self.idle_timeout:
                del self.rooms[secret]

    def close(self):
        for entry in self.rooms.values():
            entry['relay'].close()
