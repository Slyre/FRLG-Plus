"""Loopback-only companion UI, sharing the existing desktop command queue."""
import asyncio
import contextlib
import json
import math
import os
from pathlib import Path
import re
import secrets
import time


class CompanionWeb:
    def __init__(self, root, role):
        self.root = Path(root)
        self.role = role
        self.token = secrets.token_hex(24)
        self.prefix = '/session/' + self.token + '/'
        self.server = None
        self.static = {}
        for name, mime in [('index.html', 'text/html'), ('style.css', 'text/css'), ('app.js', 'text/javascript')]:
            self.static[name] = ((self.root / 'companion-web' / name).read_bytes(), mime + '; charset=utf-8')
        self.static['species'] = ((self.root / 'companion-assets/species.json').read_bytes(), 'application/json; charset=utf-8')

    async def start(self):
        self.server = await asyncio.start_server(self.handle, '127.0.0.1', 0, limit=8192)
        self.host = '127.0.0.1:' + str(self.server.sockets[0].getsockname()[1])
        self.origin = 'http://' + self.host
        self.url = self.origin + self.prefix
        self.registration = self.root / 'compagnon-web.json'
        tmp = self.registration.with_suffix('.json.tmp')
        tmp.write_text(json.dumps(dict(url=self.url, pid=os.getpid(), started=time.time())), encoding='utf-8')
        os.replace(tmp, self.registration)
        return self

    async def close(self):
        if self.server:
            self.server.close()
            await self.server.wait_closed()
        with contextlib.suppress(OSError, ValueError):
            if json.loads(self.registration.read_text(encoding='utf-8')).get('url') == self.url:
                self.registration.unlink()

    def queue(self, request):
        try: self._queue(request)
        except OSError as error:
            raise ValueError('The game folder could not be read or written. Check folder permissions and free disk space, then retry.') from error

    def _queue(self, request):
        if not isinstance(request, dict):
            raise ValueError('Invalid request.')
        action = request.get('action')
        if action == 'sync':
            clean = dict(action=action)
        elif action == 'propose':
            ids = request.get('ids')
            if (not isinstance(ids, list) or not 1 <= len(ids) <= 6 or ids != sorted(set(ids))
                    or any(type(i) is not int or not 0 < i <= 65535 for i in ids)):
                raise ValueError('Pick one to six living pairs.')
            clean = dict(action=action, ids=ids)
        elif action in ('accept', 'reject') and re.fullmatch('[0-9a-f]{32}', str(request.get('id', ''))):
            clean = dict(action=action, id=request['id'])
        else:
            raise ValueError('Unknown request.')
        state = json.loads((self.root / 'compagnon-etat.json').read_text(encoding='utf-8'))
        updated=state.get('updated')
        if (type(updated) not in (int,float) or not math.isfinite(updated)
                or not -5<=time.time()-updated<=8):
            raise ValueError('The network has stopped. Run play.bat again.')
        if action == 'propose':
            rows = {p['id']: p for p in (state.get('local') or {}).get('pairs', [])}
            if not state.get('canPropose') or state.get('proposal') or any(i not in rows or rows[i]['state'] != 2 for i in clean['ids']):
                raise ValueError('The pairs have changed or the games are not ready. Check your selection.')
        if action in ('accept', 'reject'):
            proposal = state.get('proposal') or {}
            if proposal.get('id') != clean['id'] or (action == 'accept' and proposal.get('author') == self.role):
                raise ValueError('This proposal can no longer be accepted.')
        session=state.get('commandSession')
        if not isinstance(session,str) or not re.fullmatch('[0-9a-f]{32}',session):
            raise ValueError('The companion is restarting. Try again in a moment.')
        local=state.get('local')
        clean.update(session=session,issued=time.time(),duo=[local.get('run'),local.get('participants')] if local else None)
        folder = self.root / 'compagnon-commandes'
        folder.mkdir(exist_ok=True)
        if len(list(folder.glob('*.json'))) >= 32:
            raise ValueError('Requests are already waiting. Give it a moment.')
        tmp = folder / (secrets.token_hex(16) + '.tmp')
        tmp.write_text(json.dumps(clean), encoding='utf-8')
        os.replace(tmp, tmp.with_suffix('.json'))

    async def handle(self, reader, writer):
        status = 200
        mime = 'application/json; charset=utf-8'
        body = b''
        try:
            raw = await asyncio.wait_for(reader.readuntil(b'\r\n\r\n'), 3)
            lines = raw.decode('ascii').split('\r\n')
            method, path, protocol = lines[0].split(' ')
            headers = {}
            for line in lines[1:]:
                if not line: continue
                key, value = line.split(':', 1)
                key = key.lower()
                if key in headers: raise ValueError('duplicate header')
                headers[key] = value.strip()
            if protocol != 'HTTP/1.1' or headers.get('host') != self.host:
                status = 403
            elif (headers.get('origin', self.origin) != self.origin
                  or headers.get('sec-fetch-site', 'same-origin') not in ('same-origin', 'none')):
                status = 403
            elif not path.startswith(self.prefix):
                status = 404
            else:
                route = path[len(self.prefix):]
                if method == 'GET' and (route in self.static or route == ''):
                    body, mime = self.static[route or 'index.html']
                elif method == 'GET' and re.fullmatch(r'assets/[0-9]{1,3}\.png', route):
                    body = (self.root / 'companion-assets' / route[7:]).read_bytes()
                    mime = 'image/png'
                elif method == 'GET' and route == 'health':
                    body = b'{"ready":true}'
                elif method == 'GET' and route == 'theme':
                    preference = None
                    with contextlib.suppress(OSError, ValueError):
                        preference = json.loads((self.root / 'companion-theme.json').read_text())
                    body = json.dumps(dict(theme=preference if preference in ('dark', 'light') else None)).encode()
                elif method == 'GET' and route == 'diagnostics':
                    state=json.loads((self.root / 'compagnon-etat.json').read_text(encoding='utf-8'))
                    body=json.dumps(dict(version=1,metrics=state.get('metrics',{})),indent=2).encode()
                elif method == 'GET' and route == 'state':
                    data = (self.root / 'compagnon-etat.json').read_bytes()
                    if len(data) > 131072: raise ValueError('oversized state')
                    body = data
                elif method == 'POST' and route in ('actions', 'theme'):
                    length = int(headers.get('content-length', '0'))
                    if headers.get('content-type') != 'application/json' or not 0 < length <= 4096 or 'transfer-encoding' in headers:
                        status = 400
                    else:
                        payload = await asyncio.wait_for(reader.readexactly(length), 3)
                        request = json.loads(payload)
                        if route == 'theme':
                            if not isinstance(request, dict) or request.get('theme') not in ('dark', 'light'):
                                raise ValueError('Invalid theme.')
                            preference_file = self.root / 'companion-theme.json'
                            temporary = preference_file.with_suffix('.json.tmp')
                            temporary.write_text(json.dumps(request['theme']))
                            os.replace(temporary, preference_file)
                        else:
                            self.queue(request)
                        body = b'{"queued":true}'
                else:
                    status = 404 if method == 'GET' else 405
        except FileNotFoundError:
            status = 404
        except (ValueError, TypeError, KeyError, UnicodeError) as error:
            status = 400
            body = json.dumps(dict(error=str(error)), ensure_ascii=False).encode()
        except (OSError, asyncio.TimeoutError, asyncio.IncompleteReadError, asyncio.LimitOverrunError):
            status = 400
            body = json.dumps(dict(error='The companion could not complete this request. Try again.')).encode()
        try:
            reasons = {200: 'OK', 400: 'Bad Request', 403: 'Forbidden', 404: 'Not Found', 405: 'Method Not Allowed'}
            header = (f'HTTP/1.1 {status} {reasons[status]}\r\nContent-Type: {mime}\r\nContent-Length: {len(body)}\r\n'
                      "Cache-Control: no-store\r\nX-Content-Type-Options: nosniff\r\nReferrer-Policy: no-referrer\r\n"
                      "Content-Security-Policy: default-src 'self'; script-src 'self'; style-src 'self'; img-src 'self'; connect-src 'self'; frame-ancestors 'none'; base-uri 'none'; form-action 'none'\r\n"
                      'Connection: close\r\n\r\n')
            writer.write(header.encode() + body)
            await asyncio.wait_for(writer.drain(), 3)
        except (ConnectionError, asyncio.TimeoutError):
            pass
        finally:
            writer.close()
            with contextlib.suppress(ConnectionError): await writer.wait_closed()
