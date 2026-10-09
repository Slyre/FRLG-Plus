import copy
import itertools
import unittest
from remote.group_core import GroupState, group_id

SECRET = 'a' * 48
RUN = [100, 200]
PARTICIPANTS = [10, 20, 30, 40]
BUILD = '0123abcd'
CONFIG = [1, 1, 1, 1, 1, 0, 1, 0]

def snapshot(slot, state=1, species=None, area=87, ordinal=1):
    return dict(version=2, slot=slot, run=RUN, participants=PARTICIPANTS,
                trainerName=['ASH', 'MISTY', 'BROCK', 'RED'][slot], config=list(CONFIG),
                safe=True, battle=False, party=[], rows=[dict(area=area, round=1, ordinal=ordinal,
                otId=PARTICIPANTS[slot] if state != 2 else 0,
                personality=1000 + slot if state != 2 else 0,
                species=(slot + 1 if species is None else species) if state != 2 else 0, state=state)])

def quartet(types=None):
    state = GroupState(SECRET, RUN, PARTICIPANTS, BUILD, types)
    for slot in range(4): state.observe(slot, snapshot(slot))
    return state

class GroupDomain(unittest.TestCase):
    def test_all_capture_arrival_orders(self):
        for order in itertools.permutations(range(4)):
            state = GroupState(SECRET, RUN, PARTICIPANTS, BUILD)
            for index, slot in enumerate(order):
                state.observe(slot, snapshot(slot))
                self.assertEqual('alive' if index == 3 else 'pending', state.groups()[0]['state'])
            self.assertEqual([1, 2, 3, 4], state.groups()[0]['species'])

    def test_death_from_each_trainer_survives_stale_save_and_restart(self):
        for dead in range(4):
            state = quartet()
            state.observe(dead, snapshot(dead, state=3))
            for slot in range(4): state.observe(slot, snapshot(slot))
            self.assertEqual('dead', state.groups()[0]['state'])
            for slot in range(4):
                self.assertEqual([] if slot == dead else [11137], state.death_targets(slot))
            recovered = GroupState.restore(state.dump(), SECRET, RUN, PARTICIPANTS, BUILD)
            self.assertEqual(state.state(), recovered.state())

    def test_owner_identity_and_scope_rejected_without_mutation(self):
        state = quartet()
        before = state.dump()
        for mutation in ('slot', 'run', 'participants', 'identity'):
            value = snapshot(0)
            if mutation == 'slot': value['slot'] = 1
            elif mutation == 'run': value['run'] = [200, 300]
            elif mutation == 'participants': value['participants'] = [20, 10, 30, 40]
            else: value['rows'][0]['personality'] += 1
            with self.assertRaises(ValueError): state.observe(0, value)
            self.assertEqual(before, state.dump())
        with self.assertRaises(ValueError): GroupState.restore(before, 'b' * 48, RUN, PARTICIPANTS, BUILD)

    def test_four_votes_then_four_native_receipts(self):
        state = quartet()
        state.propose(0, 'b' * 32, [11137])
        for slot in (1, 2):
            self.assertEqual('pending', state.vote(slot, 'b' * 32, True)['state'])
        with self.assertRaises(ValueError): state.confirm(0, 'b' * 32)
        self.assertEqual('approved', state.vote(3, 'b' * 32, True)['state'])
        restored = GroupState.restore(state.dump(), SECRET, RUN, PARTICIPANTS, BUILD)
        for slot in (0, 1, 2): self.assertEqual('approved', restored.confirm(slot, 'b' * 32)['state'])
        self.assertEqual('confirmed', restored.confirm(3, 'b' * 32)['state'])

    def test_fights_do_not_invalidate_votes_but_death_does(self):
        state = quartet()
        state.propose(0, 'b' * 32, [11137])
        value = snapshot(2); value.update(safe=False, battle=True, trainerName='BILL')
        state.observe(2, value)
        self.assertEqual('pending', state.proposal['state'])
        state.observe(1, snapshot(1, state=3))
        self.assertEqual('invalidated', state.proposal['state'])
        with self.assertRaises(ValueError): state.vote(3, 'b' * 32, True)

    def test_rejection_and_failed_encounter(self):
        state = quartet()
        state.propose(0, 'b' * 32, [11137])
        self.assertEqual('rejected', state.vote(2, 'b' * 32, False)['state'])
        failed = GroupState(SECRET, RUN, PARTICIPANTS, BUILD)
        for slot in range(4): failed.observe(slot, snapshot(slot, state=2 if slot == 3 else 1))
        self.assertEqual('failed', failed.groups()[0]['state'])
        with self.assertRaises(ValueError): failed.propose(0, 'c' * 32, [11137])

    def test_all_six_type_comparisons(self):
        for first, second in itertools.combinations(range(4), 2):
            types = {i + 1: [str(i), str(i)] for i in range(4)}
            types[second + 1] = types[first + 1]
            state = quartet(types)
            for slot in range(4):
                value = snapshot(slot); value['config'][5] = 2; state.observe(slot, value)
            with self.assertRaises(ValueError): state.propose(0, 'b' * 32, [11137])

    def test_unlinked_deaths_do_not_kill_other_trainers(self):
        state = quartet()
        for slot in range(4):
            value = snapshot(slot, state=3 if slot == 0 else 1)
            value['config'][1] = 0; state.observe(slot, value)
        self.assertEqual([], state.death_targets(1))
        self.assertEqual([3, 1, 1, 1], state.groups()[0]['encounters'])

    def test_native_species_and_first_encounter_contract(self):
        state = quartet(); before = state.dump()
        values = [snapshot(0, species=412), snapshot(0, state=2)]
        value = snapshot(0); value['rows'][0].update(area=8, round=2); values.append(value)
        value = snapshot(0); value['party'] = [dict(species=1, group=11137, hp=21, maxHp=20, level=5)]; values.append(value)
        for value in values:
            with self.assertRaises(ValueError): state.observe(0, value)
            self.assertEqual(before, state.dump())
