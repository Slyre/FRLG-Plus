// Four-trainer presentation module. The caller supplies validated state and
// transports actions; this module does not start a network connection.
const slots = [0, 1, 2, 3];
const textName = value => typeof value === 'string' ? value.replace(/[\u0000-\u001f\u007f]/g, '').trim().normalize('NFC').slice(0, 32) : '';
const scopeOf = state => JSON.stringify([state?.run, state?.participants, state?.build]);
const stateOf = group => typeof group?.state === 'number' ? ['empty', 'pending', 'alive', 'dead', 'failed'][group.state] : group?.state;

export function trainerLabels(players) {
  const names = slots.map(slot => textName(players?.find(player => player.slot === slot)?.trainerName));
  return names.map((name, slot) => {
    if (!name) return `Trainer ${slot + 1}`;
    const duplicate = names.filter(other => other && other.toLocaleLowerCase('en') === name.toLocaleLowerCase('en')).length > 1;
    return duplicate ? `${name} (${slot + 1})` : name;
  });
}

export class TrainerRosterCache {
  constructor() { this.scope = null; this.names = new Map(); }
  display(state) {
    const scope = scopeOf(state);
    if (scope !== this.scope) { this.scope = scope; this.names.clear(); }
    const players = Array.isArray(state?.players) ? state.players : [];
    const roster = slots.map(slot => {
      const player = players.find(player => player.slot === slot) || {};
      const trainerName = textName(player.trainerName || player.view?.trainerName);
      if (trainerName) this.names.set(slot, trainerName);
      return {slot, trainerName: trainerName || this.names.get(slot) || '', view: player.view || null,
        connected: player.connected === true, active: player.active === true};
    });
    const labels = trainerLabels(roster);
    return roster.map(player => ({...player, label: labels[player.slot], isLocal: player.slot === state?.localSlot}));
  }
}

export class GroupPartyCache {
  constructor() { this.scope = null; this.parties = new Map(); }
  display(state, player) {
    const scope = scopeOf(state);
    if (scope !== this.scope) { this.scope = scope; this.parties.clear(); }
    const party = player.view?.party;
    if (Array.isArray(party) && (player.view.safe || party.length)) this.parties.set(player.slot, party.map(mon => ({...mon})));
    const saved = this.parties.get(player.slot);
    if (!saved) return null;
    return saved.slice(0, 6).map(mon => ({...mon,
      dead: (state.groups || []).some(group => group.id === (mon.group ?? mon.pair) && stateOf(group) === 'dead' && (!Array.isArray(group.encounters) || group.encounters[player.slot] === 3))}));
  }
}

export function groupExplanation(group, roster = []) {
  const state = stateOf(group);
  if (state === 'dead') return {label: 'Lost group', detail: 'A Pokémon in this group died. This group cannot join the team.', selectable: false};
  if (state === 'failed') return {label: 'Encounter lost', detail: 'An encounter failed. This group cannot join the team.', selectable: false};
  const missing = slots.filter(slot => !group.species?.[slot]).map(slot => roster.find(player => player.slot === slot)?.label || `Trainer ${slot + 1}`);
  if (state !== 'alive' || missing.length || !Number.isInteger(group.id) || group.id <= 0) {
    return {label: 'Waiting for captures', detail: missing.length ? `Waiting for ${missing.join(', ')} to record an encounter.` : 'All captures are recorded. Waiting for the games to agree.', selectable: false};
  }
  return {label: 'Available', detail: 'Select this group to plan a team. All four trainers approve before the team changes.', selectable: true};
}

export function validGroupSelection(selected, groups) {
  return [...new Set(selected)].filter(id => groups.some(group => group.id === id && groupExplanation(group).selectable)).sort((a, b) => a - b);
}

export function findGroups(groups, {query = '', filter = 'all'} = {}, species = {}, locationName = id => `Area ${id}`) {
  const normalize = text => String(text).normalize('NFD').replace(/[\u0300-\u036f]/g, '').toLocaleLowerCase('en');
  const terms = normalize(query).trim().split(/\s+/).filter(Boolean);
  return groups.filter(group => {
    const state = stateOf(group);
    if (filter !== 'all' && !(filter === 'lost' ? ['dead', 'failed'].includes(state) : state === filter)) return false;
    const text = normalize([locationName(group.area), ...(group.species || []).map(id => species[id]?.name || 'Waiting')].join(' '));
    return terms.every(term => text.includes(term));
  }).sort((a, b) => a.area - b.area || (a.round || 0) - (b.round || 0) || a.id - b.id);
}

export function proposalProgress(proposal, roster) {
  if (!proposal) return null;
  const terminal = ['rejected', 'invalidated', 'confirmed'].includes(proposal.state);
  return {pending: !terminal && proposal.state !== 'approved', approved: proposal.state === 'approved', closed: terminal,
    title: proposal.state === 'confirmed' ? 'Team updated in all four games' : proposal.state === 'approved' ? 'Team approved' :
      proposal.state === 'rejected' ? 'Proposal declined' : proposal.state === 'invalidated' ? 'The groups changed. Propose a new team.' :
        `${roster.find(player => player.slot === proposal.author)?.label || `Trainer ${proposal.author + 1}`} proposes a team`,
    votes: slots.map(slot => ({slot, label: roster.find(player => player.slot === slot)?.label || `Trainer ${slot + 1}`,
      accepted: proposal.votes?.[slot] === true, confirmed: proposal.confirmed?.[slot] === true}))};
}

export function groupReadingIsFresh(state, nowSeconds = Date.now() / 1000) {
  return Number.isFinite(state?.updated) && Number.isFinite(nowSeconds) && nowSeconds - state.updated >= -5 && nowSeconds - state.updated <= 8;
}

export function mountGroupCompanion(container, {species = {}, locationName = id => `Area ${id}`, assetURL = 'assets/', onAction = null, now = () => Date.now() / 1000} = {}) {
  const doc = container.ownerDocument;
  const el = (tag, cls, text) => { const node = doc.createElement(tag); if (cls) node.className = cls; if (text !== undefined) node.textContent = text; return node; };
  const rosterCache = new TrainerRosterCache(), partyCache = new GroupPartyCache();
  let state = null, selected = new Set(), selectionScope = null, busy = false, closed = false, unavailable = false;
  const shell = el('section', 'group-companion');
  const heading = el('div', 'page-heading'); heading.append(el('h1', null, 'Four trainers. One shared journey.'));
  const sync = el('button', 'quiet', 'Sync'); sync.type = 'button'; heading.append(sync);
  const note = el('p', 'next-step', 'Load your save and keep the launcher open.'); note.setAttribute('role', 'status');
  const proposalCard = el('section', 'team-change'); proposalCard.hidden = true; proposalCard.tabIndex = -1;
  const teams = el('div', 'teams');
  const groupSection = el('section', 'group-browser'), tools = el('div', 'pair-search');
  const search = el('input'); search.type = 'search'; search.placeholder = 'Pokémon or location'; search.setAttribute('aria-label', 'Find a linked group');
  const filter = el('select'); filter.setAttribute('aria-label', 'Show linked groups');
  for (const [value, label] of [['all', 'All groups'], ['alive', 'Living'], ['pending', 'Waiting'], ['lost', 'Lost or failed']]) { const option = el('option', null, label); option.value = value; filter.append(option); }
  tools.append(search, filter);
  const count = el('p', 'small'); count.setAttribute('role', 'status');
  const groupList = el('div', 'duo-pairs'); groupList.tabIndex = -1;
  groupSection.append(el('h2', 'duo-picker-title', 'Choose your linked groups'), el('p', 'small', 'Each group contains one Pokémon from each trainer. Choose up to six living groups.'), tools, count, groupList);
  const selectionBar = el('div', 'selection-bar'), selectionText = el('div'), selectionCount = el('strong'), reason = el('p');
  selectionText.append(selectionCount, reason);
  const propose = el('button', 'primary', 'Propose team'); propose.type = 'button';
  selectionBar.append(selectionText, propose); shell.append(heading, note, proposalCard, teams, groupSection, selectionBar); container.replaceChildren(shell);
  const monName = id => id ? species[id]?.name || 'Unknown Pokémon' : 'Waiting for a capture';
  function sprite(id) {
    const file = species[id]?.icon;
    if (!/^\d+\.png$/.test(file || '')) return el('span', 'sprite empty-symbol', id ? '?' : '—');
    const image = el('img', 'sprite'); image.src = typeof assetURL === 'function' ? assetURL(id, file) : `${String(assetURL).replace(/\/?$/, '/')}${file}`; image.alt = ''; image.width = image.height = 64; return image;
  }
  async function act(request) {
    if (busy || unavailable || !onAction || !slots.includes(state?.localSlot) || !groupReadingIsFresh(state, now())) return;
    busy = true; render();
    try { await onAction(request); note.textContent = 'Request sent. Waiting for the games to confirm.'; }
    catch (error) { note.textContent = error?.message || 'The request could not be sent. Try again.'; }
    finally { busy = false; if (!closed) render(); }
  }
  function render() {
    if (closed) return;
    const roster = rosterCache.display(state), groups = Array.isArray(state?.groups) ? state.groups : [];
    const fresh = !unavailable && groupReadingIsFresh(state, now());
    const proposalFocus = proposalCard.contains(doc.activeElement) ? doc.activeElement.dataset.actionKey : null;
    selected = new Set(validGroupSelection(selected, groups));
    teams.replaceChildren(...roster.map(player => {
      const section = el('section', 'team'), header = el('header'), party = el('ol', 'party');
      header.append(el('h2', null, player.label + (player.isLocal ? ' · you' : '')));
      header.append(el('p', null, !fresh ? 'Last recorded team · connection interrupted' : !player.connected ? 'Disconnected · last recorded team' : !player.active ? 'Load or resume the game' : player.view?.battle ? 'In battle · last reading outside battle' : 'Last reading outside battle'));
      const mons = partyCache.display(state || {}, player);
      if (mons === null) party.append(el('li', 'no-pairs', 'Waiting for a team reading'));
      else for (let i = 0; i < 6; i++) {
        const mon = mons[i], row = el('li', 'mon' + (mon?.dead ? ' dead' : '')); row.append(el('span', 'slot', String(i + 1).padStart(2, '0')));
        if (!mon) row.append(el('span', 'empty-symbol', '—'), el('span', 'empty', 'Empty slot'));
        else { const labels = el('div'); labels.append(el('span', 'mon-name', monName(mon.species)), el('span', 'mon-detail', mon.dead ? 'Dead · group lost' : Number.isFinite(mon.hp) && Number.isFinite(mon.maxHp) && Number.isFinite(mon.level) ? `Lv. ${mon.level} · ${mon.hp}/${mon.maxHp} HP` : 'HP not available')); row.append(sprite(mon.species), labels); }
        party.append(row);
      }
      section.append(header, party); return section;
    }));
    const visible = findGroups(groups, {query: search.value, filter: filter.value || 'all'}, species, locationName);
    count.textContent = `${visible.length} of ${groups.length} groups shown`;
    const active = doc.activeElement, focusId = groupList.contains(active) ? active.dataset.groupId : null;
    const nodes = visible.map(group => {
      const info = groupExplanation(group, roster), card = el('div', 'team-change');
      card.append(el('h3', null, locationName(group.area)), el('p', 'small', info.label));
      const members = el('div', 'team-comparison');
      for (const player of roster) { const member = el('div', 'pair-mon'), labels = el('div'); labels.append(el('strong', null, player.label), el('p', 'small', monName(group.species?.[player.slot]))); member.append(sprite(group.species?.[player.slot]), labels); members.append(member); }
      const pick = el('button', selected.has(group.id) ? 'quiet primary' : 'quiet', selected.has(group.id) ? 'Selected' : 'Select group'); pick.type = 'button'; pick.dataset.groupId = String(group.id); pick.setAttribute('aria-pressed', String(selected.has(group.id)));
      pick.setAttribute('aria-label', `Select group at ${locationName(group.area)}: ${roster.map(player => `${player.label}, ${monName(group.species?.[player.slot])}`).join('; ')}`);
      pick.disabled = busy || !info.selectable;
      pick.addEventListener('click', () => { if (selected.has(group.id)) selected.delete(group.id); else if (selected.size < 6) selected.add(group.id); else { note.textContent = 'Choose up to six groups. Deselect a group first.'; return; } render(); });
      card.append(members, el('p', 'small', info.detail), pick); return card;
    });
    if (!nodes.length) nodes.push(el('p', 'no-pairs', groups.length ? 'No matching groups. Try another name, location or filter.' : 'Your encounters will appear once the four games are linked.'));
    groupList.replaceChildren(...nodes);
    if (focusId) ([...groupList.querySelectorAll('[data-group-id]')].find(button => button.dataset.groupId === focusId && !button.disabled) || groupList).focus({preventScroll: true});
    const progress = proposalProgress(state?.proposal, roster); proposalCard.hidden = !progress;
    if (!progress) proposalCard.replaceChildren();
    if (progress) {
      const title = el('h2', null, progress.title), votes = el('ul', 'team-change-progress');
      votes.append(...progress.votes.map(vote => { const row = el('li', vote.accepted ? 'applied' : ''); row.append(el('strong', null, vote.label), el('span', null, progress.approved || state.proposal.state === 'confirmed' ? vote.confirmed ? 'Team updated' : 'Waiting for the game' : vote.accepted ? 'Approved' : progress.closed ? 'No approval recorded' : 'Waiting for approval')); return row; }));
      const chosen = (state.proposal.ids || []).map(id => groups.find(group => group.id === id));
      const preview = el('p', 'small', chosen.map(group => group ? `${locationName(group.area)}: ${group.species.map(monName).join(' / ')}` : 'Group no longer available').join(' · '));
      proposalCard.replaceChildren(title, preview, votes);
      if (progress.pending) {
        const actions = el('div', 'proposal-actions'), own = state.proposal.author === state.localSlot;
        const reject = el('button', 'quiet', own ? 'Cancel proposal' : 'Decline'); reject.type = 'button'; reject.dataset.actionKey = 'reject'; reject.disabled = busy || !fresh || !onAction || !slots.includes(state.localSlot); reject.addEventListener('click', () => act({action: 'vote', id: state.proposal.id, accept: false})); actions.append(reject);
        if (!own && !state.proposal.votes?.[state.localSlot]) { const accept = el('button', 'primary', 'Approve team'); accept.type = 'button'; accept.dataset.actionKey = 'accept'; accept.disabled = busy || !fresh || !onAction || !slots.includes(state.localSlot); accept.addEventListener('click', () => act({action: 'vote', id: state.proposal.id, accept: true})); actions.append(accept); }
        proposalCard.append(actions);
      }
    }
    if (proposalFocus) {
      const replacement = [...proposalCard.querySelectorAll('[data-action-key]')].find(button => button.dataset.actionKey === proposalFocus && !button.disabled);
      (replacement || (progress ? proposalCard : groupList)).focus({preventScroll: true});
    }
    selectionCount.textContent = `${selected.size} ${selected.size === 1 ? 'group' : 'groups'} out of 6`;
    reason.textContent = !fresh ? 'You can plan with the last readings. Sending waits for the connection.' : roster.some(player => !player.connected || !player.active) ? 'You can plan a team. Sending waits for all four games to connect.' : progress?.pending || progress?.approved ? 'A team change is already waiting for confirmation.' : 'All four trainers approve before their teams change.';
    propose.disabled = busy || !onAction || !fresh || !slots.includes(state?.localSlot) || !selected.size || state?.canPropose !== true || !!(progress?.pending || progress?.approved);
    sync.disabled = busy || !onAction || !fresh || !slots.includes(state?.localSlot);
  }
  search.addEventListener('input', render); filter.addEventListener('change', render);
  propose.addEventListener('click', () => act({action: 'propose', ids: validGroupSelection(selected, state?.groups || [])}));
  sync.addEventListener('click', () => act({action: 'sync'}));
  render();
  return {update(next) { if (closed) return; const scope = scopeOf(next); if (scope !== selectionScope) { selectionScope = scope; selected.clear(); } state = next; unavailable = false;
      const roster = rosterCache.display(state);
      note.textContent = state?.detail || (!groupReadingIsFresh(state, now()) ? 'Waiting for a fresh reading. Keep the launcher open.' :
        roster.some(player => !player.connected) ? 'Waiting for all four games to connect. You can still plan your team below.' :
          roster.some(player => !player.active) ? 'Load or resume the games. Your last readings stay visible here.' :
            'Your group is connected. Choose living groups below to plan a team.');
      render(); },
    updateSpecies(next) { if (closed) return; species = next || {}; render(); },
    setUnavailable(message = 'Connection interrupted. You can still plan with the last readings.') { if (closed) return; unavailable = true; note.textContent = message; render(); },
    destroy() { closed = true; shell.remove(); }};
}

export function createGroupApp(root, {send, ...options} = {}) {
  return mountGroupCompanion(root, {...options, onAction: send || options.onAction || null});
}
