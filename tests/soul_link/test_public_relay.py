import asyncio
from pathlib import Path
import ssl
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from remote.network import Companion, receive, send
from remote.public_relay import Rooms


class PublicRelayTest(unittest.IsolatedAsyncioTestCase):
    async def asyncSetUp(self):
        self.rooms = Rooms(max_rooms=2, idle_timeout=0)
        # Plaintext is used only inside this loopback test fixture.
        self.server = await asyncio.start_server(self.rooms.handle, "127.0.0.1", 0)
        self.port = self.server.sockets[0].getsockname()[1]
        self.writers = []

    async def asyncTearDown(self):
        for writer in self.writers:
            writer.close()
            await writer.wait_closed()
        for _ in range(50):
            if self.rooms.connections == 0:
                break
            await asyncio.sleep(0.01)
        self.server.close()
        await self.server.wait_closed()
        await self.rooms.close()

    async def join(self, secret, role):
        reader, writer = await asyncio.open_connection("127.0.0.1", self.port)
        self.writers.append(writer)
        await send(writer, {"type": "join", "version": 1, "secret": secret, "role": role})
        return reader, writer

    async def test_two_rooms_are_isolated(self):
        pairs = []
        for secret in ("a" * 48, "b" * 48):
            pair = []
            for role in (0, 1):
                reader, writer = await self.join(secret, role)
                self.assertEqual(await receive(reader), {"type": "joined"})
                pair.append((reader, writer))
            pairs.append(pair)
        self.assertEqual(len(self.rooms.rooms), 2)
        a_reader, a_writer = pairs[0][0]
        b_reader, b_writer = pairs[0][1]
        await send(a_writer, {"type": "start", "epoch": 1, "build": "12345678"})
        await send(b_writer, {"type": "start", "epoch": 2, "build": "12345678"})
        self.assertEqual((await asyncio.wait_for(receive(b_reader), 1))["type"], "begin")
        self.assertEqual((await asyncio.wait_for(receive(a_reader), 1))["type"], "begin")
        self.assertEqual(self.rooms.rooms["b" * 48]["relay"].peers[0].epoch, 0)
        await send(a_writer, {"type": "ping"})
        self.assertEqual(await receive(a_reader), {"type": "pong"})

    async def test_joiner_cannot_create_and_limits_apply(self):
        reader, _ = await self.join("a" * 48, 1)
        self.assertEqual(await asyncio.wait_for(reader.read(), 1), b"")
        for secret in ("a" * 48, "b" * 48):
            reader, _ = await self.join(secret, 0)
            self.assertEqual(await receive(reader), {"type": "joined"})
        reader, _ = await self.join("c" * 48, 0)
        self.assertEqual(await asyncio.wait_for(reader.read(), 1), b"")

    async def test_duplicate_role_does_not_replace_player(self):
        reader, writer = await self.join("a" * 48, 0)
        await receive(reader)
        rejected, _ = await self.join("a" * 48, 0)
        self.assertEqual(await asyncio.wait_for(rejected.read(), 1), b"")
        await send(writer, {"type": "ping"})
        self.assertEqual(await receive(reader), {"type": "pong"})

    async def test_expiry_reclaims_empty_rooms(self):
        reader, writer = await self.join("a" * 48, 0)
        await receive(reader)
        await self.rooms.expire()
        self.assertEqual(len(self.rooms.rooms), 1)
        writer.close()
        await writer.wait_closed()
        for _ in range(50):
            if self.rooms.connections == 0:
                break
            await asyncio.sleep(0.01)
        await self.rooms.expire()
        self.assertEqual(len(self.rooms.rooms), 0)

    def test_public_client_verifies_certificates(self):
        client = Companion("soullink.example.org", 8443, 0, "a" * 48, tls=True)
        self.assertTrue(client.tls_context.check_hostname)
        self.assertEqual(client.tls_context.verify_mode, ssl.CERT_REQUIRED)


if __name__ == "__main__":
    unittest.main()
