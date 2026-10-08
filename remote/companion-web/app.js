// Canonical encounter IDs from src/data/nuzlocke_areas.h and Safari subdivisions
// from SoulLink_GetCanonicalArea in src/soul_link_local.c.
const otherAreas = {
  39:'Safari Zone',40:'Rock Tunnel',41:'Seafoam Islands',42:'Pokémon Tower',
  43:'Cerulean Cave',44:'Power Plant',45:'One Island',46:'Four Island',47:'Five Island',
  48:'Kindle Road',49:'Treasure Beach',50:'Cape Brink',51:'Bond Bridge',52:'Three Isle Port',
  53:'Resort Gorgeous',54:'Water Labyrinth',55:'Five Isle Meadow',56:'Memorial Pillar',
  57:'Outcast Island',58:'Green Path',59:'Water Path',60:'Ruin Valley',61:'Trainer Tower',
  62:'Canyon Entrance',63:'Sevault Canyon',64:'Tanoby Ruins',65:'Navel Rock',66:'Mt. Ember',
  67:'Berry Forest',68:'Icefall Cave',69:'Lost Cave',70:'Pattern Bush',71:'Altering Cave',
  72:'Monean Chamber',73:'Liptoo Chamber',74:'Weepth Chamber',75:'Dilford Chamber',
  76:'Scufib Chamber',77:'Rixy Chamber',78:'Viapois Chamber',79:'Safari Zone (East)',
  80:'Safari Zone (North)',81:'Safari Zone (West)',82:'Safari Zone (North West)',
  83:'Safari Zone (North East)',84:'Artisan Cave',85:'Southern Island',86:'Battle Frontier'
};

export function areaName(area) {
  if (area === 87) return 'Starter';
  if (area >= 8 && area <= 32) return `Route ${area - 7}`;
  return ({1:'Pallet Town',2:'Viridian City',3:'Cerulean City',4:'Vermilion City',5:'Celadon City',6:'Fuchsia City',7:"Cinnabar Island",33:'Viridian Forest',34:'Mt. Moon',35:'S.S. Anne',36:'Diglett’s Cave',37:'Victory Road',38:'Pokémon Mansion'})[area] || otherAreas[area] || `Area ${area}`;
}

export class PartyCache {
  constructor() { this.entries = new Map(); }
  display(view, pairs, side) {
    if (!view) return [];
    const key = JSON.stringify([view.run, view.participants, view.player]);
    const rows = view.pairs || pairs || [];
    let party = view.party || [];
    if (view.safe || party.length) this.entries.set(side, {key, party:party.map(mon => ({...mon}))});
    else {
      const saved = this.entries.get(side);
      if (saved?.key === key) party = saved.party;
      else if (view.locked && [0,1].includes(view.player)) party = (view.team || []).map(id => {
        const row = rows.find(pair => pair.id === id);
        const species = row?.species[view.player];
        return species ? {species, pair:id, hp:null, maxHp:null, level:null} : null;
      }).filter(Boolean);
    }
    return party.slice(0,6).map(mon => ({...mon, dead:rows.some(pair => pair.id === mon.pair && pair.state === 3)}));
  }
}

export function validSelection(selected, pairs) {
  return [...selected].filter(id => pairs.some(pair => pair.id === id && pair.state === 2)).sort((a,b) => a-b);
}

export function editDraft(selected,id,pairs,state,species={},busy=false) {
  const next=new Set(selected);
  if(busy)return {selected:next,error:'Wait for the current request to finish.'};
  if(next.has(id)){next.delete(id);return {selected:next};}
  const pair=pairs.find(pair=>pair.id===id);
  if(!pair || !id || pair.state!==2)return {selected:next,error:'This pair cannot join the team.'};
  const info=pairExplanation(pair,state,species);
  if(info.blocked)return {selected:next,error:info.reason};
  if(next.size>=6)return {selected:next,error:'Six pairs max. Deselect one first.'};
  next.add(id);return {selected:next};
}

export function createPollLoop(task,delay,{schedule=setTimeout,cancel=clearTimeout}={}) {
  let timer=null,running=false,wakePending=false;
  async function run() {
    timer=null;running=true;
    try {await task();}
    finally {running=false;const next=wakePending?0:delay();wakePending=false;timer=schedule(run,next);}
  }
  return {wake(){
    if(running){wakePending=true;return;}
    if(timer!==null){cancel(timer);timer=null;}
    return run();
  }};
}

export function readingIsFresh(reading,nowSeconds=Date.now()/1000) {
  return Number.isFinite(reading?.updated) && Number.isFinite(nowSeconds)
    && nowSeconds-reading.updated>=-5 && nowSeconds-reading.updated<=8;
}

export function relayLatency(state,current=true) {
  const ping=state?.metrics?.timings?.relayRoundTrip;
  if(!current || state?.relayConnected!==true || !Number.isFinite(ping?.ageMs)
     || ping.ageMs<0 || ping.ageMs>15000 || !Number.isFinite(ping?.lastMs) || ping.lastMs<0)return null;
  return Math.round(ping.lastMs);
}

export function connectionChecks(state,current=true) {
  const relay=current && state?.relayConnected===true;
  const game=(name,view,active,connected=true)=>({name,
    detail:!current?'Companion unavailable':!connected?'Not connected':!active?'Load your save or resume the game':view?.battle?'In battle · keep playing':view?.safe?'Available for updates':'In a menu or dialogue · updates wait',
    online:!!(current && connected && active)});
  return [{name:'Internet relay',online:!!relay,detail:!current?'Companion unavailable':relay?'Connected':'Reconnecting automatically'},
    game('Your game',state?.local,state?.localActive),
    game('Partner’s game',state?.peer,state?.peerActive,relay && state?.peerConnected===true)];
}

export function teamChangeProgress(state, current=true) {
  const ids=state?.acceptedTeam?.ids;
  if(!Array.isArray(ids) || !ids.length) return null;
  const matches=view=>!!view && state.state!=='error' && JSON.stringify(view.run)===JSON.stringify(state.acceptedTeam.run)
    && view.save===1 && Number.isInteger(view.status) && ![1,2].includes(view.status) && view.locked
    && JSON.stringify(view.team)===JSON.stringify(ids)
    && JSON.stringify((view.party||[]).map(mon=>mon.pair).sort((a,b)=>a-b))===JSON.stringify(ids);
  const sides=[['Your game',state.local,state.localActive],['Partner’s game',state.peer,state.peerConnected && state.peerActive]];
  return sides.map(([name,view,active])=>({name,
    applied:!!(current && active && matches(view)),
    detail:!current?'Checking the connection':state.state==='error'?'Check the message above':!active?'Waiting for the game to reconnect':matches(view)?'Team updated':view?.safe?'Waiting for the team change':'Finish the battle or close menus'}));
}

export function pairExplanation(pair, state, species={}) {
  if(pair.state===3) return {label:'Lost pair',reason:'A Pokémon in this pair died. Neither can join the team.'};
  if(pair.state===4) return {label:'Encounter lost',reason:'An encounter failed. This pair cannot join the team.'};
  const missing=pair.species.map((id,side)=>id?null:'AB'[side]).filter(Boolean);
  if(pair.state!==2 || !pair.id) return {label:'Waiting for a partner',reason:missing.length?`Waiting for Player ${missing.join(' and ')} to record their encounter.`:'Both captures are recorded. The games still need to agree on the pair.'};
  const mode=state?.local?.config?.[5]||0;
  if(mode) {
    const types=pair.species.map(id=>species[id]?.types);
    if(types.some(t=>!t?.length)) return {label:'Check the type rule',reason:'Type information is unavailable for this pair.',blocked:true};
    const shared=types[0].slice(0,mode===1?1:2).find(t=>types[1].slice(0,mode===1?1:2).includes(t));
    if(shared) return {label:'Type rule conflict',reason:`Both Pokémon have ${shared.toLowerCase()} as ${mode===1?'their primary type':'a type'}. Choose another pair or change the rule in both games.`,blocked:true};
  }
  if(state?.acceptedTeam?.ids?.includes(pair.id)) return {label:'Team change pending',reason:'Approved by both players. Waiting for both games to show this pair.'};
  if(state?.peerConnected===false) return {label:'Last recorded pair',reason:'You can plan with this pair. Sending a proposal waits for your partner to reconnect.'};
  if(state?.local?.digest!==undefined && state?.peer?.digest!==state.local.digest) return {label:'Waiting for sync',reason:'This pair is recorded. The games still need to agree on their latest captures and deaths.'};
  return {label:'Available',reason:'You can select this pair. Your partner approves before either team changes.'};
}

export function selectionProblem(selected,pairs,state,species) {
  if(selected.size>6)return 'Choose up to six pairs.';
  for(const id of selected) {
    const pair=pairs.find(p=>p.id===id);
    if(!pair || pair.state!==2)return 'A selected pair is no longer available.';
    const info=pairExplanation(pair,state,species);if(info.blocked)return info.reason;
  }
  return '';
}

export function teamComparison(party, ids, pairs, player) {
  const current=(party||[]).map(mon=>({...mon,change:ids.includes(mon.pair)?'Stays':'Leaves the team'}));
  const proposed=ids.map(id=>pairs.find(pair=>pair.id===id)).filter(Boolean)
    .map(pair=>({pair:pair.id,species:pair.species[player],change:(party||[]).some(mon=>mon.pair===pair.id)?'Stays':'Joins the team'}));
  return {current,proposed};
}

export function nextStep(state,current=true) {
  if(!current)return 'Leave the launcher open while we reconnect. If it closed, run play.bat again. You can still plan your team below.';
  if(state?.state==='error')return state.detail || 'Check Connection below, then check the Soul Link rules in both games.';
  if(!state?.localActive)return 'Load your save in mGBA. Keep play.bat open alongside the game.';
  if(!state?.relayConnected || !state?.peerConnected)return 'Keep playing while your partner connects. Both players need play.bat open and the same room code.';
  if(state.acceptedTeam)return state.local?.safe?'Your approval is recorded. Keep playing; the other game may still need to finish a battle or close a menu.':'Finish your battle or close menus and dialogue to apply the approved team. Your partner can keep playing.';
  if(state.proposal)return state.proposal.author==='AB'.indexOf(state.role)?'Your partner has the proposal. You can keep playing while they review it.':'Review the current and proposed teams below, then accept or decline.';
  if(!state.peerActive)return 'Your partner needs to load or resume their save. Your last readings stay visible here.';
  if(!state.canPropose)return 'Keep playing. Team proposals become available once both games have caught up; battles can run separately.';
  return 'All set. Captures and deaths update automatically. Select pairs below when you want to change teams.';
}

export function findPairs(pairs,{filter='all',query='',sort='location'}={},species={}) {
  const normalize=text=>String(text).normalize('NFD').replace(/[\u0300-\u036f]/g,'').toLowerCase();
  const terms=normalize(query).trim().split(/\s+/).filter(Boolean);
  const result=pairs.filter(pair=>{
    if(!(filter==='all' || (filter==='alive'?pair.state===2:filter==='waiting'?pair.state<=1:pair.state>=3)))return false;
    const text=normalize([areaName(pair.area),...pair.species.map(id=>species[id]?.name||'Waiting')].join(' '));
    return terms.every(term=>text.includes(term));
  });
  const name=pair=>pair.species.map(id=>species[id]?.name||'Waiting').join(' ');
  return result.sort((a,b)=>(sort==='name'?name(a).localeCompare(name(b),'en'):sort==='status'?a.state-b.state: a.area-b.area)||a.id-b.id);
}

export class AlertTracker {
  constructor(){this.scope=null;this.seen=new Set();}
  observe(state){
    const scope=JSON.stringify([state.commandSession,state.local?.run,state.local?.participants,state.role]);
    const rows=state.local?.pairs||[];
    const candidates=rows.filter(pair=>pair.state===3 && pair.id).map(pair=>({key:'death:'+pair.id,kind:'death',pair}));
    if(state.proposal && state.proposal.author!=='AB'.indexOf(state.role))candidates.push({key:'proposal:'+state.proposal.id,kind:'proposal'});
    const first=scope!==this.scope;
    if(first){this.scope=scope;this.seen=new Set();}
    const alerts=[];
    for(const item of candidates){if(!first && !this.seen.has(item.key))alerts.push(item);this.seen.add(item.key);}
    while(this.seen.size>216)this.seen.delete(this.seen.values().next().value);
    return alerts;
  }
}

if (typeof document !== 'undefined') {
  const $ = id => document.getElementById(id);
  const systemTheme = matchMedia('(prefers-color-scheme: dark)');
  let chosenTheme = null;
  try { chosenTheme = localStorage.getItem('soullink-theme'); } catch {}
  if (!['light','dark'].includes(chosenTheme)) chosenTheme = null;
  function applyTheme() {
    const dark = chosenTheme ? chosenTheme === 'dark' : systemTheme.matches;
    document.documentElement.dataset.theme = dark ? 'dark' : 'light';
    $('theme').textContent = dark ? 'Light theme' : 'Dark theme';
    $('theme').setAttribute('aria-pressed',String(dark));
  }
  $('theme').addEventListener('click',()=>{
    chosenTheme = document.documentElement.dataset.theme === 'dark' ? 'light' : 'dark';
    try { localStorage.setItem('soullink-theme',chosenTheme); } catch {}
    applyTheme();
    fetch('theme',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({theme:chosenTheme}),signal:AbortSignal.timeout(3000)}).catch(()=>{});
  });
  systemTheme.addEventListener('change',()=>{if (!chosenTheme) applyTheme();});
  applyTheme();
  fetch('theme',{signal:AbortSignal.timeout(3000)}).then(response=>response.json()).then(preference=>{
    if (['light','dark'].includes(preference.theme)) {chosenTheme=preference.theme;applyTheme();}
  }).catch(()=>{});
  const el = (tag, cls, text) => { const node=document.createElement(tag); if(cls) node.className=cls; if(text!==undefined) node.textContent=text; return node; };
  const cache = new PartyCache();
  let species={}, state=null, selected=new Set(), initial=true, busy=false, pairKey='', partyKey='', oldNote='', failed=false, historyKey='', duoKey='', failureCount=0, selectionScope='', changeKey='';
  const alerts=new AlertTracker();
  let sound=false,desktopAlerts=false,audio=null,unread=0;
  function alertNotice(message){
    notify(message);
    if(document.hidden){unread++;document.title=`(${unread}) Soul Link · ${message}`;}
    if(sound && audio?.state==='running'){
      const oscillator=audio.createOscillator(),gain=audio.createGain();oscillator.connect(gain);gain.connect(audio.destination);
      oscillator.frequency.value=660;gain.gain.setValueAtTime(.035,audio.currentTime);gain.gain.exponentialRampToValueAtTime(.001,audio.currentTime+.18);
      oscillator.start();oscillator.stop(audio.currentTime+.2);
    }
    if(desktopAlerts && document.hidden && 'Notification' in window && Notification.permission==='granted'){
      try{const notice=new Notification('Soul Link',{body:message,tag:'soul-link-update',silent:true});notice.onclick=()=>{window.focus();notice.close();};setTimeout(()=>notice.close(),10000);}catch{}
    }
  }
  $('sound').addEventListener('click',async()=>{
    if(!sound){try{audio ||= new (window.AudioContext||window.webkitAudioContext)();await audio.resume();if(audio.state!=='running')throw Error();}catch{notify('Sound is unavailable in this browser.');return;}}
    sound=!sound;$('sound').textContent=sound?'Sound on':'Sound off';$('sound').setAttribute('aria-pressed',String(sound));
  });
  $('desktop-alerts').addEventListener('click',async()=>{
    if(desktopAlerts){desktopAlerts=false;}
    else{if(!('Notification' in window)){notify('Desktop alerts are unavailable in this browser. Tab alerts still work.');return;}
      try{desktopAlerts=(await Notification.requestPermission())==='granted';}catch{desktopAlerts=false;}
      if(!desktopAlerts)notify('Desktop alerts are blocked. Tab alerts still work.');}
    $('desktop-alerts').textContent=desktopAlerts?'Desktop alerts on':'Desktop alerts off';$('desktop-alerts').setAttribute('aria-pressed',String(desktopAlerts));
  });
  let toastTimer;
  function notify(message) { $('toast').textContent=message; $('toast').hidden=false; clearTimeout(toastTimer); toastTimer=setTimeout(()=>{$('toast').hidden=true;},6500); }
  const monName = id => id ? (species[id]?.name || 'Unknown Pokémon') : 'Waiting';
  function sprite(id) {
    const file=species[id]?.icon;
    if (!file || !/^\d+\.png$/.test(file)) return el('span','sprite empty-symbol',id?'?':'—');
    const img=el('img','sprite'); img.src='assets/'+file; img.alt=''; img.width=64; img.height=64;
    return img;
  }
  function replaceKeepingFocus(target,...nodes) {
    const active=document.activeElement;
    const key=target.contains(active)?active.dataset.focusKey:null;
    target.replaceChildren(...nodes);
    if(key) {
      const replacement=[...target.querySelectorAll('[data-focus-key]')].find(node=>node.dataset.focusKey===key && !node.disabled);
      (replacement || target).focus({preventScroll:true});
    }
  }
  function partyList(target, party) {
    const nodes=[];
    for(let i=0;i<6;i++) {
      const mon=party[i], row=el('li','mon'+(mon?.dead?' dead':''));
      row.append(el('span','slot',String(i+1).padStart(2,'0')));
      if (!mon) { row.append(el('span','empty-symbol','—'),el('span','empty','Empty slot')); }
      else {
        row.append(sprite(mon.species));
        const labels=el('div'); labels.append(el('span','mon-name',monName(mon.species)));
        const unknown=mon.hp==null || mon.level==null || !mon.maxHp;
        const detail=mon.dead?'Dead · pair lost':unknown?'Approved team · HP unknown':`Lv. ${mon.level} · ${mon.hp}/${mon.maxHp} HP`;
        labels.append(el('span','mon-detail',detail));
        if(!unknown) {
          const ratio=mon.dead?0:mon.hp/mon.maxHp, hp=el('progress','hp'+(ratio<=.2?' critical':ratio<=.5?' low':''));
          hp.max=mon.maxHp; hp.value=mon.dead?0:mon.hp; hp.setAttribute('aria-label','HP of '+monName(mon.species)); labels.append(hp);
        }
        row.append(labels);
        if(mon.pair) {
          const pick=el('button','quiet team-pick',selected.has(mon.pair)?'Selected':'Select pair');
          const pair=rows().find(p=>p.id===mon.pair);
          pick.disabled=!pair || pair.state!==2 || (!!pairExplanation(pair,state,species).blocked && !selected.has(mon.pair)) || busy;
          pick.dataset.focusKey=target.id+'-'+mon.pair;
          pick.setAttribute('aria-pressed',String(selected.has(mon.pair)));
          pick.addEventListener('click',()=>togglePair(mon.pair));labels.append(pick);
        }
      }
      nodes.push(row);
    }
    replaceKeepingFocus(target,...nodes);
  }
  function rows() { return state?.local?.pairs || []; }
  function fresh() { return !failed && readingIsFresh(state); }
  function togglePair(id) {
    const result=editDraft(selected,id,rows(),state,species,busy);
    if(result.error){notify(result.error);return;}
    selected=result.selected;
    buttons();
  }
  function buttons() {
    renderPairs();renderDuoPairs();renderParties();
    renderTeamChange();renderComparison($('draft-comparison'),validSelection(selected,rows()));
    $('next-step').textContent=nextStep(state,fresh());
    const checks=connectionChecks(state,fresh());
    $('connection-checks').replaceChildren(...checks.map(check=>{
      const row=el('li',check.online?'online':''), text=el('div');
      text.append(el('strong',null,check.name),el('span',null,check.detail));row.append(text);return row;
    }));
    $('selection-count').textContent=selected.size?`${selected.size} pair${selected.size>1?'s':''} out of 6`:'No pairs selected';
    const problem=selectionProblem(selected,rows(),state,species);
    $('selection-reason').textContent=problem || (!fresh()?'Connection interrupted. You can plan with the last recorded pairs; sending waits for a fresh reading.':state?.acceptedTeam?'The approved team is still being applied.':state?.proposal?'A proposal is already waiting for a reply.':!state?.canPropose?'You can plan a team here. Send it once both games are connected and up to date.':'Your partner approves before either team changes.');
    $('propose').disabled=busy || !fresh() || !state.canPropose || !!state.proposal || !selected.size || !!problem;
    $('sync').disabled=busy || !fresh() || !state.local || state.state==='offline';
    $('current').disabled=busy || !rows().length;
    $('accept').disabled=busy || !fresh(); $('reject').disabled=busy || !fresh();
  }
  function renderPairs(force=false) {
    const pairs=rows(), filter=$('filter').value,query=$('search').value,sort=$('sort').value;
    const key=JSON.stringify([pairs,filter,query,sort,busy,!!state?.canPropose,state?.local?.config,state?.acceptedTeam,state?.peerConnected,state?.local?.digest,state?.peer?.digest, [...selected]]);
    if(!force && key===pairKey) return;
    pairKey=key;
    const visible=findPairs(pairs,{filter,query,sort},species);
    $('search-count').textContent=`${visible.length} of ${pairs.length} pairs shown`;
    const nodes=visible.map(pair=>{
      const row=el('div','pair-row'+(selected.has(pair.id)?' selected':'')+(pair.state===3?' dead':pair.state!==2?' waiting':''));
      const info=pairExplanation(pair,state,species);
      const label=el('label','pair-pick'), check=el('input'); check.type='checkbox'; check.checked=selected.has(pair.id); check.disabled=busy || pair.state!==2 || !pair.id || (!!info.blocked && !selected.has(pair.id));
      check.dataset.focusKey='pair-'+pair.id;
      check.setAttribute('aria-label',`Select ${monName(pair.species[0])} and ${monName(pair.species[1])}, ${areaName(pair.area)}`);
      check.addEventListener('change',()=>{
        check.checked=selected.has(pair.id);
        togglePair(pair.id);
      });
      label.append(check);row.append(label);
      for(let side=0;side<2;side++) {const mon=el('div','pair-mon'), name=el('span',null,monName(pair.species[side])); const wrapper=el('div');wrapper.append(el('small',null,'Player '+'AB'[side]),name);mon.append(sprite(pair.species[side]),wrapper);row.append(mon);}
      const place=el('div','pair-place',areaName(pair.area)); place.append(el('small',null,info.label),el('p','pair-reason',info.reason));row.append(place);
      return row;
    });
    if(!nodes.length) nodes.push(el('div','no-pairs',pairs.length?'No matching pairs. Try another name, location or filter.':'No pairs yet. Your captures will appear here once the duo is ready.'));
    replaceKeepingFocus($('pair-list'),...nodes);
  }
  function showTab(tab) {
    if(!['duo','pairs','activity','help'].includes(tab)) tab='duo';
    for(const name of ['duo','pairs','activity','help']) $(name).hidden=name!==tab;
    $('team-selection').hidden=!['duo','pairs'].includes(tab);
    $('draft-review').hidden=!['duo','pairs'].includes(tab);
    document.querySelectorAll('[data-tab]').forEach(button=>button.setAttribute('aria-current',button.dataset.tab===tab?'page':'false'));
    history.replaceState(null,'','#'+tab);
  }
  function render(next) {
    const scope=JSON.stringify([next.local?.run,next.local?.participants,next.role]);
    if(selectionScope && scope!==selectionScope){selected=new Set();initial=true;pairKey=partyKey=duoKey='';}
    selectionScope=scope;
    state=next;
    const pairs=rows();
    if(initial && state.local) {selected=new Set(validSelection(new Set(state.local.team?.length?state.local.team:state.local.party?.map(mon=>mon.pair)||[]),pairs));initial=false;}
    const clean=validSelection(selected,pairs); if(clean.length!==selected.size){selected=new Set(clean);pairKey='';}
    $('role').textContent='Player '+state.role+' · '+(state.role==='A'?'room creator':'joined room');
    $('local-role').textContent='PLAYER '+state.role; $('peer-role').textContent='PLAYER '+(state.role==='A'?'B':'A');
    $('status-title').textContent=state.title; $('status-detail').textContent=state.detail;
    const ping=relayLatency(state,fresh());
    if(ping!==null) $('status-detail').textContent+=` · Relay ${ping} ms`;
    $('status-light').className='signal '+state.state;
    const alive=pairs.filter(pair=>pair.state===2).length;
    $('pair-count').textContent=`${pairs.length} pair${pairs.length>1?'s':''} · ${alive} living`;
    $('peer-subtitle').textContent=state.peer?(state.peerActive?'Last received reading':'Saved reading · waiting for your partner. HP may be out of date.'):'Waiting for your partner';
    $('last-sync').textContent=state.lastSuccess?'Last sync · '+new Date(state.lastSuccess*1000).toLocaleTimeString('en-GB'):'';
    const proposal=state.proposal; $('proposal').hidden=!proposal;
    if(proposal) {
      const own=proposal.author==='AB'.indexOf(state.role);
      $('proposal-title').textContent=own?'Waiting for your partner to decide':'Your partner proposes this team';
      renderComparison($('proposal-names'),proposal.ids);
      $('reject').textContent=own?'Cancel proposal':'Decline'; $('accept').hidden=own;
    }
    if(state.note && state.note!==oldNote) notify(state.note);
    oldNote=state.note;
    const notices=alerts.observe(state).map(alert=>alert.kind==='proposal'?'Your partner proposed a team. Open the journal to review it.':`${alert.pair.species.map(monName).join(' / ')} · pair lost.`);
    if(notices.length)alertNotice(notices.join(' '));
    renderSessionSummary();renderHistory();buttons();
  }
  function renderParties() {
    const pairs=rows();
    const sameDuo=!state?.local || !state?.peer || JSON.stringify([state?.local.run,state?.local.participants])===JSON.stringify([state?.peer.run,state?.peer.participants]);
    const local=cache.display(state?.local,pairs,'local'), peer=cache.display(sameDuo?state?.peer:null,pairs,'peer');
    const nextPartyKey=JSON.stringify([local,peer,[...selected],busy,state?.local?.config]);
    if(nextPartyKey!==partyKey){partyList($('local-party'),local);partyList($('peer-party'),peer);partyKey=nextPartyKey;}
  }
  function renderComparison(target,ids) {
    const pairs=rows(),local=state?.local,peer=state?.peer;
    const sameDuo=local && peer && JSON.stringify([local.run,local.participants])===JSON.stringify([peer.run,peer.participants]);
    const sides=[['Your team',local,'local'],['Partner’s team',sameDuo?peer:null,'peer']];
    const models=sides.map(([name,view,side])=>({name,known:!!view,stale:!fresh() || !(side==='local'?state?.localActive:state?.peerActive) || !view?.safe,
      ...teamComparison(cache.display(view,pairs,side),ids,pairs,view?.player??(side==='local'?'AB'.indexOf(state?.role||'A'):1-'AB'.indexOf(state?.role||'A')))}));
    const key=JSON.stringify(models);if(target.dataset.renderKey===key)return;target.dataset.renderKey=key;
    const nodes=models.map(model=>{
      const section=el('section','comparison-side');section.append(el('h3',null,model.name));
      const columns=el('div','comparison-columns');
      for(const [label,mons] of [[model.stale?'Last recorded':'Current',model.current],['Proposed',model.proposed]]) {
        const column=el('div');column.append(el('h4',null,label));const list=el('ul','comparison-list');
        for(const mon of mons){const row=el('li');row.append(sprite(mon.species));const text=el('div');text.append(el('strong',null,monName(mon.species)),el('small',mon.change==='Stays'?'':'changed',label==='Proposed' && !model.known?'Current team unavailable':mon.change));row.append(text);list.append(row);}
        if(!mons.length)list.append(el('li','small',label==='Proposed'?'Select a pair':model.known?'No Pokémon recorded':'Waiting for a reading'));
        column.append(list);columns.append(column);
      }
      section.append(columns);return section;
    });target.replaceChildren(...nodes);
  }
  function previewTeam(target,ids) {
    target.replaceChildren(...ids.map(id=>{
      const pair=rows().find(pair=>pair.id===id), item=el('div','team-preview-pair');
      if(pair) {item.append(sprite(pair.species[0]),sprite(pair.species[1]),el('span',null,pair.species.map(monName).join(' / ')));}
      else item.append(el('span',null,'Pair '+id));
      return item;
    }));
  }
  function renderTeamChange() {
    const progress=teamChangeProgress(state,fresh()), card=$('team-change');card.hidden=!progress;
    if(!progress){changeKey='';return;}
    const key=JSON.stringify([progress,state.acceptedTeam,rows().map(pair=>[pair.id,pair.species])]);if(key===changeKey)return;changeKey=key;
    previewTeam($('approved-preview'),state.acceptedTeam.ids);
    $('team-change-detail').textContent=progress.every(side=>side.applied)?'Both readings match. Waiting for final confirmation.':'Keep both launchers open. The change finishes when both games are available.';
    $('team-change-progress').replaceChildren(...progress.map(side=>{
      const item=el('li',side.applied?'applied':''), marker=el('span','receipt-mark',side.applied?'✓':'…');marker.setAttribute('aria-hidden','true');
      const body=el('div');body.append(el('strong',null,side.name),el('span',null,side.detail));item.append(marker,body);return item;
    }));
  }
  function renderDuoPairs() {
    const key=JSON.stringify([rows(),[...selected],busy,state?.local?.config,state?.acceptedTeam,state?.peerConnected,state?.local?.digest,state?.peer?.digest]);if(key===duoKey)return;duoKey=key;
    const nodes=rows().filter(p=>p.state===2).map(pair=>{
      const info=pairExplanation(pair,state,species), button=el('button','duo-pair'+(selected.has(pair.id)?' selected':''));
      button.dataset.focusKey='duo-'+pair.id;
      button.setAttribute('aria-pressed',String(selected.has(pair.id)));button.disabled=busy || (!!info.blocked && !selected.has(pair.id));
      button.append(sprite(pair.species[0]),sprite(pair.species[1]));
      const text=el('span');text.append(el('strong',null,pair.species.map(monName).join(' / ')),el('small',null,areaName(pair.area)+' · '+info.label));button.append(text);
      button.title=info.reason;button.addEventListener('click',()=>togglePair(pair.id));return button;
    });
    if(!nodes.length)nodes.push(el('p','small','No living pairs yet. Waiting captures are listed in Choose your team.'));
    replaceKeepingFocus($('duo-pairs'),...nodes);
  }
  function renderSessionSummary(){
    const summary=state.sessionSummary,counts=summary?.counts;
    $('session-summary').hidden=!counts;
    if(!counts)return;
    $('session-start').textContent='Recorded since '+new Date(summary.started*1000).toLocaleTimeString('en-GB')+' · both players';
    const labels={capture:'Captures',death:'Deaths',failed:'Failed encounters',evolution:'Evolutions','team-applied':'Team changes'};
    $('session-counts').replaceChildren(...Object.entries(labels).map(([key,label])=>{const item=el('div');item.append(el('dt',null,label),el('dd',null,String(counts[key]||0)));return item;}));
  }
  function renderHistory() {
    const key=JSON.stringify([state.history,state.historySaved]);if(key===historyKey)return;historyKey=key;
    const nodes=(state.history||[]).map(event=>{
      const row=el('li','activity-row'), time=el('time',null,new Date(event.time*1000).toLocaleString('en-GB'));
      const titles={capture:'Capture recorded',failed:'Encounter lost',death:'Death recorded',evolution:'Evolution recorded','pair-ready':'Pair ready','team-proposed':'Team proposed','team-approved':'Team approved','team-applied':'Both teams updated','team-closed':'Proposal closed'};
      let detail=Array.isArray(event.species)?event.species.map(monName).join(' / '):event.species?monName(event.species):'';
      if([0,1].includes(event.role))detail='Player '+'AB'[event.role]+(detail?' · '+detail:'');
      if(event.area)detail+=(detail?' · ':'')+areaName(event.area);
      if(event.ids)detail+=(detail?' · ':'')+event.ids.length+' pair'+(event.ids.length===1?'':'s');
      const body=el('div');body.append(el('strong',null,titles[event.kind]||'Duo update'),el('p',null,detail));row.append(time,body);return row;
    });
    if(!nodes.length)nodes.push(el('li','small','Your captures and team changes will appear here.'));
    $('activity-list').replaceChildren(...nodes);
    $('history-note').textContent=state.historySaved===false?'Recent entries have not been saved yet. Keep the launcher open.':'Recorded when this companion receives an update. Keeps the latest 128 entries on this PC.';
  }
  async function action(request) {
    if(busy || !fresh()) return;
    busy=true;buttons();
    try { const response=await fetch('actions',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(request),signal:AbortSignal.timeout(4000)}); const result=await response.json(); if(!response.ok) throw Error(result.error||'The game cannot receive this request.');notify(request.action==='sync'?'Sync requested.':'Request sent. Waiting for the companion to confirm.'); }
    catch(error) {notify(error.message==='Failed to fetch'?'The companion has stopped. Run play.bat again.':error.message);}
    finally {busy=false;buttons();}
  }
  $('sync').addEventListener('click',()=>action({action:'sync'}));
  $('propose').addEventListener('click',()=>action({action:'propose',ids:validSelection(selected,rows())}));
  for(const vote of ['accept','reject']) $(vote).addEventListener('click',()=>state?.proposal&&action({action:vote,id:state.proposal.id}));
  $('current').addEventListener('click',()=>{selected=new Set(validSelection(new Set(state?.local?.party?.map(mon=>mon.pair)||[]),rows()));renderPairs(true);buttons();});
  $('filter').addEventListener('change',()=>renderPairs(true));
  $('search').addEventListener('input',()=>renderPairs());
  $('sort').addEventListener('change',()=>renderPairs());
  $('choose').addEventListener('click',()=>showTab('pairs'));
  const navButtons=[...document.querySelectorAll('[data-tab]')];
  navButtons.forEach((button,index)=>{
    button.addEventListener('click',()=>showTab(button.dataset.tab));
    button.addEventListener('keydown',event=>{
      let next;
      if(event.key==='ArrowRight')next=(index+1)%navButtons.length;
      if(event.key==='ArrowLeft')next=(index+navButtons.length-1)%navButtons.length;
      if(event.key==='Home')next=0;if(event.key==='End')next=navButtons.length-1;
      if(next===undefined)return;event.preventDefault();navButtons[next].focus();showTab(navButtons[next].dataset.tab);
    });
  });
  // Make partner proposals visible even while the player is on the duo page.
  document.querySelector('main').prepend($('proposal'));
  $('proposal').after($('team-change'));
  document.querySelector('main').append($('draft-review'),$('team-selection'));
  const browserTimings=[];
  const diagnostics=el('a',null,'Download network timings');diagnostics.href='diagnostics';diagnostics.download='network-timings.json';$('help').append(diagnostics);
  diagnostics.addEventListener('click',async event=>{
    event.preventDefault();
    try {const response=await fetch('diagnostics');if(!response.ok)throw Error('Export failed');
      const report=await response.json();report.browserReadings=browserTimings;
      const url=URL.createObjectURL(new Blob([JSON.stringify(report,null,2)],{type:'application/json'}));
      const link=el('a');link.href=url;link.download='network-timings.json';link.click();setTimeout(()=>URL.revokeObjectURL(url),1000);
    }catch {notify('Could not download timings. Try again while the companion is running.');}
  });
  showTab(location.hash.slice(1));
  window.addEventListener('hashchange',()=>{if(location.hash!=='#content')showTab(location.hash.slice(1));});
  async function poll() {
    try {
      if(!Object.keys(species).length) {const response=await fetch('species',{signal:AbortSignal.timeout(4000)});if(!response.ok)throw Error('species');species=await response.json();}
      const fetchStarted=performance.now();
      const response=await fetch('state',{signal:AbortSignal.timeout(4000)});
      if(!response.ok)throw Error('state');
      const next=await response.json();
      if(!readingIsFresh(next))throw Error('stale');
      failed=false;failureCount=0;render(next);
      browserTimings.push({fetchAndRenderMs:Math.round(performance.now()-fetchStarted),stateAgeMs:Math.max(0,Math.round(Date.now()-next.updated*1000)),hidden:document.hidden});
      if(browserTimings.length>128)browserTimings.shift();
      if(!fresh())throw Error('stale');
    } catch {
      failed=true;failureCount++;
      $('status-title').textContent=failureCount<8?'Checking the connection…':'The companion is not responding';
      $('status-detail').textContent=failureCount<8?'Your last readings are still here. Retrying automatically.':'Your last readings are still here. If the launcher has closed, run play.bat again.';
      $('peer-subtitle').textContent='Saved reading · connection interrupted. HP may be out of date.';
      $('status-light').className='signal offline';partyKey='';buttons();
    }
  }
  const polling=createPollLoop(poll,()=>document.hidden?3000:1000);
  document.addEventListener('visibilitychange',()=>{if(!document.hidden){unread=0;document.title='Soul Link · Adventure journal';polling.wake();}});
  polling.wake();
}
