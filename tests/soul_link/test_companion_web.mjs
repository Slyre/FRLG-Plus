import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import {PartyCache,validSelection,areaName,pairExplanation,selectionProblem,teamChangeProgress,connectionChecks,readingIsFresh,createPollLoop,editDraft,relayLatency,teamComparison,nextStep,findPairs,AlertTracker} from '../../remote/companion-web/app.js';
const pairs=[{id:1,state:2,species:[7,4]},{id:2,state:3,species:[1,25]}];
const view=()=>({run:[1,2],participants:[10,20],player:1,safe:true,locked:true,team:[1],pairs,party:[{species:4,pair:1,hp:20,maxHp:20,level:5}]});
test('combat retains last party and real stats without inventing cold-start values',()=>{
  const cache=new PartyCache(), v=view();cache.display(v,pairs,'peer');v.safe=false;v.party=[];
  assert.equal(cache.display(v,pairs,'peer')[0].hp,20);
  const cold=new PartyCache().display(v,pairs,'peer');assert.equal(cold[0].species,4);assert.equal(cold[0].hp,null);
  v.safe=true;assert.deepEqual(cache.display(v,pairs,'peer'),[]);
});
test('deaths overlay preserved party without mutating telemetry',()=>{
  const cache=new PartyCache(), v=view();cache.display(v,pairs,'peer');v.safe=false;v.party=[];
  v.pairs=[{...pairs[0],state:3}];assert.equal(cache.display(v,pairs,'peer')[0].dead,true);
  assert.equal(pairs[0].state,2);
});
test('run identity prevents reuse of an unrelated cached party',()=>{
  const cache=new PartyCache(),v=view();cache.display(v,pairs,'peer');v.safe=false;v.party=[];v.locked=false;v.run=[3,4];
  assert.deepEqual(cache.display(v,pairs,'peer'),[]);
});
test('dead and absent pairs are removed from selection',()=>{
  assert.deepEqual(validSelection(new Set([2,9,1]),pairs),[1]);
  assert.equal(areaName(87),'Starter');assert.equal(areaName(8),'Route 1');
});
test('type preview follows the pair-only native primary or any-type rule',()=>{
  const pair={id:3,state:2,species:[6,130]};const species={6:{types:['FIRE','FLYING']},130:{types:['WATER','FLYING']}};
  const state={local:{config:[1,1,1,1,1,1]}};
  assert.equal(pairExplanation(pair,state,species).blocked,undefined);
  state.local.config[5]=2;
  assert.match(selectionProblem(new Set([3]),[pair],state,species),/flying/);
  state.local.config[5]=0;assert.equal(selectionProblem(new Set([3]),[pair],state,species),'');
  assert.match(selectionProblem(new Set([9]),[pair],state,species),/no longer/);
});
test('pair explanations distinguish missing captures, failed, dead and approved changes',()=>{
  assert.match(pairExplanation({id:0,state:1,species:[1,0]},{}).reason,/Player B/);
  assert.match(pairExplanation({state:4},{}).reason,/failed/);
  assert.match(pairExplanation({state:3},{}).reason,/died/);
  assert.equal(pairExplanation(pairs[0],{acceptedTeam:{ids:[1]}}).label,'Team change pending');
});

test('approved team progress requires real matching parties, selection and live readings',()=>{
  const applied={...view(),save:1,status:0}, pending={...applied,party:[{pair:2}]};
  const state={state:'syncing',acceptedTeam:{ids:[1],run:[1,2]},local:applied,peer:pending,localActive:true,peerConnected:true,peerActive:true};
  assert.deepEqual(teamChangeProgress(state).map(side=>side.applied),[true,false]);
  state.peer=applied;assert.deepEqual(teamChangeProgress(state).map(side=>side.applied),[true,true]);
  for(const field of [{locked:false},{team:[2]},{status:1},{save:2},{run:[3,4]},{party:[]},{party:[{pair:1},{pair:1}]}]) {
    state.peer={...applied,...field};assert.equal(teamChangeProgress(state)[1].applied,false);
  }
  state.peer=applied;state.peerConnected=false;
  assert.equal(teamChangeProgress(state)[1].applied,false);
  assert.deepEqual(teamChangeProgress(state,false).map(side=>side.applied),[false,false]);
  state.localActive=false;assert.equal(teamChangeProgress(state)[0].applied,false);
  state.localActive=true;state.state='error';assert.equal(teamChangeProgress(state)[0].applied,false);
  state.acceptedTeam=null;assert.equal(teamChangeProgress(state),null);
});

test('connection checks distinguish relay, local game and partner without treating battles as disconnects',()=>{
 const state={relayConnected:true,localActive:true,peerActive:true,peerConnected:true,local:{safe:true},peer:{battle:true,safe:false}};
 assert.deepEqual(connectionChecks(state).map(c=>c.online),[true,true,true]);
 assert.match(connectionChecks(state)[2].detail,/In battle/);
 state.peerConnected=false;assert.equal(connectionChecks(state)[2].online,false);
 state.relayConnected=false;assert.equal(connectionChecks(state)[0].online,false);
 assert.equal(connectionChecks(state)[1].online,true);
 assert.ok(connectionChecks(state,false).every(c=>!c.online));
 assert.ok(connectionChecks(null).every(c=>!c.online));
});

test('freshness rejects expired or invalid readings before replacing the last valid UI state',()=>{
 assert.equal(readingIsFresh({updated:100},108),true);
 assert.equal(readingIsFresh({updated:100},108.001),false);
 assert.equal(readingIsFresh({updated:100},95),true);
 assert.equal(readingIsFresh({updated:100},94.999),false);
 for(const updated of [undefined,null,'100',NaN,Infinity,-Infinity])assert.equal(readingIsFresh({updated},100),false);
 assert.equal(readingIsFresh(null,100),false);
 assert.equal(readingIsFresh({updated:100},NaN),false);
});

test('returning to the companion refreshes immediately without overlapping polls or multiplying timers',async()=>{
 let release,calls=0,nextId=0;const timers=new Map();
 const loop=createPollLoop(()=>{calls++;return new Promise(resolve=>release=resolve);},()=>3000,
 {schedule:(fn,ms)=>{const id=++nextId;timers.set(id,{fn,ms});return id;},cancel:id=>timers.delete(id)});
 const first=loop.wake();loop.wake();loop.wake();assert.equal(calls,1);
 release();await first;assert.equal(timers.size,1);assert.equal([...timers.values()][0].ms,0);
 const second=loop.wake();assert.equal(timers.size,0);assert.equal(calls,2);
 release();await second;assert.equal(timers.size,1);assert.equal([...timers.values()][0].ms,3000);
 const third=loop.wake();assert.equal(timers.size,0);assert.equal(calls,3);
 release();await third;assert.equal(timers.size,1);
});

test('offline drafts use recorded living pairs without requests, respect rules and never mutate the previous selection',()=>{
 const selected=new Set([1]), state={peerConnected:false}, alive=[...pairs,{id:3,state:2,species:[16,19]}];
 const add=editDraft(selected,3,alive,state);assert.deepEqual([...add.selected],[1,3]);assert.deepEqual([...selected],[1]);
 assert.deepEqual([...editDraft(add.selected,1,alive,state).selected],[3]);
 for(const id of [0,2,9])assert.ok(editDraft(selected,id,alive,state).error);
 assert.ok(editDraft(selected,3,alive,state,{},true).error);
 const full=new Set([1,2,4,5,6,7]);assert.match(editDraft(full,3,alive,state).error,/Six/);
 const typed={local:{config:[0,0,0,0,0,2]}};
 assert.ok(editDraft(selected,3,alive,typed,{16:{types:['NORMAL']},19:{types:['NORMAL']}}).error);
 assert.deepEqual([...editDraft(new Set([3]),3,alive,typed).selected],[]);
});

test('relay latency only displays a recent sample on a current connected reading',()=>{
 const s={relayConnected:true,metrics:{timings:{relayRoundTrip:{ageMs:100,lastMs:12.4}}}};
 assert.equal(relayLatency(s),12);assert.equal(relayLatency(s,false),null);
 s.relayConnected=false;assert.equal(relayLatency(s),null);s.relayConnected=true;
 for(const age of [null,undefined,NaN,-1,15001]){s.metrics.timings.relayRoundTrip.ageMs=age;assert.equal(relayLatency(s),null);}
 s.metrics.timings.relayRoundTrip.ageMs=0;s.metrics.timings.relayRoundTrip.lastMs=0;assert.equal(relayLatency(s),0);
});

test('team review compares actual party membership for either player without mutating it',()=>{
  const rows=[{id:1,species:[7,4]},{id:3,species:[25,133]}];
  const party=[{pair:1,species:4},{pair:2,species:19},{pair:0,species:10}];
  const result=teamComparison(party,[1,3],rows,1);
  assert.deepEqual(result.current.map(mon=>mon.change),['Stays','Leaves the team','Leaves the team']);
  assert.deepEqual(result.proposed.map(mon=>[mon.species,mon.change]),[[4,'Stays'],[133,'Joins the team']]);
  assert.equal(party[0].change,undefined);
  assert.deepEqual(teamComparison([],[],rows,0),{current:[],proposed:[]});
});
test('next step preserves errors and explains explicit approval and independent battles',()=>{
  const base={role:'B',localActive:true,peerActive:true,relayConnected:true,peerConnected:true,canPropose:true,local:{safe:false}};
  assert.match(nextStep(base,false),/reconnect/);
  assert.equal(nextStep({...base,state:'error',detail:'Rules differ'}),'Rules differ');
  assert.match(nextStep({...base,proposal:{author:1}}),/partner has the proposal/);
  assert.match(nextStep({...base,proposal:{author:0}}),/accept or decline/);
  assert.match(nextStep({...base,acceptedTeam:{ids:[1]}}),/partner can keep playing/);
  assert.match(nextStep({...base,peerConnected:false}),/same room code/);
});

test('pair search combines names and places, normalizes accents, filters and sorts without mutation',()=>{
 const source=[{id:2,area:9,state:3,species:[25,133]},{id:1,area:87,state:2,species:[7,4]}];
 const names={25:{name:'Pikachu'},133:{name:'Eevee'},7:{name:'Squirtle'},4:{name:'Charmander'}};
 assert.deepEqual(findPairs(source,{query:'pikachu route 2'},names).map(p=>p.id),[2]);
 assert.equal(findPairs(source,{query:'PÍKACHU',filter:'alive'},names).length,0);
 assert.deepEqual(findPairs(source,{sort:'name'},names).map(p=>p.id),[2,1]);
 assert.deepEqual(findPairs(source,{sort:'status'},names).map(p=>p.id),[1,2]);
 assert.deepEqual(source.map(p=>p.id),[2,1]);
});
test('all native encounter areas and Safari subdivisions have searchable English names',()=>{
 const native=readFileSync(new URL('../../src/data/nuzlocke_areas.h',import.meta.url),'utf8');
 const ids=new Set([...native.matchAll(/\[MAPSEC_\w+\]\s*=\s*0x([0-9A-Fa-f]+)/g)].map(match=>parseInt(match[1],16)));
 for(let id=79;id<=83;id++)ids.add(id);
 ids.add(87);
 for(const id of ids)assert.doesNotMatch(areaName(id),/^Area /,`Missing canonical name for ${id}`);
 const source=[{id:1,area:40,state:2,species:[19,16]},{id:2,area:39,state:3,species:[25,133]},
  {id:3,area:44,state:2,species:[81,100]},{id:4,area:68,state:1,species:[86,0]},
  {id:5,area:82,state:2,species:[102,111]},{id:6,area:86,state:2,species:[143,131]}];
 for(const [query,id] of [['rock tunnel',1],['safari zone',2],['power plant',3],['icefall cave',4],['safari north west',5],['battle frontier',6]]){
   assert.ok(findPairs(source,{query}).some(pair=>pair.id===id),`Cannot search ${query}`);
 }
 assert.deepEqual(findPairs(source,{query:'safari',filter:'alive'}).map(pair=>pair.id),[5]);
 assert.equal(areaName(84),'Artisan Cave');assert.equal(areaName(85),'Southern Island');
 assert.equal(areaName(65535),'Area 65535');
});
test('alerts ignore initial history, deduplicate updates and reset for a new duo',()=>{
 const tracker=new AlertTracker();const s={commandSession:'one',role:'A',local:{run:[1,2],participants:[10,20],pairs:[{id:1,state:2,species:[7,4]}]}};
 assert.deepEqual(tracker.observe(s),[]);
 s.local.pairs[0].state=3;s.proposal={id:12,author:1};
 assert.deepEqual(tracker.observe(s).map(a=>a.kind),['death','proposal']);
 assert.deepEqual(tracker.observe(s),[]);
 s.proposal=null;tracker.observe(s);s.proposal={id:12,author:1};assert.deepEqual(tracker.observe(s),[]);
 s.proposal={id:13,author:0};assert.deepEqual(tracker.observe(s),[]);
 s.local.run=[3,4];assert.deepEqual(tracker.observe(s),[]);
 s.role='B';tracker.observe(s);s.proposal={id:14,author:0};assert.equal(tracker.observe(s)[0].kind,'proposal');
});

test('every canonical encounter area is named and late areas can be searched',()=>{
 for(let id=1;id<=87;id++)assert.doesNotMatch(areaName(id),/^Area /);
 for(const [area,query] of [[39,'Safari'],[40,'Rock Tunnel'],[44,'Power Plant'],[69,'Lost Cave'],[86,'Battle Frontier']])
   assert.equal(findPairs([{id:1,area,state:2,species:[7,4]}],{query}).length,1);
});
