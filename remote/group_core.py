"""Four-player domain, independent of the legacy two-player ROM/transport.

JSON schema 2 owner snapshots have exactly these fields:
  version, slot, run[2], participants[4], trainerName, config[8], safe, battle,
  rows[{area, round, ordinal, otId, personality, species, state}].
Only the authenticated owner's column is accepted. ``round`` is the encounter
number within the area when same-area pairing is enabled, otherwise the global
non-starter encounter number. ``ordinal`` is the owner's native provenance ID.
State 1/2/3 means captured/failed/dead. A missing row never deletes a record.

Group IDs are (area << 7) | round, or 0x8000 | round without same-area pairing.
The starter always uses area 87, round 1. These IDs need no elected allocator.
An approval is a receipt, not proof that a ROM has changed its party. Runtime
integration must confirm only after observing the actual native party.
"""
import copy
import hashlib
import hmac
import itertools
import json
import os
from pathlib import Path
import re
import unicodedata

VERSION = 2
PLAYERS = 4
CAPACITY = 88
STARTER_AREA = 87
MAX_RECORD = 131072
SNAPSHOT_KEYS = {'version', 'slot', 'run', 'participants', 'trainerName',
                 'config', 'safe', 'battle', 'rows', 'party'}
ROW_KEYS = {'area', 'round', 'ordinal', 'otId', 'personality', 'species', 'state'}


def _integer(value, low, high):
    return type(value) is int and low <= value <= high


def _words(value, size, low=0):
    return (isinstance(value, list) and len(value) == size
            and all(_integer(n, low, 0xFFFFFFFF) for n in value))


def canonical(value):
    return json.dumps(value, sort_keys=True, separators=(',', ':'),
                      ensure_ascii=True, allow_nan=False).encode('ascii')


def digest(value):
    return hashlib.sha256(canonical(value)).hexdigest()


def _object(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError('duplicate JSON field')
        result[key] = value
    return result


def parse_json(data, limit=MAX_RECORD):
    if not isinstance(data, (bytes, str)) or len(data) > limit:
        raise ValueError('oversized group record')
    try:
        return json.loads(data, object_pairs_hook=_object,
                          parse_constant=lambda value: (_ for _ in ()).throw(ValueError('nonfinite JSON')))
    except (UnicodeError, RecursionError) as error:
        raise ValueError('invalid group JSON') from error


def group_id(row, same_area=True):
    if row['area'] == STARTER_AREA:
        if row['round'] != 1:
            raise ValueError('only one starter group is allowed')
        return (STARTER_AREA << 7) | 1
    return (row['area'] << 7 | row['round']) if same_area else (0x8000 | row['round'])


def validate_snapshot(value, slot, run, participants):
    if not isinstance(value, dict) or set(value) != SNAPSHOT_KEYS:
        raise ValueError('invalid owner snapshot fields')
    if (type(value['version']) is not int or value['version'] != VERSION
            or not _integer(slot, 0, 3) or type(value['slot']) is not int or value['slot'] != slot
            or not _words(value['run'], 2) or value['run'] != run
            or not _words(value['participants'], 4, 1) or value['participants'] != participants):
        raise ValueError('snapshot belongs to another owner or run')
    name = value['trainerName']
    if (not isinstance(name, str) or len(unicodedata.normalize('NFC', name)) > 7
            or any(unicodedata.category(c).startswith('C') for c in name)):
        raise ValueError('invalid trainer name')
    config = value['config']
    if (not isinstance(config, list) or len(config) != 8
            or any(not _integer(n, 0, 1) for i, n in enumerate(config) if i not in (5, 7))
            or not _integer(config[5], 0, 2) or type(config[7]) is not int or config[7] != 0):
        raise ValueError('invalid group rules')
    if type(value['safe']) is not bool or type(value['battle']) is not bool:
        raise ValueError('invalid game activity')
    party = value['party']
    if not isinstance(party, list) or len(party) > 6:
        raise ValueError('invalid owner party')
    for mon in party:
        if (not isinstance(mon, dict) or set(mon) != {'species', 'group', 'hp', 'maxHp', 'level'}
                or not _integer(mon['species'], 1, 411) or not _integer(mon['group'], 0, 65535)
                or not _integer(mon['maxHp'], 1, 65535) or not _integer(mon['hp'], 0, mon['maxHp'])
                or not _integer(mon['level'], 1, 100)):
            raise ValueError('invalid owner party Pokemon')
    if not isinstance(value['rows'], list) or len(value['rows']) > CAPACITY:
        raise ValueError('too many owner encounters')
    ids, ordinals, identities = set(), set(), set()
    for row in value['rows']:
        if not isinstance(row, dict) or set(row) != ROW_KEYS:
            raise ValueError('invalid owner encounter fields')
        if (not _integer(row['area'], 1, STARTER_AREA) or not _integer(row['round'], 1, CAPACITY)
                or not _integer(row['ordinal'], 1, 65535) or not _integer(row['state'], 1, 3)
                or not _integer(row['otId'], 0, 0xFFFFFFFF)
                or not _integer(row['personality'], 0, 0xFFFFFFFF)
                or not _integer(row['species'], 0 if row['state'] == 2 else 1, 411)):
            raise ValueError('invalid owner encounter')
        if row['state'] == 2 and any(row[field] for field in ('species', 'otId', 'personality')):
            raise ValueError('failed encounter cannot contain a captured Pokemon')
        if config[2] and config[3] and row['round'] != 1:
            raise ValueError('only the first encounter in an area can form a group')
        identifier = group_id(row, bool(config[2]))
        identity = (row['otId'], row['personality'])
        if identifier in ids or row['ordinal'] in ordinals or (row['species'] and identity in identities):
            raise ValueError('duplicate owner encounter')
        ids.add(identifier)
        ordinals.add(row['ordinal'])
        if row['species']:
            identities.add(identity)
    clean = copy.deepcopy(value)
    clean['trainerName'] = unicodedata.normalize('NFC', name).strip()
    clean['rows'].sort(key=lambda row: group_id(row, bool(config[2])))
    return clean


class GroupState:
    """Bounded group knowledge and unanimous team receipts for one exact run."""
    def __init__(self, secret, run, participants, build, species_types=None):
        if (not isinstance(secret, str) or not 20 <= len(secret) <= 128 or not secret.isascii()
                or not _words(run, 2) or not any(run) or not _words(participants, 4, 1)
                or len(set(participants)) != PLAYERS or not isinstance(build, str)
                or re.fullmatch('[0-9a-f]{8}', build) is None):
            raise ValueError('invalid four-player run identity')
        self.scope = digest(['soul-link-group-v2', secret])
        self.run, self.participants, self.build = list(run), list(participants), build
        self.species_types = copy.deepcopy(species_types or {})
        self.owners = [None] * PLAYERS
        self.proposal = None

    def context(self):
        owners = []
        for owner in self.owners:
            if owner is None:
                owners.append(None)
            else:
                config = list(owner['config'])
                # Enabling enforcement is an effect of applying the agreement.
                config[6] = 1
                owners.append(dict(config=config, rows=owner['rows']))
        return digest(dict(run=self.run, participants=self.participants,
                           build=self.build, owners=owners))

    def observe(self, slot, snapshot):
        clean = validate_snapshot(snapshot, slot, self.run, self.participants)
        old = self.owners[slot]
        if old:
            if old['rows'] and old['config'][2] != clean['config'][2]:
                raise ValueError('pairing mode cannot change after encounters have been recorded')
            rows = {group_id(row, bool(old['config'][2])): copy.deepcopy(row) for row in old['rows']}
            for row in clean['rows']:
                identifier = group_id(row, bool(clean['config'][2]))
                previous = rows.get(identifier)
                if previous:
                    if row['ordinal'] != previous['ordinal']:
                        raise ValueError('encounter provenance cannot be replaced')
                    if previous['state'] in (2, 3):
                        continue
                    if row['state'] == 2 or (row['otId'], row['personality']) != (previous['otId'], previous['personality']):
                        raise ValueError('captured Pokemon identity cannot be replaced')
                rows[identifier] = row
            clean['rows'] = sorted(rows.values(), key=lambda row: group_id(row, bool(clean['config'][2])))
            # Check the union, including rows omitted by an older save.
            clean = validate_snapshot(clean, slot, self.run, self.participants)
        prospective = list(self.owners)
        prospective[slot] = clean
        ids = {group_id(row, bool(owner['config'][2])) for owner in prospective if owner for row in owner['rows']}
        if len(ids) > CAPACITY:
            raise ValueError('group encounter registry is full')
        changed = clean != old
        self.owners = prospective
        if (self.proposal and self.proposal['state'] in ('pending', 'approved')
                and self.proposal['context'] != self.context()):
            self.proposal['state'] = 'invalidated'
        return changed

    def groups(self):
        groups = {}
        for slot, owner in enumerate(self.owners):
            if owner is None:
                continue
            for row in owner['rows']:
                identifier = group_id(row, bool(owner['config'][2]))
                entry = groups.setdefault(identifier, dict(id=identifier, area=row['area'],
                                          round=row['round'], owners=[None] * PLAYERS))
                entry['owners'][slot] = copy.deepcopy(row)
        for entry in groups.values():
            states = [row['state'] if row else 0 for row in entry['owners']]
            entry['state'] = ('dead' if 3 in states else 'failed' if 2 in states
                              else 'alive' if all(state == 1 for state in states) else 'pending')
            entry['species'] = [row['species'] if row else 0 for row in entry['owners']]
            entry['encounters'] = [3 if 3 in states and state == 1 and self.owners[slot]['config'][1] else state for slot, state in enumerate(states)]
        return [groups[key] for key in sorted(groups)]

    def death_targets(self, slot):
        if not _integer(slot, 0, 3):
            raise ValueError('invalid player slot')
        return [group['id'] for group in self.groups() if group['state'] == 'dead'
                and self.owners[slot]['config'][1] and group['owners'][slot] and group['owners'][slot]['state'] == 1]

    def _team(self, ids):
        if (not isinstance(ids, list) or not 1 <= len(ids) <= 6
                or any(not _integer(n, 1, 65535) for n in ids) or ids != sorted(set(ids))):
            raise ValueError('choose one to six unique sorted groups')
        if any(owner is None or not owner['config'][0] for owner in self.owners):
            raise ValueError('all four games must have joined and enabled Soul Link')
        configs = [list(owner['config']) for owner in self.owners]
        for config in configs:
            config[6] = 1
        if any(config != configs[0] for config in configs):
            raise ValueError('all four games must use the same rules')
        rows = {group['id']: group for group in self.groups()}
        mode = configs[0][5]
        for identifier in ids:
            if identifier not in rows or rows[identifier]['state'] != 'alive':
                raise ValueError('every selected group needs four living captures')
            if mode:
                types = []
                for species in rows[identifier]['species']:
                    values = self.species_types.get(species)
                    if not isinstance(values, (list, tuple)) or len(values) != 2:
                        raise ValueError('trusted species types are unavailable')
                    types.append(set(values[:1] if mode == 1 else values))
                if any(a & b for a, b in itertools.combinations(types, 2)):
                    raise ValueError('the selected group violates the pairwise type rule')

    def propose(self, slot, identifier, ids):
        if not _integer(slot, 0, 3) or not isinstance(identifier, str) or re.fullmatch('[0-9a-f]{32}', identifier) is None:
            raise ValueError('invalid team proposal identity')
        self._team(ids)
        if self.proposal and self.proposal['id'] == identifier:
            if (self.proposal['author'] != slot or self.proposal['ids'] != ids
                    or self.proposal['context'] != self.context()):
                raise ValueError('proposal identity cannot be reused for different content')
            return copy.deepcopy(self.proposal)
        if self.proposal and self.proposal['state'] in ('pending', 'approved'):
            raise ValueError('an agreement is already pending')
        self.proposal = dict(id=identifier, author=slot, ids=list(ids), context=self.context(),
                             votes=[False] * PLAYERS, confirmed=[False] * PLAYERS, state='pending')
        self.proposal['votes'][slot] = True
        return copy.deepcopy(self.proposal)

    def vote(self, slot, identifier, accept):
        if not _integer(slot, 0, 3) or type(accept) is not bool:
            raise ValueError('invalid vote')
        proposal = self.proposal
        if not proposal or proposal['id'] != identifier or proposal['state'] not in ('pending', 'approved'):
            raise ValueError('this proposal is no longer pending')
        if proposal['context'] != self.context():
            raise ValueError('the group context has changed')
        if proposal['state'] == 'approved':
            if not accept:
                raise ValueError('an approved agreement cannot be revoked by a late vote')
            return copy.deepcopy(proposal)
        if not accept:
            proposal['state'] = 'rejected'
        else:
            proposal['votes'][slot] = True
            if all(proposal['votes']):
                self._team(proposal['ids'])
                proposal['state'] = 'approved'
        return copy.deepcopy(proposal)

    def confirm(self, slot, identifier):
        if not _integer(slot, 0, 3):
            raise ValueError('invalid confirmation owner')
        proposal = self.proposal
        if not proposal or proposal['id'] != identifier or proposal['state'] not in ('approved', 'confirmed'):
            raise ValueError('no approved agreement to confirm')
        if proposal['context'] != self.context():
            raise ValueError('the group context has changed')
        proposal['confirmed'][slot] = True
        if all(proposal['confirmed']):
            proposal['state'] = 'confirmed'
        return copy.deepcopy(proposal)

    def state(self):
        return dict(version=VERSION, run=list(self.run), participants=list(self.participants), build=self.build,
                    players=[dict(slot=slot, trainerName=owner['trainerName'] if owner else '',
                                  view=copy.deepcopy(owner)) for slot, owner in enumerate(self.owners)],
                    groups=self.groups(), proposal=copy.deepcopy(self.proposal))

    def dump(self):
        record = dict(version=VERSION, scope=self.scope, run=self.run, participants=self.participants,
                      build=self.build, owners=self.owners, proposal=self.proposal)
        data = canonical(dict(record=record, checksum=digest(record)))
        if len(data) > MAX_RECORD:
            raise ValueError('group recovery record is too large')
        return data

    @classmethod
    def restore(cls, data, secret, run, participants, build, species_types=None):
        state = cls(secret, run, participants, build, species_types)
        envelope = parse_json(data)
        if not isinstance(envelope, dict) or set(envelope) != {'record', 'checksum'}:
            raise ValueError('invalid group recovery envelope')
        record = envelope['record']
        if (not isinstance(record, dict) or set(record) != {'version', 'scope', 'run', 'participants', 'build', 'owners', 'proposal'}
                or type(record['version']) is not int or record['version'] != VERSION
                or record['scope'] != state.scope or not _words(record['run'], 2) or record['run'] != run
                or not _words(record['participants'], 4, 1) or record['participants'] != participants or record['build'] != build
                or not isinstance(envelope['checksum'], str)
                or re.fullmatch('[0-9a-f]{64}', envelope['checksum']) is None
                or not hmac.compare_digest(envelope['checksum'], digest(record))
                or not isinstance(record['owners'], list) or len(record['owners']) != PLAYERS):
            raise ValueError('invalid or foreign group recovery record')
        for slot, owner in enumerate(record['owners']):
            if owner is not None:
                state.observe(slot, owner)
        proposal = record['proposal']
        if proposal is not None:
            keys = {'id', 'author', 'ids', 'context', 'votes', 'confirmed', 'state'}
            if (not isinstance(proposal, dict) or set(proposal) != keys
                    or not isinstance(proposal['id'], str) or re.fullmatch('[0-9a-f]{32}', proposal['id']) is None
                    or not _integer(proposal['author'], 0, 3)
                    or not isinstance(proposal['context'], str) or re.fullmatch('[0-9a-f]{64}', proposal['context']) is None
                    or proposal['state'] not in ('pending', 'approved', 'rejected', 'invalidated', 'confirmed')
                    or any(not isinstance(proposal[key], list) or len(proposal[key]) != PLAYERS
                           or any(type(flag) is not bool for flag in proposal[key]) for key in ('votes', 'confirmed'))
                    or not proposal['votes'][proposal['author']]
                    or not isinstance(proposal['ids'], list) or not 1 <= len(proposal['ids']) <= 6
                    or any(not _integer(n, 1, 65535) for n in proposal['ids'])
                    or proposal['ids'] != sorted(set(proposal['ids']))):
                raise ValueError('invalid saved agreement')
            if proposal['state'] in ('pending', 'approved'):
                state._team(proposal['ids'])
                if proposal['context'] != state.context():
                    raise ValueError('saved agreement has a different context')
            if ((proposal['state'] in ('approved', 'confirmed') and not all(proposal['votes']))
                    or (proposal['state'] == 'pending' and (all(proposal['votes']) or any(proposal['confirmed'])))
                    or (proposal['state'] == 'confirmed' and not all(proposal['confirmed']))
                    or (any(proposal['confirmed']) and not all(proposal['votes']))):
                raise ValueError('inconsistent saved votes or confirmations')
            state.proposal = copy.deepcopy(proposal)
        return state

    def save(self, path):
        path = Path(path)
        temporary = path.with_suffix(path.suffix + '.tmp')
        data = self.dump()
        try:
            with temporary.open('wb') as stream:
                stream.write(data)
                stream.flush()
                os.fsync(stream.fileno())
            os.replace(temporary, path)
            return True
        except OSError:
            return False
