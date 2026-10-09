import asyncio
import unittest
from remote.public_relay import Rooms
from remote.network import send, receive
from remote.group_relay import encode, receive as receive_group
from tests.soul_link.test_group_core import SECRET, RUN, PARTICIPANTS, BUILD

class MixedProtocols(unittest.IsolatedAsyncioTestCase):
    async def test_same_code_isolated_across_duo_and_group_protocols(self):
        rooms = Rooms(four_players=True)
        server = await asyncio.start_server(rooms.handle, '127.0.0.1', 0)
        port = server.sockets[0].getsockname()[1]
        writers = []
        try:
            duo, writer = await asyncio.open_connection('127.0.0.1', port); writers.append(writer)
            await send(writer, dict(type='join', version=1, secret=SECRET, role=0))
            self.assertEqual({'type': 'joined'}, await asyncio.wait_for(receive(duo), 2))
            group, writer = await asyncio.open_connection('127.0.0.1', port); writers.append(writer)
            writer.write(encode(dict(type='join', version=2, players=4, role=0, secret=SECRET,
                run=RUN, participants=PARTICIPANTS, build=BUILD, client='b' * 32)))
            await writer.drain()
            self.assertEqual('joined', (await asyncio.wait_for(receive_group(group), 2))['type'])
            self.assertIsNot(rooms.rooms[SECRET]['relay'], rooms.groups.rooms[SECRET]['relay'])
            await send(writers[0], dict(type='ping'))
            self.assertEqual({'type': 'pong'}, await asyncio.wait_for(receive(duo), 2))
        finally:
            for writer in writers: writer.close()
            for writer in writers: await writer.wait_closed()
            server.close(); await server.wait_closed()
            for _ in range(50):
                if rooms.connections == 0: break
                await asyncio.sleep(.01)
            await rooms.close()

    async def test_four_player_support_requires_explicit_enable(self):
        rooms = Rooms()
        server = await asyncio.start_server(rooms.handle, '127.0.0.1', 0)
        reader, writer = await asyncio.open_connection('127.0.0.1', server.sockets[0].getsockname()[1])
        try:
            writer.write(encode(dict(type='join', version=2, players=4, role=0, secret=SECRET,
                run=RUN, participants=PARTICIPANTS, build=BUILD, client='b' * 32)))
            await writer.drain()
            self.assertEqual(b'', await asyncio.wait_for(reader.read(), 2))
            self.assertIsNone(rooms.groups)
        finally:
            writer.close(); await writer.wait_closed()
            server.close(); await server.wait_closed(); await rooms.close()
