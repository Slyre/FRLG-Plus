import asyncio
import copy
import unittest
from remote.group_relay import GroupRooms, GroupRelay, GroupPeer, Outbox, encode, receive
from tests.soul_link.test_group_core import SECRET, RUN, PARTICIPANTS, BUILD, snapshot

class GroupNetwork(unittest.IsolatedAsyncioTestCase):
    async def asyncSetUp(self):
        self.rooms = GroupRooms()
        self.server = await asyncio.start_server(self.rooms.handle, '127.0.0.1', 0)
        self.port = self.server.sockets[0].getsockname()[1]
        self.clients = []

    async def asyncTearDown(self):
        for reader, writer, _ in self.clients:
            writer.close()
        for reader, writer, _ in self.clients:
            await writer.wait_closed()
        self.rooms.close()
        self.server.close()
        await self.server.wait_closed()
        await asyncio.sleep(.02)

    async def connect(self, slot):
        reader, writer = await asyncio.open_connection('127.0.0.1', self.port)
        writer.write(encode(dict(type='join', version=2, players=4, role=slot, secret=SECRET,
                                 run=RUN, participants=PARTICIPANTS, build=BUILD, client=f'{slot + 1:032x}')))
        await writer.drain()
        joined = await asyncio.wait_for(receive(reader), 2)
        self.assertEqual('joined', joined['type'])
        client = (reader, writer, joined['session']); self.clients.append(client)
        return client

    async def send(self, client, kind, **values):
        reader, writer, session = client
        writer.write(encode(dict(type=kind, version=2, session=session, **values)))
        await writer.drain()

    async def until(self, client, predicate):
        async def read():
            while True:
                value = await receive(client[0])
                if predicate(value): return value
        return await asyncio.wait_for(read(), 2)

    async def joined_quartet(self):
        clients = [await self.connect(i) for i in range(4)]
        for slot, client in enumerate(clients): await self.send(client, 'view', sequence=1, snapshot=snapshot(slot))
        await self.until(clients[0], lambda value: value.get('type') == 'state' and value['state']['groups'] and value['state']['groups'][0]['state'] == 'alive')
        return clients

    async def test_real_four_sockets_votes_and_receipts(self):
        clients = await self.joined_quartet()
        domain = self.rooms.rooms[SECRET]['relay'].domain
        context = domain.context()
        await self.send(clients[0], 'propose', id='b' * 32, ids=[11137], context=context)
        await self.until(clients[0], lambda value: value.get('type') == 'state' and value['state']['proposal'] is not None)
        for slot in range(1, 4): await self.send(clients[slot], 'vote', id='b' * 32, accept=True, context=context)
        await self.until(clients[0], lambda value: value.get('type') == 'state' and value['state']['proposal']['state'] == 'approved')
        for client in clients: await self.send(client, 'confirm', id='b' * 32, context=context)
        final = await self.until(clients[0], lambda value: value.get('type') == 'state' and value['state']['proposal']['state'] == 'confirmed')
        self.assertEqual([True] * 4, final['state']['proposal']['confirmed'])

    async def test_owner_impersonation_and_stale_sequence_rejected(self):
        clients = await self.joined_quartet()
        await self.send(clients[1], 'view', sequence=2, snapshot=snapshot(0))
        error = await self.until(clients[1], lambda value: value['type'] == 'error')
        self.assertIn('owner', error['reason'])
        value = snapshot(1); value['trainerName'] = 'BILL'
        await self.send(clients[1], 'view', sequence=1, snapshot=value)
        error = await self.until(clients[1], lambda value: value['type'] == 'error')
        self.assertIn('sequence', error['reason'])
        self.assertEqual('MISTY', self.rooms.rooms[SECRET]['relay'].domain.owners[1]['trainerName'])

    async def test_disconnect_retains_death_for_rejoining_trainer(self):
        clients = await self.joined_quartet()
        clients[3][1].close(); await clients[3][1].wait_closed()
        await self.until(clients[0], lambda value: value['type'] == 'state' and not value['state']['players'][3]['connected'])
        await self.send(clients[1], 'view', sequence=2, snapshot=snapshot(1, state=3))
        await self.until(clients[0], lambda value: value['type'] == 'state' and value['state']['groups'][0]['state'] == 'dead')
        returning = await self.connect(3)
        state = await self.until(returning, lambda value: value['type'] == 'state')
        self.assertEqual('dead', state['state']['groups'][0]['state'])
        self.assertEqual('RED', state['state']['players'][3]['trainerName'])
        self.assertFalse(state['state']['players'][3]['active'])

    async def test_duplicate_live_seat_does_not_evict_owner(self):
        await self.connect(0)
        reader, writer = await asyncio.open_connection('127.0.0.1', self.port)
        writer.write(encode(dict(type='join', version=2, players=4, role=0, secret=SECRET,
                                 run=RUN, participants=PARTICIPANTS, build=BUILD, client=f'{1:032x}')))
        await writer.drain()
        self.assertEqual(b'', await asyncio.wait_for(reader.read(), 2))
        writer.close(); await writer.wait_closed()
        self.assertEqual(1, len(self.rooms.rooms[SECRET]['relay'].peers))

    async def test_slow_outbox_keeps_latest_reading_and_ordered_votes(self):
        class Writer:
            transport = None
            def __init__(self): self.transport = self; self.aborted = False
            def abort(self): self.aborted = True
        writer = Writer(); box = Outbox(writer, limit=2)
        box.put(b'old reading', state=True); box.put(b'new reading', state=True)
        box.put(b'vote 1'); box.put(b'vote 2')
        self.assertEqual(b'new reading', box.latest_state)
        self.assertEqual([b'vote 1', b'vote 2'], list(box.control))
        self.assertFalse(box.put(b'vote 3')); self.assertTrue(writer.aborted)

    async def test_automatic_seats_remember_installation_after_disconnect(self):
        await self.connect(0)
        async def auto(credential):
            reader, writer = await asyncio.open_connection('127.0.0.1', self.port)
            writer.write(encode(dict(type='join', version=2, players=4, role=-1, secret=SECRET,
                                     run=RUN, participants=PARTICIPANTS, build=BUILD, client=credential)))
            await writer.drain()
            joined = await asyncio.wait_for(receive(reader), 2)
            self.clients.append((reader, writer, joined['session']))
            return joined['role'], writer
        for seat in range(1, 4):
            role, writer = await auto(f'{seat + 1:032x}')
            self.assertEqual(seat, role)
        writer.close(); await writer.wait_closed()
        await self.until(self.clients[0], lambda value: value['type'] == 'state' and not value['state']['players'][3]['connected'])
        role, writer = await auto(f'{4:032x}')
        self.assertEqual(3, role)
