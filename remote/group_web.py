"""Local four-trainer UI using the same HTTP capability checks as the duo UI."""
import json
import math
import os
from pathlib import Path
import re
import secrets
import time

try:
    from .companion_web import CompanionWeb
except ImportError:
    from companion_web import CompanionWeb


class GroupCompanionWeb(CompanionWeb):
    def __init__(self, root, role):
        if type(role) is not int or not 0 <= role < 4:
            raise ValueError('Invalid trainer seat')
        super().__init__(root, role)
        source = Path(__file__).with_name('companion-web')
        for name, mime in [('group.js', 'text/javascript'), ('group-entry.js', 'text/javascript'), ('group.css', 'text/css')]:
            self.static[name] = ((source / name).read_bytes(), mime + '; charset=utf-8')
        self.static['index.html'] = ((source / 'group.html').read_bytes(), 'text/html; charset=utf-8')

    def _queue(self, request):
        if not isinstance(request, dict):
            raise ValueError('Invalid request.')
        state = json.loads((self.root / 'compagnon-etat.json').read_text(encoding='utf-8'))
        updated = state.get('updated')
        if (state.get('version') != 2 or state.get('localSlot') != self.role
                or type(updated) not in (int, float) or not math.isfinite(updated)
                or not -5 <= time.time() - updated <= 8):
            raise ValueError('The connection has stopped. Run play.bat again.')
        action = request.get('action')
        clean = {'action': action}
        if action == 'sync':
            pass
        elif action == 'propose':
            ids = request.get('ids')
            if (not isinstance(ids, list) or not 1 <= len(ids) <= 6
                    or any(type(i) is not int or not 0 < i <= 65535 for i in ids)
                    or ids != sorted(set(ids))):
                raise ValueError('Choose one to six living groups.')
            rows = {row['id']: row for row in state.get('groups', [])}
            if not state.get('canPropose') or any(rows.get(i, {}).get('state') != 'alive' for i in ids):
                raise ValueError('The group has changed. Refresh and choose again.')
            clean['ids'] = ids
        elif action in ('accept', 'reject', 'vote'):
            if action == 'vote' and type(request.get('accept')) is not bool:
                raise ValueError('Invalid team vote.')
            proposal = state.get('proposal')
            if (not re.fullmatch('[0-9a-f]{32}', str(request.get('id', '')))
                    or not isinstance(proposal, dict) or proposal.get('id') != request['id']
                    or proposal.get('state') != 'pending'):
                raise ValueError('This team proposal is no longer waiting for votes.')
            clean['id'] = request['id']
            if action == 'vote':
                clean['accept'] = request['accept']
        else:
            raise ValueError('Unknown request.')
        session = state.get('commandSession')
        if not isinstance(session, str) or not re.fullmatch('[0-9a-f]{32}', session):
            raise ValueError('The companion is restarting. Try again in a moment.')
        clean.update(session=session, issued=time.time(), slot=self.role,
                     run=state.get('run'), participants=state.get('participants'))
        folder = self.root / 'compagnon-commandes'
        folder.mkdir(exist_ok=True)
        if sum(1 for _ in folder.glob('*.json')) >= 16:
            raise ValueError('A few actions are still waiting. Try again in a moment.')
        target = folder / (secrets.token_hex(16) + '.json')
        temporary = target.with_suffix('.tmp')
        temporary.write_text(json.dumps(clean, separators=(',', ':')), encoding='utf-8')
        os.replace(temporary, target)
