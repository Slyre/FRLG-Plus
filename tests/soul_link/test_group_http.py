import json
from pathlib import Path
import tempfile
import time
import unittest

from remote.group_web import GroupCompanionWeb


class GroupHTTP(unittest.IsolatedAsyncioTestCase):
    async def asyncSetUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        (self.root / 'companion-web').mkdir()
        (self.root / 'companion-assets').mkdir()
        for name in ('index.html', 'style.css', 'app.js'):
            (self.root / 'companion-web' / name).write_text('fixture')
        (self.root / 'companion-assets/species.json').write_text('{}')
        self.state = dict(version=2, localSlot=3, updated=time.time(), commandSession='b' * 32,
                          run=[1, 2], participants=[10, 20, 30, 40], canPropose=True,
                          groups=[dict(id=11137, state='alive'), dict(id=129, state='dead')],
                          proposal=None)
        self.write_state()
        self.web = await GroupCompanionWeb(self.root, 3).start()

    def write_state(self):
        (self.root / 'compagnon-etat.json').write_text(json.dumps(self.state), encoding='utf-8')

    async def asyncTearDown(self):
        await self.web.close()
        self.temp.cleanup()

    async def request(self, route, data=None, host=None):
        reader, writer = await __import__('asyncio').open_connection('127.0.0.1', int(self.web.host.split(':')[1]))
        payload = json.dumps(data).encode() if data is not None else b''
        method = 'POST' if data is not None else 'GET'
        headers = f'{method} {self.web.prefix}{route} HTTP/1.1\r\nHost: {host or self.web.host}\r\n'
        if data is not None:
            headers += f'Content-Type: application/json\r\nContent-Length: {len(payload)}\r\n'
        writer.write(headers.encode() + b'\r\n' + payload)
        await writer.drain()
        response = await reader.read()
        writer.close()
        await writer.wait_closed()
        return int(response.split(b' ')[1]), response.split(b'\r\n\r\n', 1)[1]

    async def test_group_assets_and_capability(self):
        for route in ('', 'group.js', 'group-entry.js'):
            self.assertEqual(200, (await self.request(route))[0])
        self.assertEqual(403, (await self.request('state', host='attacker.example'))[0])
        self.assertEqual(404, (await self.request('../group_core.py'))[0])

    async def test_bound_local_identity_and_living_groups(self):
        status, _ = await self.request('actions', dict(action='propose', ids=[11137]))
        self.assertEqual(200, status)
        queued = json.loads(next((self.root / 'compagnon-commandes').glob('*.json')).read_text())
        self.assertEqual(3, queued['slot'])
        self.assertEqual('b' * 32, queued['session'])
        self.assertEqual([10, 20, 30, 40], queued['participants'])
        for ids in ([129], [True], [11137, 11137]):
            self.assertEqual(400, (await self.request('actions', dict(action='propose', ids=ids)))[0])
        self.state['localSlot'] = 0
        self.write_state()
        self.assertEqual(400, (await self.request('actions', dict(action='sync')))[0])

    async def test_vote_context_and_stale_actions(self):
        self.state['proposal'] = dict(id='a' * 32, state='pending')
        self.write_state()
        self.assertEqual(200, (await self.request('actions', dict(action='accept', id='a' * 32)))[0])
        self.assertEqual(400, (await self.request('actions', dict(action='accept', id='c' * 32)))[0])
        self.state['proposal']['state'] = 'approved'
        self.write_state()
        self.assertEqual(400, (await self.request('actions', dict(action='reject', id='a' * 32)))[0])
        self.state['updated'] = time.time() - 20
        self.write_state()
        self.assertEqual(400, (await self.request('actions', dict(action='sync')))[0])
