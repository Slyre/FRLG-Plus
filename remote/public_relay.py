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
    def __init__(self, max_rooms=128, max_connections=256, idle_timeout=600):
        self.max_rooms = max_rooms
        self.max_connections = max_connections
        self.idle_timeout = idle_timeout
        self.connections = 0
        self.rooms = {}

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
            if (hello.get("type") != "join" or hello.get("version") != 1
                    or type(role) is not int or role not in (0, 1)
                    or not isinstance(secret, str)
                    or re.fullmatch(r"[0-9a-f]{48}", secret) is None):
                raise ValueError("Invalid invitation")
            entry = self.rooms.get(secret)
            if entry is None:
                if role != 0:
                    raise ValueError("The creator must open the room")
                if len(self.rooms) >= self.max_rooms:
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
        for entry in self.rooms.values():
            entry["watchdog"].cancel()
        for entry in self.rooms.values():
            with contextlib.suppress(asyncio.CancelledError):
                await entry["watchdog"]


async def run(args):
    tls = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    tls.minimum_version = ssl.TLSVersion.TLSv1_2
    tls.load_cert_chain(args.cert, args.key)
    rooms = Rooms(args.max_rooms, args.max_connections)
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
