import test from 'node:test';
import assert from 'node:assert/strict';
import {trainerLabels, TrainerRosterCache, GroupPartyCache, groupExplanation, validGroupSelection, findGroups, proposalProgress, groupReadingIsFresh, mountGroupCompanion, createGroupApp} from '../../remote/companion-web/group.js';

const players = () => ['ASH', 'MISTY', 'BROCK', 'LEAF'].map((trainerName, slot) => ({slot, trainerName, connected: true, active: true, view: {safe: true, party: [{species: slot + 1, group: 1, level: 5, hp: 20, maxHp: 20}]}}));
const group = (id = 1, state = 'alive') => ({id, area: 8, round: id, state, species: [1, 4, 7, 25], encounters: state === 'dead' ? [3, 3, 3, 3] : [1, 1, 1, 1]});
const state = () => ({version: 2, updated: 100, localSlot: 0, run: [1, 2], participants: [10, 20, 30, 40], build: 'fixture', players: players(), groups: [group()], canPropose: true});

test('names use game spelling and stable seat numbers only when names collide', () => {
  const source = [{slot: 3, trainerName: ' ash '}, {slot: 1, trainerName: 'MISTY'}, {slot: 0, trainerName: 'ASH'}, {slot: 2, trainerName: 'LEAF'}];
  assert.deepEqual(trainerLabels(source), ['ASH (1)', 'MISTY', 'LEAF', 'ash (4)']);
  assert.deepEqual(trainerLabels([]), ['Trainer 1', 'Trainer 2', 'Trainer 3', 'Trainer 4']);
  assert.deepEqual(trainerLabels([{slot: 0, trainerName: 'ÉVA'}, {slot: 2, trainerName: 'E\u0301VA'}]), ['ÉVA (1)', 'Trainer 2', 'ÉVA (3)', 'Trainer 4']);
  assert.equal(source[0].trainerName, ' ash ');
});

test('the complete roster retains offline names and clears names when the run changes', () => {
  const cache = new TrainerRosterCache(), first = state(); first.players[2].trainerName = 'ASH';
  assert.deepEqual(cache.display(first).map(player => player.label), ['ASH (1)', 'MISTY', 'ASH (3)', 'LEAF']);
  const offline = {...first, players: [{slot: 0, connected: true, active: true}]};
  const roster = cache.display(offline);
  assert.equal(roster[2].label, 'ASH (3)'); assert.equal(roster[2].connected, false); assert.equal(roster[0].isLocal, true);
  assert.equal(cache.display({...offline, run: [3, 4]})[2].label, 'Trainer 3');
});

test('party readings remain available while offline and receive terminal death overlays', () => {
  const cache = new GroupPartyCache(), first = state();
  const original = cache.display(first, first.players[2]); assert.equal(original[0].hp, 20);
  const offline = {...first, groups: [group(1, 'dead')]};
  const saved = cache.display(offline, {slot: 2, view: null});
  assert.equal(saved[0].dead, true); assert.equal(saved[0].hp, 20); assert.equal(first.players[2].view.party[0].dead, undefined);
  assert.equal(cache.display({...offline, participants: [11, 22, 33, 44]}, {slot: 2, view: null}), null);
});

test('unavailable teams never invent Pokémon or HP, and a fresh empty team clears its cache', () => {
  const cache = new GroupPartyCache(), first = state();
  assert.equal(cache.display(first, {slot: 3, view: {safe: false, party: []}}), null);
  cache.display(first, first.players[3]);
  assert.equal(cache.display(first, {slot: 3, view: {safe: false, party: []}})[0].hp, 20);
  assert.deepEqual(cache.display(first, {slot: 3, view: {safe: true, party: []}}), []);
});

test('capture groups require all four captures and explain the missing trainer by name', () => {
  const roster = new TrainerRosterCache().display(state());
  const pending = {...group(2, 'pending'), species: [1, 4, 0, 0]};
  assert.match(groupExplanation(pending, roster).detail, /BROCK, LEAF/);
  assert.equal(groupExplanation({...group(), species: [1, 4, 7, 0]}, roster).selectable, false);
  for (const status of ['pending', 'dead', 'failed']) assert.equal(groupExplanation(group(1, status)).selectable, false);
  assert.equal(groupExplanation(group()).selectable, true);
  assert.deepEqual(validGroupSelection(new Set([3, 2, 1, 99]), [group(1), group(2, 'dead'), group(3)]), [1, 3]);
});

test('search covers every trainer Pokémon and filters quartet states without changing data', () => {
  const source = [group(3, 'failed'), {...group(1), area: 40}, group(2, 'pending')];
  const species = {1: {name: 'Bulbasaur'}, 4: {name: 'Charmander'}, 7: {name: 'Squirtle'}, 25: {name: 'Pikachu'}};
  assert.deepEqual(findGroups(source, {query: 'píkachu rock'}, species, area => area === 40 ? 'Rock Tunnel' : 'Route 1').map(row => row.id), [1]);
  assert.deepEqual(findGroups(source, {filter: 'lost'}).map(row => row.id), [3]);
  assert.deepEqual(findGroups(source, {filter: 'pending'}).map(row => row.id), [2]);
  assert.deepEqual(source.map(row => row.id), [3, 1, 2]);
});

test('four separate approvals and receipts are shown with the author game name', () => {
  const roster = new TrainerRosterCache().display(state());
  const proposal = {author: 2, ids: [1], state: 'pending', votes: [true, false, true, false], confirmed: [false, false, false, false]};
  const progress = proposalProgress(proposal, roster);
  assert.equal(progress.title, 'BROCK proposes a team'); assert.equal(progress.pending, true);
  assert.deepEqual(progress.votes.map(vote => vote.accepted), [true, false, true, false]);
  assert.deepEqual(progress.votes.map(vote => vote.label), ['ASH', 'MISTY', 'BROCK', 'LEAF']);
  assert.equal(proposalProgress({...proposal, state: 'approved'}, roster).approved, true);
  const confirmed = proposalProgress({...proposal, state: 'confirmed', confirmed: [true, true, true, true]}, roster);
  assert.equal(confirmed.pending, false); assert.equal(confirmed.title, 'Team updated in all four games');
  for (const status of ['rejected', 'invalidated']) assert.equal(proposalProgress({...proposal, state: status}, roster).pending, false);
});

test('actions require a recent numeric companion timestamp', () => {
  assert.equal(groupReadingIsFresh({updated: 100}, 108), true);
  assert.equal(groupReadingIsFresh({updated: 100}, 108.1), false);
  assert.equal(groupReadingIsFresh({updated: 100}, 94.9), false);
  for (const updated of [undefined, null, '100', NaN, Infinity]) assert.equal(groupReadingIsFresh({updated}, 100), false);
});

// Small DOM surface for testing the mount contract without launching a browser.
class Element {
  constructor(tag, doc) { this.tagName = tag; this.ownerDocument = doc; this.children = []; this.dataset = {}; this.attributes = {}; this.listeners = {}; this.value = ''; this._text = ''; this.disabled = false; }
  set textContent(value) { this._text = String(value); this.children = []; }
  get textContent() { return this._text + this.children.map(child => child.textContent).join(' '); }
  append(...nodes) { for (const node of nodes) { if (node.parent) node.remove(); node.parent = this; this.children.push(node); if (this.tagName === 'select' && this.children.length === 1) this.value = node.value; } }
  replaceChildren(...nodes) { this._text = ''; for (const child of this.children) child.parent = null; this.children = []; this.append(...nodes); }
  setAttribute(key, value) { this.attributes[key] = value; }
  addEventListener(type, handler) { (this.listeners[type] ||= []).push(handler); }
  async dispatch(type) { for (const handler of this.listeners[type] || []) await handler({target: this}); }
  contains(node) { return node === this || this.children.some(child => child.contains(node)); }
  querySelectorAll(selector) { return this.children.flatMap(child => [...((selector === '[data-group-id]' && child.dataset.groupId) || (selector === '[data-action-key]' && child.dataset.actionKey) ? [child] : []), ...child.querySelectorAll(selector)]); }
  focus() { this.ownerDocument.activeElement = this; }
  remove() { if (this.parent) this.parent.children = this.parent.children.filter(child => child !== this); this.parent = null; }
}
function documentFixture() { const doc = {activeElement: null, createElement: tag => new Element(tag, doc)}; return new Element('div', doc); }
function all(node, predicate) { return [...(predicate(node) ? [node] : []), ...node.children.flatMap(child => all(child, predicate))]; }

test('mount renders four real trainer teams and four capture members with text-only names', async () => {
  const container = documentFixture(), fixture = state(); fixture.players[1].trainerName = '<img src=x>';
  const mounted = mountGroupCompanion(container, {now: () => 100}); mounted.update(fixture);
  assert.equal(all(container, node => node.className === 'team').length, 4);
  assert.equal(all(container, node => node.className === 'pair-mon').length, 4);
  assert.match(container.textContent, /<img src=x>/); assert.equal(all(container, node => node.tagName === 'img').length, 0);
  assert.doesNotMatch(container.textContent, /Player [ABCD]/);
  mounted.destroy(); assert.equal(container.children.length, 0);
});

test('mount preserves a selected group focus and sends explicit proposal IDs only when fresh', async () => {
  const container = documentFixture(), requests = [], fixture = state();
  const mounted = mountGroupCompanion(container, {now: () => 100, onAction: async request => requests.push(request)}); mounted.update(fixture);
  const pick = all(container, node => node.dataset.groupId === '1')[0]; pick.focus(); await pick.dispatch('click');
  assert.equal(container.ownerDocument.activeElement.dataset.groupId, '1');
  let propose = all(container, node => node.tagName === 'button' && node.textContent === 'Propose team')[0];
  assert.equal(propose.disabled, false); await propose.dispatch('click');
  assert.deepEqual(requests, [{action: 'propose', ids: [1]}]);
  mounted.update({...fixture, updated: 1}); assert.equal(propose.disabled, true); await propose.dispatch('click'); assert.equal(requests.length, 1);
  mounted.update({...fixture, run: [3, 4]}); assert.equal(propose.disabled, true);
});

test('mount exposes four named votes and sends only the current incoming proposal approval', async () => {
  const container = documentFixture(), requests = [], fixture = state();
  fixture.proposal = {id: 'proposal-1', author: 2, ids: [1], state: 'pending', votes: [false, false, true, false]};
  const mounted = mountGroupCompanion(container, {now: () => 100, onAction: async request => requests.push(request)}); mounted.update(fixture);
  const votes = all(container, node => node.className === 'team-change-progress')[0]; assert.equal(votes.children.length, 4);
  const accept = all(container, node => node.tagName === 'button' && node.textContent === 'Approve team')[0]; await accept.dispatch('click');
  assert.deepEqual(requests, [{action: 'vote', id: 'proposal-1', accept: true}]);
  mounted.update({...fixture, proposal: {...fixture.proposal, votes: [true, false, true, false]}});
  assert.equal(all(container, node => node.tagName === 'button' && node.textContent === 'Approve team').length, 0);
});

test('createGroupApp adapts send and asset paths and exposes an explicit sync action', async () => {
  const container = documentFixture(), requests = [], fixture = state();
  const mounted = createGroupApp(container, {now: () => 100, send: async request => requests.push(request), assetURL: '/session/token/assets'});
  mounted.update(fixture); mounted.updateSpecies({1: {name: 'Bulbasaur', icon: '1.png'}});
  assert.ok(all(container, node => node.tagName === 'img').every(image => image.src === '/session/token/assets/1.png'));
  const sync = all(container, node => node.tagName === 'button' && node.textContent === 'Sync')[0]; await sync.dispatch('click');
  assert.deepEqual(requests, [{action: 'sync'}]);
});

test('incoming proposal controls retain keyboard focus across new readings and fall back when removed', () => {
  const container = documentFixture(), fixture = state();
  fixture.proposal = {id: 'proposal-1', author: 2, ids: [1], state: 'pending', votes: [false, false, true, false]};
  const mounted = createGroupApp(container, {now: () => 100, send: async () => {}}); mounted.update(fixture);
  all(container, node => node.dataset.actionKey === 'accept')[0].focus();
  mounted.update({...fixture, updated: 101}); assert.equal(container.ownerDocument.activeElement.dataset.actionKey, 'accept');
  mounted.update({...fixture, proposal: null}); assert.equal(container.ownerDocument.activeElement.className, 'duo-pairs');
});

test('unlinked death does not mark a surviving trainer Pokemon as dead', () => {
  const cache = new GroupPartyCache(), first = state();
  first.groups = [{...group(1, 'dead'), encounters: [3, 1, 1, 1]}];
  assert.equal(cache.display(first, first.players[0])[0].dead, true);
  assert.equal(cache.display(first, first.players[1])[0].dead, false);
});
