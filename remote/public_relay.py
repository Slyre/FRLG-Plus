"""TLS relay for independent two-player Soul Link rooms. Standard library only."""
import argparse
import asyncio
import contextlib
import logging
import re
import ssl
import time
from pathlib import Path

try:
    from .network import Relay, receive
except ImportError:
    from network import Relay, receive

LOG = logging.getLogger("soul-link-vps")


class Rooms:
    def __init__(self, max_rooms=128, max_connections=256, idle_timeout=600, four_players=False, species_types=None):
        self.max_rooms = max_rooms
        self.max_connections = max_connections
        self.idle_timeout = idle_timeout
        self.connections = 0
        self.rooms = {}
        self.groups = None
        if four_players:
            try:
                from .group_relay import GroupRooms
            except ImportError:
                from group_relay import GroupRooms
            self.groups = GroupRooms(max_rooms, max_connections, idle_timeout, species_types)

    async def handle(self, reader, writer):
        if self.connections >= self.max_connections:
            writer.close()
            return
        self.connections += 1
        entry = None
        try:
            hello = await asyncio.wait_for(receive(reader), 5)
            secret = hello.get("secret")
            role = hello.get("role")
            if hello.get("version") == 2 and self.groups is not None:
                if (isinstance(secret, str) and secret not in self.groups.rooms
                        and len(self.rooms) + len(self.groups.rooms) >= self.max_rooms):
                    raise ValueError("Relay is full")
                await self.groups.handle(reader, writer, hello=hello)
                return
            if (hello.get("type") != "join" or hello.get("version") != 1
                    or type(role) is not int or role not in (0, 1)
                    or not isinstance(secret, str)
                    or re.fullmatch(r"[0-9a-f]{48}", secret) is None):
                raise ValueError("Invalid invitation")
            entry = self.rooms.get(secret)
            if entry is None:
                if role != 0:
                    raise ValueError("The creator must open the room")
                if len(self.rooms) + (len(self.groups.rooms) if self.groups else 0) >= self.max_rooms:
                    raise ValueError("Relay is full")
                relay = Relay(secret)
                entry = {"relay": relay, "last": time.monotonic(), "handlers": 0,
                         "watchdog": asyncio.create_task(relay.watchdog())}
                self.rooms[secret] = entry
            entry["handlers"] += 1
            await entry["relay"].handle(reader, writer, hello=hello)
        except (ValueError, TypeError, UnicodeError, ConnectionError,
                asyncio.IncompleteReadError, asyncio.TimeoutError):
            # Never log a room code or the supplied authentication packet.
            LOG.info("Connection refused or interrupted before admission")
        finally:
            if entry is not None:
                entry["handlers"] -= 1
                entry["last"] = time.monotonic()
            self.connections -= 1
            writer.close()
            with contextlib.suppress(ConnectionError):
                await writer.wait_closed()

    async def expire(self):
        if self.groups:
            self.groups.expire()
        now = time.monotonic()
        for secret, entry in list(self.rooms.items()):
            if entry["handlers"] == 0 and now - entry["last"] >= self.idle_timeout:
                del self.rooms[secret]
                entry["watchdog"].cancel()
                with contextlib.suppress(asyncio.CancelledError):
                    await entry["watchdog"]

    async def maintenance(self):
        while True:
            await asyncio.sleep(30)
            await self.expire()

    async def close(self):
        if self.groups:
            self.groups.close()
        for entry in self.rooms.values():
            entry["watchdog"].cancel()
        for entry in self.rooms.values():
            with contextlib.suppress(asyncio.CancelledError):
                await entry["watchdog"]


async def run(args):
    tls = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    tls.minimum_version = ssl.TLSVersion.TLSv1_2
    tls.load_cert_chain(args.cert, args.key)
    species_types = None
    if args.four_players:
        import json
        data = json.loads(args.group_species.read_text(encoding='utf-8'))
        species_types = {int(key): value['types'] for key, value in data.items() if value.get('types')}
        if any(not isinstance(types, list) or len(types) != 2
               or any(not isinstance(t, str) or not t for t in types) for types in species_types.values()):
            raise ValueError('Invalid trusted species types')
    rooms = Rooms(args.max_rooms, args.max_connections, four_players=args.four_players, species_types=species_types)
    maintenance = asyncio.create_task(rooms.maintenance())
    try:
        async with await asyncio.start_server(rooms.handle, args.bind, args.port,
                ssl=tls, ssl_handshake_timeout=5, backlog=128) as server:
            LOG.info("TLS relay ready on port %s", args.port)
            await server.serve_forever()
    finally:
        maintenance.cancel()
        with contextlib.suppress(asyncio.CancelledError):
            await maintenance
        await rooms.close()


def main():
    parser = argparse.ArgumentParser(description="Relais VPS Soul Link avec TLS")
    parser.add_argument("--bind", default="0.0.0.0")
    parser.add_argument("--port", type=int, default=443)
    parser.add_argument("--cert", type=Path, required=True)
    parser.add_argument("--key", type=Path, required=True)
    parser.add_argument("--max-rooms", type=int, default=128)
    parser.add_argument("--max-connections", type=int, default=256)
    parser.add_argument('--four-players', action='store_true', help='Enable experimental version-2 rooms alongside stable duo rooms')
    parser.add_argument('--group-species', type=Path, default=Path(__file__).with_name('companion-assets') / 'species.json',
                        help='Trusted species metadata for the exact four-player ROM build')
    args = parser.parse_args()
    if not (1 <= args.port <= 65535 and args.max_rooms > 0 and args.max_connections > 0):
        parser.error("Port ou limites invalides")
    logging.basicConfig(level=logging.INFO, format="%(asctime)s %(message)s")
    try:
        asyncio.run(run(args))
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
