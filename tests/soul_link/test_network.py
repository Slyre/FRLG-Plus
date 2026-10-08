import asyncio
import base64
from pathlib import Path
import struct
import sys
import unittest
import time
from unittest.mock import AsyncMock

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from remote.network import (Relay, Companion, LOCAL, START, DATA, FINISH, CANCEL,
                            READY, DISCONNECT, ACK, HEARTBEAT, WINDOW, encode, receive, local_frame, unpack_local)

SECRET = "test-secret-not-for-production-123456"


def packet(role, seq, kind=0):
    result = bytearray(64)
    struct.pack_into("<H", result, 12, seq)
    result[16] = role
    result[17] = kind
    return bytes(result)


class NetworkTest(unittest.IsolatedAsyncioTestCase):
    async def test_failed_heartbeat_reconnects_without_waiting_for_receive_timeout(self):
        c=self.companions[0];original=c.heartbeat;calls=0
        async def fail_once():
            nonlocal calls
            calls+=1
            if calls==1: raise ConnectionError('injected heartbeat write failure')
            await original()
        c.heartbeat=fail_once;before=c.connections;c.wan.close()
        deadline=time.monotonic()+4
        while time.monotonic()<deadline:
            if c.joined and c.connections>=before+2:break
            await asyncio.sleep(.01)
        self.assertGreaterEqual(c.connections,before+2)
        self.assertTrue(c.joined);self.assertEqual(calls,2)

    async def test_actual_relay_reconnect_does_not_reuse_previous_partner_activity(self):
        import tempfile
        from remote.companion_core import DesktopBridge,decode_view
        from test_companion import snapshot
        c=self.companions[0]
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);(root/'lgfr-sl.gba').write_bytes(b'fixture')
            c.desktop=DesktopBridge(c,root);d=c.desktop
            d.local=decode_view(snapshot());d.peer=decode_view(snapshot(1));d.peer_build=d.build
            d.peer_active=d.peer_connected=True;d.last_local_beat=time.monotonic()
            old=d.peer;connections=c.connections;c.wan.close()
            for _ in range(300):
                if c.joined and c.connections>connections:break
                await asyncio.sleep(.01)
            self.assertGreater(c.connections,connections);self.assertTrue(c.joined)
            self.assertIs(d.peer,old);self.assertTrue(d.peer_refresh_required);self.assertFalse(d.peer_active)
            await self.companions[1].wan_send('view',data=base64.b64encode(snapshot(1)).decode(),build=d.build)
            for _ in range(100):
                if not d.peer_refresh_required:break
                await asyncio.sleep(.01)
            self.assertFalse(d.peer_refresh_required);self.assertTrue(d.peer_active)
            c.desktop=None

    async def test_join_deadline_releases_game_but_never_interrupts_running_transfer(self):
        c=Companion('127.0.0.1',1,0,SECRET)
        c.wan_send=AsyncMock();c.local_send=AsyncMock()
        c.epoch=9;c.join_started=time.monotonic()-11;c.session='active-session'
        await c.check_join_deadline();c.local_send.assert_not_awaited()
        c.session=None;await c.check_join_deadline()
        c.wan_send.assert_awaited_once_with('cancel')
        c.local_send.assert_awaited_once_with(DISCONNECT)
        self.assertEqual(c.epoch,0);self.assertEqual(c.join_started,0)
    async def test_local_pause_idle_survives_but_active_sync_times_out(self):
        c = Companion('127.0.0.1', 1, 0, SECRET, local_timeout=0.02)
        server = await asyncio.start_server(c.local_handle, '127.0.0.1', 0)
        reader, writer = await asyncio.open_connection('127.0.0.1', server.sockets[0].getsockname()[1])
        try:
            # Several deadlines elapse while the emulator settings are open.
            await asyncio.sleep(0.08)
            writer.write(local_frame(HEARTBEAT, 0, 0))
            await writer.drain()
            response = await asyncio.wait_for(reader.readexactly(LOCAL.size), 0.5)
            self.assertEqual(unpack_local(response)[0], HEARTBEAT)
            self.assertIsNotNone(c.local)
            # Keeping an idle bridge alive must not weaken sync timeouts.
            c.epoch = 123
            self.assertEqual(await asyncio.wait_for(reader.read(), 0.5), b'')
            self.assertIsNone(c.local)
            self.assertEqual(c.epoch, 0)
        finally:
            writer.close()
            await writer.wait_closed()
            server.close()
            await server.wait_closed()

    async def asyncSetUp(self):
        self.relay = Relay(SECRET, progress_timeout=0.2)
        self.server = await asyncio.start_server(self.relay.handle, "127.0.0.1", 0)
        port = self.server.sockets[0].getsockname()[1]
        self.companions, self.servers, self.tasks, self.locals = [], [], [], []
        for role in range(2):
            c = Companion("127.0.0.1", port, role, SECRET)
            s = await asyncio.start_server(c.local_handle, "127.0.0.1", 0)
            self.companions.append(c)
            self.servers.append(s)
            self.tasks.append(asyncio.create_task(c.connect_forever()))
            self.locals.append(await asyncio.open_connection("127.0.0.1", s.sockets[0].getsockname()[1]))
        for _ in range(100):
            if all(c.joined for c in self.companions):
                break
            await asyncio.sleep(0.01)
        self.assertTrue(all(c.joined for c in self.companions))

    async def asyncTearDown(self):
        for _, writer in self.locals:
            writer.close()
        for task in self.tasks:
            task.cancel()
        await asyncio.gather(*self.tasks, return_exceptions=True)
        for peer in list(self.relay.peers.values()):
            peer.writer.close()
        for server in self.servers + [self.server]:
            server.close()
            await server.wait_closed()
        await asyncio.sleep(0.01)

    async def local_send(self, role, kind, epoch, body=b"", fragmented=False):
        writer = self.locals[role][1]
        frame = local_frame(kind, role, epoch, body)
        if fragmented:
            for offset in range(0, len(frame), 7):
                writer.write(frame[offset:offset+7])
                await writer.drain()
                await asyncio.sleep(0)
        else:
            writer.write(frame)
            await writer.drain()

    async def local_receive(self, role, kind, epoch):
        frame = await asyncio.wait_for(self.locals[role][0].readexactly(LOCAL.size), 3)
        actual, player, generation, body = unpack_local(frame)
        self.assertEqual((actual, player, generation), (kind, role, epoch))
        return body

    async def start(self, epochs):
        for role in range(2):
            await self.local_send(role, START, epochs[role], b"TEST", fragmented=True)
        for role in range(2):
            await self.local_receive(role, READY, epochs[role])

    async def exchange(self, epochs, seq):
        for role in range(2):
            await self.local_send(role, DATA, epochs[role], packet(role, seq), fragmented=True)
        for role in range(2):
            self.assertEqual(await self.local_receive(role, DATA, epochs[role]), packet(role ^ 1, seq))
        # No sender ACK until the other emulator actually accepts its inbox.
        self.assertTrue(all(list(c.pending_tx) == [seq] for c in self.companions))
        await asyncio.sleep(0.002 * (seq % 3))
        for role in range(2):
            await self.local_send(role, ACK, epochs[role], struct.pack("<I", seq))
        for role in range(2):
            self.assertEqual(await self.local_receive(role, ACK, epochs[role]), struct.pack("<I", seq))

    async def test_fragmented_188_rounds_and_delayed_delivery(self):
        epochs = (31, 19)
        await self.start(epochs)
        for seq in range(188):
            await self.exchange(epochs, seq)
        for role in range(2):
            await self.local_send(role, FINISH, epochs[role])
        await asyncio.sleep(0.02)
        self.assertIsNone(self.relay.session)
        self.assertTrue(all(p.epoch == 0 for p in self.relay.peers.values()))

    async def test_cancel_fresh_session_and_stale_frames(self):
        await self.start((5, 6))
        old_session = self.relay.session
        await self.exchange((5, 6), 0)
        await self.local_send(0, CANCEL, 5)
        # Initiator already tore down its local epoch, peer sees disconnect.
        await self.local_receive(1, DISCONNECT, 6)
        await self.start((7, 8))
        self.assertNotEqual(old_session, self.relay.session)
        self.companions[0].wan.write(encode({"type": "data", "epoch": 5,
            "session": old_session, "packet": base64.b64encode(packet(0, 1)).decode()}))
        await self.companions[0].wan.drain()
        await self.exchange((7, 8), 0)

    async def test_disconnection_aborts_both_and_clears_inflight(self):
        await self.start((1, 2))
        await self.local_send(0, DATA, 1, packet(0, 0))
        await self.local_receive(1, DATA, 2)
        self.companions[1].wan.close()
        await self.local_receive(0, DISCONNECT, 1)
        await self.local_receive(1, DISCONNECT, 2)
        self.assertIsNone(self.relay.session)
        self.assertTrue(all(not p.pending for p in self.relay.peers.values()))

    async def fill_window(self, epochs):
        # Withhold every receiver ACK: the entire window must arrive anyway.
        for role in range(2):
            for seq in range(WINDOW):
                await self.local_send(role, DATA, epochs[role], packet(role, seq, 2), fragmented=True)
        for role in range(2):
            for seq in range(WINDOW):
                self.assertEqual(await self.local_receive(role, DATA, epochs[role]), packet(role ^ 1, seq, 2))
        self.assertTrue(all(list(c.pending_tx) == list(range(WINDOW)) for c in self.companions))

    async def test_full_window_without_waiting_for_delivery_and_barrier(self):
        epochs = (91, 92)
        await self.start(epochs)
        await self.fill_window(epochs)
        for role in range(2):
            for seq in range(WINDOW):
                await self.local_send(role, ACK, epochs[role], struct.pack("<I", seq), fragmented=True)
        for role in range(2):
            for seq in range(WINDOW):
                self.assertEqual(await self.local_receive(role, ACK, epochs[role]), struct.pack("<I", seq))
        # A non-streaming barrier can follow only once all deliveries arrived.
        await self.exchange(epochs, WINDOW)
        for role in range(2):
            await self.local_send(role, FINISH, epochs[role])
        await asyncio.sleep(0.02)
        self.assertIsNone(self.relay.session)

    async def test_cut_with_full_window_discards_every_old_packet(self):
        await self.start((41, 42))
        await self.fill_window((41, 42))
        old_session = self.relay.session
        self.companions[1].wan.close()
        for role in range(2):
            await self.local_receive(role, DISCONNECT, 41 + role)
        for _ in range(200):
            if all(c.joined for c in self.companions):
                break
            await asyncio.sleep(0.01)
        self.assertTrue(all(c.joined for c in self.companions))
        self.assertTrue(all(not c.pending_tx and not c.pending_rx for c in self.companions))
        await self.start((43, 44))
        self.companions[0].wan.write(encode({"type": "delivered", "epoch": 41,
            "session": old_session, "sequence": 0}))
        await self.companions[0].wan.drain()
        await self.exchange((43, 44), 0)

    async def test_barrier_cannot_overtake_unacknowledged_rows(self):
        await self.start((51, 52))
        await self.local_send(0, DATA, 51, packet(0, 0, 2))
        await self.local_receive(1, DATA, 52)
        await self.local_send(0, DATA, 51, packet(0, 1, 5))
        for role in range(2):
            await self.local_receive(role, DISCONNECT, 51 + role)

    async def test_window_overflow_and_reordered_ack_fail_closed(self):
        await self.start((61, 62))
        await self.fill_window((61, 62))
        # Wrong acknowledgement must never release a later packet.
        self.companions[1].wan.write(encode({"type": "delivered", "epoch": 62,
            "session": self.relay.session, "sequence": 1}))
        await self.companions[1].wan.drain()
        for role in range(2):
            await self.local_receive(role, DISCONNECT, 61 + role)
        for _ in range(200):
            if all(c.joined for c in self.companions):
                break
            await asyncio.sleep(0.01)
        await self.start((63, 64))
        await self.fill_window((63, 64))
        self.companions[0].wan.write(encode({"type": "data", "epoch": 63,
            "session": self.relay.session, "packet": base64.b64encode(packet(0, WINDOW, 2)).decode()}))
        await self.companions[0].wan.drain()
        for role in range(2):
            await self.local_receive(role, DISCONNECT, 63 + role)

    async def test_out_of_order_and_timeout_fail_closed(self):
        await self.start((1, 2))
        await self.local_send(0, DATA, 1, packet(0, 9))
        await self.local_receive(0, DISCONNECT, 1)
        await self.local_receive(1, DISCONNECT, 2)

    async def test_wrong_secret_duplicate_role_and_bounded_frames(self):
        port = self.server.sockets[0].getsockname()[1]
        for secret in ("wrong secret", SECRET):
            reader, writer = await asyncio.open_connection("127.0.0.1", port)
            writer.write(encode({"type": "join", "version": 1, "role": 0, "secret": secret}))
            await writer.drain()
            self.assertEqual(await asyncio.wait_for(reader.read(), 2), b"")
            writer.close()
        reader, writer = await asyncio.open_connection("127.0.0.1", port)
        writer.write(struct.pack("!I", 16385))
        await writer.drain()
        self.assertEqual(await asyncio.wait_for(reader.read(), 2), b"")
        writer.close()

    async def test_no_progress_timeout_despite_live_connections(self):
        await self.start((11, 12))
        watchdog = asyncio.create_task(self.relay.watchdog())
        try:
            await self.local_receive(0, DISCONNECT, 11)
            await self.local_receive(1, DISCONNECT, 12)
            self.assertTrue(all(c.joined for c in self.companions))
            await self.start((13, 14))
            await self.exchange((13, 14), 0)
        finally:
            watchdog.cancel()
            await asyncio.gather(watchdog, return_exceptions=True)

    async def test_different_rom_builds_refused_before_packets(self):
        await self.local_send(0, START, 2, b"TEST")
        await self.local_send(1, START, 3, b"DIFF")
        await self.local_receive(0, DISCONNECT, 2)
        await self.local_receive(1, DISCONNECT, 3)
        self.assertIsNone(self.relay.session)
        await self.start((4, 5))
        await self.exchange((4, 5), 0)

    async def test_peer_emulator_disappears_while_waiting(self):
        await self.local_send(0, START, 31, b"TEST")
        await asyncio.sleep(0.03)
        self.locals[1][1].close()
        await self.local_receive(0, DISCONNECT, 31)
        self.assertIsNone(self.relay.session)


if __name__ == "__main__":
    unittest.main()
