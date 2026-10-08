"""Bounded ROM telemetry, local desktop IPC and automatic duo coordination."""
import asyncio, base64, hashlib, json, logging, math, os, secrets, struct, time, zlib
from pathlib import Path
MAGIC=0x31434C53
SIZE=2996
VIEW,COMMAND=9,10
LOG=logging.getLogger('soullink.desktop')
PAIR=struct.Struct('<IIHHIIHHHHBBBB')
DEATH_SAVE_NOTE='Deaths are tracked, but could not be saved. Keep the launcher open and check that your game folder is writable.'

def valid_death_notice(event):
    return (isinstance(event,dict) and event.get('role') in (0,1)
            and isinstance(event.get('run'),list) and len(event['run'])==2 and any(event['run'])
            and isinstance(event.get('participants'),list) and len(event['participants'])==2
            and all(type(x)==int and 0<x<=0xFFFFFFFF for x in event['participants'])
            and all(type(x)==int and 0<=x<=0xFFFFFFFF for x in event['run'])
            and isinstance(event.get('ids'),list) and 1<=len(event['ids'])<=88
            and all(type(x)==int and 0<x<=65535 for x in event['ids'])
            and len(set(event['ids']))==len(event['ids']))

class DeathJournal:
    """Two bounded local records, scoped to one room and receiving role.

    Terminal deaths have no age expiry. Each direction retains at most one run
    and 88 IDs; a new run replaces that direction's previous record.
    """
    LIMIT=4096
    def __init__(self, root, secret, role):
        self.root=Path(root); self.role=role
        self.scope=hashlib.sha256(('death-journal-v1:'+secret+':'+str(role)).encode()).hexdigest()
        words=struct.unpack('<4I',hashlib.sha256(secret.encode()).digest()[:16])
        self.legacy_run=list(words[:2]); self.legacy_participants=[words[2] or 1,words[3] or 2]
        if self.legacy_participants[0]==self.legacy_participants[1]:
            self.legacy_participants[1]=(self.legacy_participants[0]^0x80000000) or 2
    def path(self, incoming):
        return self.root/('morts-en-attente.json' if incoming else 'morts-signalees.json')
    @staticmethod
    def checksum(event):
        return hashlib.sha256(json.dumps(event,sort_keys=True,separators=(',',':')).encode()).hexdigest()
    def load(self, incoming):
        try:
            with self.path(incoming).open('rb') as stream: data=stream.read(self.LIMIT+1)
            if len(data)>self.LIMIT: return None
            record=json.loads(data)
            if not isinstance(record,dict): return None
            if record.get('version')==1:
                event=record.get('event')
                if (record.get('scope')!=self.scope or not valid_death_notice(event)
                        or record.get('checksum')!=self.checksum(event)): return None
            else:
                # Old kits did not store room scope. Migrate only records whose
                # run and participants prove the current room's derived identity.
                event=dict(record)
                event.setdefault('role',(self.role^1) if incoming else self.role)
                if (not valid_death_notice(event) or event['run']!=self.legacy_run
                        or event['participants']!=self.legacy_participants): return None
            if event['role']!=((self.role^1) if incoming else self.role): return None
            return event
        except (OSError,ValueError,TypeError,UnicodeError): return None
    def save(self, incoming, event):
        if not valid_death_notice(event) or event['role']!=((self.role^1) if incoming else self.role):
            raise ValueError('invalid death journal event')
        record=dict(version=1,scope=self.scope,event=event,checksum=self.checksum(event))
        data=json.dumps(record,sort_keys=True,separators=(',',':')).encode()
        if len(data)>self.LIMIT: raise ValueError('death journal too large')
        path=self.path(incoming); tmp=path.with_suffix('.tmp')
        try:
            with tmp.open('wb') as stream:
                stream.write(data); stream.flush(); os.fsync(stream.fileno())
            os.replace(tmp,path)
            return True
        except OSError:
            LOG.warning('Death recovery record could not be saved; keeping it in memory')
            return False

def decode_view(data):
    if len(data)!=SIZE or struct.unpack_from('<I',data)[0]!=MAGIC or struct.unpack_from('<I',data,44)[0]!=MAGIC:
        raise ValueError('invalid companion snapshot')
    save,safe,battle,enabled,status,player,grave,count=struct.unpack_from('<8B',data,52)
    if count>88 or safe>1 or battle>1 or enabled>1 or status>10 or player not in (0,1,255): raise ValueError('invalid view header')
    run_a,run_b,id_a,id_b=struct.unpack_from('<4I',data,60)
    team=list(struct.unpack_from('<6H',data,76)); team_count=data[88]
    if team_count>6 or data[89]>1: raise ValueError('invalid team')
    pairs=[]
    for i in range(count):
        a=PAIR.unpack_from(data,180+i*32)
        if a[2]>512 or a[6]>512 or a[10]>3 or a[11]>3 or a[12]>4: raise ValueError('invalid pair')
        pairs.append(dict(id=a[8],area=a[9],species=[a[2],a[6]],ordinal=[a[3],a[7]],encounters=[a[10],a[11]],state=a[12]))
    party=[]
    for i in range(6):
        species,pair,hp,maxhp,level=struct.unpack_from('<4HB',data,108+i*12)
        if species>512 or level>100: raise ValueError(f'invalid party slot {i}: species={species}, level={level}')
        if species: party.append(dict(species=species,pair=pair,hp=hp,maxHp=maxhp,level=level))
    return dict(save=save,safe=bool(safe),battle=bool(battle),enabled=bool(enabled),status=status,player=player,
                grave=grave,run=[run_a,run_b],participants=[id_a,id_b],team=team[:team_count],locked=bool(data[89]),
                config=list(data[92:100]),configRevision=struct.unpack_from('<I',data,100)[0],digest=struct.unpack_from('<I',data,104)[0],
                party=party,pairs=pairs,command=struct.unpack_from('<I',data,8)[0],result=data[13])

class ViewAssembler:
    def __init__(self): self.key=None; self.buffer=bytearray(); self.started=0
    def add(self,payload):
        if not 8<len(payload)<=56: raise ValueError('invalid view chunk')
        key,offset,total=struct.unpack_from('<IHH',payload)
        if total!=SIZE or offset+len(payload)-8>total: raise ValueError('invalid view bounds')
        if offset==0: self.key=key; self.buffer=bytearray(); self.started=time.monotonic()
        if self.key!=key or offset!=len(self.buffer) or time.monotonic()-self.started>5: raise ValueError('out of order view')
        self.buffer.extend(payload[8:])
        if len(self.buffer)==total:
            data=bytes(self.buffer); self.key=None; self.buffer=bytearray(); return data
        return None

def command_packet(role,kind,run=(0,0),participants=(0,0),ids=()):
    if role not in (0,1) or kind not in (1,2,3,4,5) or len(ids)>6 or any(type(x)!=int or not 0<x<=65535 for x in ids): raise ValueError('invalid command')
    identifier=secrets.randbelow(0xFFFFFFFE)+1
    return identifier,struct.pack('<I4B4I6H',identifier,kind,role,len(ids),0,*run,*participants,*(list(ids)+[0]*(6-len(ids))))

def proposal_valid(proposal,views):
    if not isinstance(proposal,dict): return False
    token=proposal.get('id'); ids=proposal.get('ids'); digest=proposal.get('digest'); run=proposal.get('run')
    if not isinstance(token,str) or len(token)!=32 or any(x not in '0123456789abcdef' for x in token): return False
    if not isinstance(ids,list) or not 1<=len(ids)<=6 or any(type(x)!=int or not 0<x<=65535 for x in ids) or ids!=sorted(set(ids)): return False
    if type(digest)!=int or not isinstance(run,list) or len(run)!=2 or any(type(x)!=int for x in run) or not any(run): return False
    if len(views)!=2: return False
    for view in views:
        if not view or view['run']!=run or view['digest']!=digest or not view['enabled']: return False
        rows={p['id']:p for p in view['pairs']}
        if any(x not in rows or rows[x]['state']!=2 for x in ids): return False
    return True

def team_applied(view, ids):
    # Selection is written before the native exchange. Check the actual party
    # as well, so command acceptance cannot stand in for a completed commit.
    return bool(view and view['save']==1 and view['status'] not in (1,2)
                and view['locked'] and view['team']==ids
                and sorted(p['pair'] for p in view['party'])==ids)

class TeamJournal:
    LIMIT=4096
    def __init__(self, root, secret, role, build):
        self.path=Path(root)/'team-recovery.json'; self.build=build
        self.scope=hashlib.sha256(('team-journal-v1:'+secret+':'+str(role)).encode()).hexdigest()
    def valid(self, record):
        if not isinstance(record,dict) or record.get('scope')!=self.scope or record.get('build')!=self.build: return False
        p=record.get('proposal'); participants=record.get('participants'); config=record.get('config')
        if (not isinstance(p,dict) or not isinstance(participants,list) or len(participants)!=2
                or any(type(x)!=int or not 0<x<=0xFFFFFFFF for x in participants)
                or not isinstance(config,list) or len(config)!=8 or any(type(x)!=int or not 0<=x<=255 for x in config)
                or record.get('state') not in ('pending','confirmed','cancelled')): return False
        # Use the same proposal parser without trusting any restored game view.
        rows=[dict(id=x,state=2) for x in p.get('ids',[]) if type(x)==int] if isinstance(p.get('ids'),list) else []
        view=dict(run=p.get('run'),digest=p.get('digest'),enabled=True,pairs=rows)
        return proposal_valid(p,[view,view]) and all(type(x)==int and 0<=x<=0xFFFFFFFF for x in p['run'])
    def load(self):
        try:
            with self.path.open('rb') as stream: data=stream.read(self.LIMIT+1)
            if len(data)>self.LIMIT: return None
            envelope=json.loads(data); record=envelope.get('record') if isinstance(envelope,dict) else None
            if not self.valid(record) or envelope.get('checksum')!=DeathJournal.checksum(record): return None
            return record
        except (OSError,ValueError,TypeError,UnicodeError): return None
    def save(self, record):
        if not self.valid(record): raise ValueError('invalid team recovery record')
        data=json.dumps(dict(record=record,checksum=DeathJournal.checksum(record)),separators=(',',':')).encode()
        if len(data)>self.LIMIT: raise ValueError('team recovery record too large')
        try:
            with self.path.with_suffix('.tmp').open('wb') as stream:
                stream.write(data); stream.flush(); os.fsync(stream.fileno())
            os.replace(self.path.with_suffix('.tmp'),self.path)
            return True
        except OSError:
            LOG.warning('Team approval could not be saved; keeping the change pending')
            return False

class InternetMetrics:
    """Session-only timings; fixed keys and bounded samples contain no game secrets."""
    def __init__(self):
        self.samples={}; self.sampled_at={}; self.started={}; self.reconnects=0; self.sent_bytes=0; self.received_bytes=0
    def mark(self, stage):
        self.started[stage]=time.monotonic()
    def finish(self, stage):
        start=self.started.pop(stage,None)
        if start is not None: self.add(stage,(time.monotonic()-start)*1000)
    def add(self, stage, milliseconds):
        self.sampled_at[stage]=time.monotonic()
        values=self.samples.setdefault(stage,[])
        values.append(round(max(0,milliseconds),1)); del values[:-128]
    def invalidate(self,stage):
        self.sampled_at.pop(stage,None)
    def report(self):
        timings={}
        for stage,values in self.samples.items():
            ordered=sorted(values); n=len(ordered)
            timings[stage]=dict(ageMs=round(max(0,time.monotonic()-self.sampled_at[stage])*1000) if stage in self.sampled_at else None,samples=n,lastMs=values[-1],medianMs=ordered[n//2],p95Ms=ordered[max(0,(95*n+99)//100-1)])
        return dict(timings=timings,reconnects=self.reconnects,applicationBytesSent=self.sent_bytes,applicationBytesReceived=self.received_bytes,
                    pendingMs={key:round((time.monotonic()-value)*1000) for key,value in self.started.items()})

class ActivityLog:
    """Local observations, bounded and scoped to this room. Not a game journal."""
    def __init__(self, root, secret, role):
        self.path=Path(root)/'duo-history.json'
        self.scope=hashlib.sha256((secret+str(role)).encode()).hexdigest()
        self.session_started=time.time(); self.session_counts={kind:0 for kind in ('capture','death','failed','evolution','team-applied')}
        self.run=None; self.seen={}; self.ready=[]; self.events=[]; self.keys=[]; self.dirty=False; self.retry=0
        try:
            if self.path.stat().st_size>65536: return
            data=json.loads(self.path.read_text(encoding='utf-8'))
            if not isinstance(data,dict) or data.get('scope')!=self.scope or data.get('version')!=1: return
            if not isinstance(data['seen'],dict) or len(data['seen'])>176 or not isinstance(data['events'],list) or len(data['events'])>128: return
            if not isinstance(data['ready'],list) or len(data['ready'])>88 or not isinstance(data['keys'],list) or len(data['keys'])>128: return
            if not all(isinstance(v,list) and len(v)==2 and all(type(n)==int for n in v) for v in data['seen'].values()): return
            if not all(isinstance(e,dict) and isinstance(e.get('kind'),str) and isinstance(e.get('time'),(float,int)) for e in data['events']): return
            self.run=data['run']; self.seen=data['seen']; self.ready=data['ready']; self.events=data['events']; self.keys=data['keys']
        except (OSError,ValueError,KeyError,TypeError): pass
    def add(self, kind, key, **details):
        key=kind+':'+str(key)
        if key in self.keys: return
        self.keys=(self.keys+[key])[-128:]
        self.events=(self.events+[dict(kind=kind,time=time.time(),**details)])[-128:]
        if kind in self.session_counts: self.session_counts[kind]+=1
        self.dirty=True
    def observe(self, view):
        if not view or not any(view['run']): return
        run=view['run']+view['participants']
        if run!=self.run:
            if self.run is not None:
                self.session_started=time.time(); self.session_counts={kind:0 for kind in self.session_counts}
            self.run=run; self.seen={}; self.ready=[]; self.events=[]; self.keys=[]; self.dirty=True
        for row in view['pairs']:
            for side in (0,1):
                ordinal=row['ordinal'][side]; encounter=row['encounters'][side]; species=row['species'][side]
                if not ordinal or not encounter: continue
                key=f'{side}:{ordinal}'; old=self.seen.get(key)
                if old and old[0] in (2,3): continue # Terminal history cannot be undone by an older reading.
                # A partner's copy can lag behind while its game is busy. Once
                # recorded, species changes come from that Pokemon's owner;
                # terminal encounter states still propagate from either copy.
                if old and view.get('player') in (0,1) and side!=view['player']:
                    species=old[1]
                if not old or old[0]!=encounter:
                    kind={1:'capture',2:'failed',3:'death'}.get(encounter)
                    if kind: self.add(kind,key,role=side,species=species,area=row['area'],pair=row['id'])
                elif old[1]!=species:
                    self.add('evolution',key+':'+str(species),role=side,species=species,previous=old[1],area=row['area'],pair=row['id'])
                if old!=[encounter,species]: self.seen[key]=[encounter,species]; self.dirty=True
            if row['id'] and row['state']==2 and row['id'] not in self.ready:
                self.ready.append(row['id']); self.dirty=True
                self.add('pair-ready',row['id'],pair=row['id'],species=row['species'],area=row['area'])
    def save(self):
        if not self.dirty or time.monotonic()<self.retry: return
        self.retry=time.monotonic()+5
        try:
            text=json.dumps(dict(version=1,scope=self.scope,run=self.run,seen=self.seen,ready=self.ready,events=self.events,keys=self.keys),separators=(',',':'))
            if len(text.encode())>65536: return
            tmp=self.path.with_suffix('.json.tmp')
            tmp.write_text(text,encoding='utf-8'); os.replace(tmp,self.path); self.dirty=False
        except OSError: pass # Display history is optional; gameplay never waits for it.

class DesktopBridge:
    def __init__(self,companion,root):
        self.metrics=InternetMetrics()
        self.activity=ActivityLog(root,companion.secret,companion.role)
        self.net=companion; self.root=Path(root); self.root.mkdir(parents=True,exist_ok=True)
        self.command_session=secrets.token_hex(16)
        self.commands=self.root/'compagnon-commandes'; self.commands.mkdir(exist_ok=True)
        self.peer_refresh_required=False
        self.local=None; self.peer=None; self.peer_at=0; self.peer_active=False; self.peer_connected=False
        self.local_at=0; self.last_local_beat=0; self.last_wan_view=None; self.last_view_sent=0
        self.proposal=None; self.pending=None; self.last_attempt=0; self.synced=None; self.requested=False
        self.note=None; self.last_success=None; self.last_status=None; self.failed_signature=None
        self.local_signature=None; self.peer_signature=None
        self.awaiting_completion=False
        self.completion_until=0
        self.init_until=0
        self.peer_data=None; self.async_attempt=None; self.async_until=0; self.death_until=0
        self.wakeup=asyncio.Event()
        self.build=struct.pack('>I',zlib.crc32((self.root/'lgfr-sl.gba').read_bytes())).hex()
        self.version=0
        self.accepted_team=None
        self.death_backlog=None
        self.local_deaths=None; self.last_death_sent=0
        self.journal=DeathJournal(self.root,self.net.secret,self.net.role)
        self.local_deaths=self.journal.load(False)
        self.death_backlog=self.journal.load(True)
        self.journal_failed={False:False,True:False}
        self.journal_tried={False:0,True:0}
        self.team_journal=TeamJournal(self.root,self.net.secret,self.net.role,self.build)
        self.team_record=self.team_journal.load()
        self.team_recovery=bool(self.team_record and self.team_record['state']!='cancelled')
        if self.team_recovery: self.accepted_team=self.team_record['proposal']
        self.team_retry_at=0; self.team_recover_at=time.monotonic()+(0 if self.net.role==0 else 3)
        self.team_saved=True
    def team_context(self):
        if not self.team_record or not self.compatible(): return False
        for view in (self.local,self.peer):
            config=list(view['config']);config[6]=1
            if view['participants']!=self.team_record['participants'] or config!=self.team_record['config']: return False
        return proposal_valid(self.team_record['proposal'],[self.local,self.peer])
    def finish_team(self, state):
        self.team_record=dict(self.team_record,state=state)
        self.team_saved=self.team_journal.save(self.team_record)
        self.accepted_team=None; self.team_recovery=False
    def remember_deaths(self, incoming, event):
        old=self.death_backlog if incoming else self.local_deaths
        if old and old['run']==event['run'] and old['participants']==event['participants']:
            event=dict(event,ids=sorted(set(event['ids']+old['ids'])))
        if not valid_death_notice(event):
            LOG.warning('Death recovery record exceeds registry bounds; ignoring update')
            return
        if incoming: self.death_backlog=event
        else:
            self.local_deaths=event
            if event!=old: self.last_death_sent=0
        if event!=old or (self.journal_failed[incoming] and time.monotonic()-self.journal_tried[incoming]>=5):
            self.journal_tried[incoming]=time.monotonic()
            self.journal_failed[incoming]=not self.journal.save(incoming,event)
            if self.journal_failed[incoming]:
                self.note=DEATH_SAVE_NOTE
            elif not any(self.journal_failed.values()) and self.note==DEATH_SAVE_NOTE:
                self.note=None
    def active(self): return self.net.local is not None and time.monotonic()-self.last_local_beat<5
    def pair_signature(self):
        if not self.local or not self.peer: return None
        return (self.local['digest'],self.peer['digest'],self.local['configRevision'],self.peer['configRevision'],tuple(self.local['team']),tuple(self.peer['team']))
    async def snapshot(self,data):
        view=decode_view(data); previous=self.local; self.local=view; self.local_at=time.monotonic(); self.last_local_beat=self.local_at
        self.activity.observe(view)
        if previous and previous['digest']!=view['digest'] and self.last_status not in (1,2) and not self.awaiting_completion:
            if 'changeToConfirmed' not in self.metrics.started:
                self.metrics.mark('changeToConfirmed'); self.metrics.mark('queueToCommand')
        if self.pending and view['command']==self.pending['id']:
            self.metrics.finish('commandToAcknowledgement')
            LOG.info('Game command %s finished: result %s',self.pending['kind'],view['result'])
            if self.pending['kind']==4 and view['result']==1: self.death_until=time.monotonic()+1
            if self.pending['kind']==5:
                self.async_until=time.monotonic()+1
                if view['result']==3: self.async_attempt=None
            if self.pending['kind']==2 and view['result']==1:
                self.init_until=time.monotonic()+1
            if self.pending['kind']==3:
                self.team_retry_at=time.monotonic()+10
            if self.pending['kind']==3 and view['result']==2:
                self.note='The game could not change this team. Check PC space and held mail. The approved change is still pending.'
            if view['result']!=1 and self.pending['kind']!=5 and not (self.pending['kind']==3 and view['result']==2): self.note='The game is busy or rejected the request. Checks can resume when both games are ready.'
            self.pending=None
        if view['status'] in (3,10) and (self.last_status in (1,2) or self.awaiting_completion):
            if view['status']==3:
                self.synced=self.pair_signature() if self.peer and self.peer['digest']==view['digest'] else None; self.failed_signature=None; self.last_success=time.time(); self.note=None
                if self.synced:
                    self.awaiting_completion=False; self.metrics.finish('changeToConfirmed')
            else:
                self.failed_signature=self.pair_signature(); self.awaiting_completion=False; self.note='Captures and deaths are up to date, but the team could not be changed. Check the rules, PC space and held mail.'
        elif view['status'] in (4,5,6,7,8,9) and self.last_status in (1,2):
            self.note='Sync interrupted. The companion will retry automatically.'
        if previous:
            old={p['id']:p['state'] for p in previous['pairs'] if p['id']}
            if any(p['state'] in (3,4) and old.get(p['id']) in (1,2) for p in view['pairs']):
                self.note='A pair has died. Check Choose your team to see which one.'
            elif any(p['state']==2 and old.get(p['id'])==1 for p in view['pairs']):
                self.note='A new pair is ready! You can select it in Choose your team.'
        self.last_status=view['status']
        ids=[p['id'] for p in view['pairs'] if p['id'] and view['player']==self.net.role and p['encounters'][self.net.role]==3]
        if ids and any(view['run']):
            event=dict(role=self.net.role,run=view['run'],participants=view['participants'],ids=ids)
            if valid_death_notice(event): self.remember_deaths(False,event)
        # ACK fields and changing generation are local only. A stable game view
        # is not retransmitted every second; the ten-second check is lightweight.
        public={k:v for k,v in view.items() if k not in ('command','result')}
        serialized=json.dumps(public,sort_keys=True,separators=(',',':'))
        if self.net.joined and (serialized!=self.last_wan_view or time.monotonic()-self.last_view_sent>=10):
            await self.net.wan_send('view',data=base64.b64encode(data).decode('ascii'),build=self.build)
            self.last_wan_view=serialized; self.last_view_sent=time.monotonic()
        self.wakeup.set()
    def relay_disconnected(self):
        self.metrics.invalidate('relayRoundTrip')
        self.peer_refresh_required=True
        self.peer_connected=False; self.peer_active=False; self.peer_data=None
        self.proposal=None; self.synced=None; self.async_attempt=None
        self.wakeup.set()

    async def message(self,message):
        self.wakeup.set()
        kind=message['type']
        if kind=='pong':
            self.peer_connected=bool(message.get('peerConnected',False)); self.peer_active=bool(message.get('peerActive',False)) and not self.peer_refresh_required
            if not self.peer_connected:
                self.peer_data=None; self.proposal=None; self.synced=None
                if self.peer is not None: self.peer_refresh_required=True
        elif kind=='view':
            data=base64.b64decode(message['data'],validate=True)
            self.peer=decode_view(data); self.peer_refresh_required=False; self.peer_data=data; self.peer_at=time.monotonic()
            self.peer_connected=True; self.peer_active=True; self.peer_build=message['build']
            if self.compatible():
                self.activity.observe(self.peer)
                ids=[p['id'] for p in self.peer['pairs'] if p['id'] and p['encounters'][self.net.role^1]==3]
                if ids:
                    self.remember_deaths(True,dict(role=self.net.role^1,run=self.peer['run'],participants=self.peer['participants'],ids=ids))
        elif kind=='sync_request': self.requested=True; self.failed_signature=None
        elif kind=='death_notice':
            event=message.get('event')
            if not valid_death_notice(event) or event['role']!=(self.net.role^1) or message.get('build')!=self.build: return
            if not self.local or event['run']!=self.local['run'] or event['participants']!=self.local['participants']: return
            self.remember_deaths(True,event)
        elif kind=='proposal':
            self.proposal=message['proposal']; self.note=None
            self.activity.add('team-proposed',self.proposal['id'],ids=self.proposal['ids'],role=self.proposal.get('author'))
            if (self.team_record and self.team_record['state']!='cancelled'
                    and self.proposal.get('author')!=self.net.role
                    and all(self.proposal.get(k)==self.team_record['proposal'].get(k) for k in ('id','ids','run','digest'))
                    and self.team_context()):
                await self.net.wan_send('vote',id=self.proposal['id'],accept=True)
        elif kind=='proposal_closed':
            if self.proposal: self.activity.add('team-closed',self.proposal['id'])
            self.proposal=None; self.note=message.get('reason')
        elif kind=='apply_team':
            proposal=message['proposal']
            if not proposal_valid(proposal,[self.local,self.peer]):
                self.note='The Pokémon have changed since this proposal. Propose a new team.'; return
            self.proposal=None; self.requested=False
            self.accepted_team=proposal
            self.activity.add('team-approved',proposal['id'],ids=proposal['ids'])
            config=list(self.local['config']);config[6]=1
            self.team_record=dict(scope=self.team_journal.scope,build=self.build,proposal=proposal,
                                  participants=self.local['participants'],config=config,state='pending')
            self.team_saved=self.team_journal.save(self.team_record)
            self.team_recovery=False; self.team_retry_at=0
            LOG.info('Team accepted: pairs %s; waiting to apply',proposal['ids'])
            await self.coordinate()
            self.note='Your partner accepted. The team will change when both games are ready.'
        elif kind=='notice': self.note=message.get('reason')
    async def command(self,kind,run=(0,0),participants=(0,0),ids=()):
        if self.pending: return
        identifier,payload=command_packet(self.net.role,kind,run,participants,ids)
        self.pending=dict(id=identifier,kind=kind,time=time.monotonic())
        self.metrics.mark('commandToAcknowledgement')
        if kind in (1,3): self.metrics.finish('queueToCommand')
        LOG.info('Game command %s sent: pairs %s',kind,list(ids))
        await self.net.local_send(COMMAND,payload)
        if kind in (1,3): self.last_attempt=time.monotonic()
    async def apply_peer(self):
        identifier,payload=command_packet(self.net.role,5)
        self.pending=dict(id=identifier,kind=5,time=time.monotonic())
        self.metrics.mark('commandToAcknowledgement'); self.metrics.finish('queueToCommand')
        self.async_attempt=(self.local['digest'],self.peer['digest'])
        data=self.peer_data[44:]
        try:
            for offset in range(0,len(data),48):
                await self.net.local_send(11,struct.pack('<IHH',identifier,offset,len(data))+data[offset:offset+48])
            await self.net.local_send(COMMAND,payload)
        except (ConnectionError,OSError,asyncio.TimeoutError):
            self.pending=None; self.async_attempt=None; self.async_until=time.monotonic()+1
            LOG.info('Local reading upload interrupted; a fresh reading can retry')

    def compatible(self):
        return self.local and self.peer and getattr(self,'peer_build',None)==self.build and self.local['run']==self.peer['run'] and self.local['participants']==self.peer['participants'] and any(self.local['run']) and self.local['player']==self.net.role and self.peer['player']==(self.net.role^1)
    async def coordinate(self):
        for incoming,event in ((False,self.local_deaths),(True,self.death_backlog)):
            if event and self.journal_failed[incoming] and time.monotonic()-self.journal_tried[incoming]>=5:
                self.remember_deaths(incoming,event)
        if self.net.joined and self.local_deaths and time.monotonic()-self.last_death_sent>=5:
            await self.net.wan_send('death_notice',event=self.local_deaths,build=self.build)
            self.last_death_sent=time.monotonic()
        if self.local:
            for stage,busy in (('localBusy',not self.local['safe']),('partnerBusy',bool(self.peer and not self.peer['safe']))):
                if busy and stage not in self.metrics.started: self.metrics.mark(stage)
                elif not busy: self.metrics.finish(stage)
        if not self.active() or not self.local: return
        if self.pending:
            if time.monotonic()-self.pending['time']>8: self.pending=None; self.async_attempt=None
            else: return
        if not self.local['safe'] or self.net.epoch or self.local['status'] in (1,2): return
        # FINISH can arrive before the final local/remote views. Allow those
        # views to catch up instead of immediately starting the same sync again.
        # A remembered death remains applicable if its sender is in a defeat
        # dialogue, disconnected or has already returned to the title screen.
        dead=self.death_backlog
        if dead and time.monotonic()>=self.death_until and self.local['enabled'] and self.local['run']==dead['run'] and self.local['participants']==dead['participants']:
            ids=[p['id'] for p in self.local['pairs'] if p['id'] in dead['ids'] and p['encounters'][self.net.role^1]==1]
            if ids:
                await self.command(4,run=dead['run'],participants=dead['participants'],ids=ids[:6]); return
        if self.peer_refresh_required: return
        if not any(self.local['run']):
            if time.monotonic()<self.init_until: return
            if self.local['save']!=1 or not self.net.joined: return
            if self.peer and getattr(self,'peer_build',None)!=self.build: return
            if self.peer and any(self.peer['run']): run=self.peer['run']; participants=self.peer['participants']
            else:
                words=struct.unpack('<4I',hashlib.sha256(self.net.secret.encode()).digest()[:16]); run=list(words[:2]); participants=[words[2] or 1,words[3] or 2]
                if participants[0]==participants[1]: participants[1]=(participants[0]^0x80000000) or 2
            await self.command(2,run=run,participants=participants); return
        if not self.net.joined or not self.peer_active or not self.peer or getattr(self,'peer_build',None)!=self.build: return
        if (not self.accepted_team and self.team_record and self.team_record['state']=='confirmed'
                and self.team_context() and not (team_applied(self.local,self.team_record['proposal']['ids'])
                                                and team_applied(self.peer,self.team_record['proposal']['ids']))):
            self.accepted_team=self.team_record['proposal'];self.team_recovery=True
        if self.accepted_team:
            if not self.team_context():
                self.finish_team('cancelled')
                self.note='The approved team no longer matches the pairs or rules. Check both parties and propose a new team.'
                return
            if team_applied(self.local,self.accepted_team['ids']) and team_applied(self.peer,self.accepted_team['ids']):
                self.activity.add('team-applied',self.accepted_team['id'],ids=self.accepted_team['ids'])
                self.finish_team('confirmed'); self.last_success=time.time(); self.note=None
            elif self.team_recovery:
                if not self.proposal and time.monotonic()>=self.team_recover_at:
                    await self.net.wan_send('propose',proposal=self.accepted_team)
                    self.team_recover_at=time.monotonic()+15
                return
        if (self.accepted_team or self.peer_data is None) and not self.peer['safe'] and self.peer['status']!=1: return
        if self.awaiting_completion and time.monotonic()<self.completion_until: return
        if self.accepted_team:
            if not self.team_saved:
                if time.monotonic()>=self.team_retry_at:
                    self.team_saved=self.team_journal.save(self.team_record)
                    self.team_retry_at=0 if self.team_saved else time.monotonic()+5
                self.note=None if self.team_saved else 'The approval could not be saved. Check that your game folder is writable; the team change is waiting.'
                return
            if time.monotonic()>=self.team_retry_at:
                await self.command(3,ids=self.accepted_team['ids'])
            return
        if not self.compatible() or not self.local['enabled'] or not self.peer['enabled'] or self.local['config']!=self.peer['config']: return
        signature=self.pair_signature()
        if self.peer_data is not None:
            if self.local['digest']==self.peer['digest']:
                if self.synced!=signature: self.last_success=time.time()
                self.synced=signature; self.requested=False
                self.metrics.finish('changeToConfirmed')
                return
            if self.peer['status'] in (1,2) or time.monotonic()<self.async_until: return
            key=(self.local['digest'],self.peer['digest'])
            if key!=self.async_attempt or self.requested:
                self.requested=False; await self.apply_peer()
            return
        # A short retry guard prevents request loops without making a new
        # capture inherit a ten-second wait from the previous exchange.
        if (time.monotonic()-self.last_attempt<1 and not self.requested
                and self.peer['status']!=1): return
        if self.requested or (signature!=self.synced and signature!=self.failed_signature):
            self.requested=False; await self.command(1)
    async def actions(self):
        for file in sorted(self.commands.glob('*.json'))[:32]:
            if file.is_symlink(): continue
            claimed=None
            try:
                marker=file.with_suffix('.'+self.command_session+'.processing')
                if marker.exists(): raise ValueError('This request was already consumed. Try again in the companion.')
                file.rename(marker);claimed=marker
                if claimed.stat().st_size>4096: raise ValueError('oversized UI command')
                request=json.loads(claimed.read_text(encoding='utf-8-sig'))
                if not isinstance(request,dict): raise ValueError('Invalid UI request.')
                issued=request.get('issued')
                if (request.get('session')!=self.command_session or type(issued) not in (int,float)
                        or not math.isfinite(issued) or not -5<=time.time()-issued<=15):
                    raise ValueError('This request has expired. Try again in the companion.')
                scope=[self.local.get('run'),self.local.get('participants')] if self.local else None
                if request.get('duo')!=scope:
                    raise ValueError('The duo has changed. Check your selection and try again.')
                action=request.get('action')
                if action=='sync':
                    self.requested=True; self.failed_signature=None
                    if self.net.joined: await self.net.wan_send('sync_request')
                    self.async_attempt=None
                    self.note='Refresh requested. Each game applies updates when it is free.'
                elif action=='propose':
                    ids=request.get('ids'); proposal=dict(id=secrets.token_hex(16),ids=ids,digest=self.local['digest'] if self.local else 0,run=self.local['run'] if self.local else [])
                    if not self.compatible() or not proposal_valid(proposal,[self.local,self.peer]): raise ValueError('Pick one to six living pairs after both games have synced.')
                    await self.net.wan_send('propose',proposal=proposal)
                elif action in ('accept','reject'):
                    if not self.proposal or request.get('id')!=self.proposal['id']: raise ValueError('The proposal has expired.')
                    await self.net.wan_send('vote',id=request['id'],accept=action=='accept')
                else: raise ValueError('Unknown request.')
            except (ValueError,OSError,KeyError,TypeError,ConnectionError) as error: self.note=str(error)
            finally:
                if claimed is not None:
                    try: claimed.unlink()
                    except OSError: LOG.warning('Consumed UI request could not be removed; it will not be replayed')
    def status(self):
        if not self.net.joined: return 'offline','Reconnecting to the relay','Keep the game open. The companion is trying to reconnect.'
        if self.local and self.local['save'] in (2,3): return 'error','Unsupported Soul Link save','The companion cannot use this save. Keep the .sav file and check that you have the right ROM.'
        if not self.active(): return 'waiting','Waiting for your game','Load your save in mGBA, or resume the game if it is paused.'
        if not self.peer_connected or not self.peer: return 'waiting','Waiting for your partner','You are connected. Your partner needs to run play.bat and join with the same code.'
        if self.peer_refresh_required: return 'waiting','Waiting for a fresh partner reading','The connection is back. Your partner’s last team stays visible until a new reading arrives.'
        if not self.peer_active: return 'waiting','Your partner’s game is paused','Sync will resume when your partner resumes their game.'
        if getattr(self,'peer_build',None)!=self.build: return 'error','Different game versions','Both players need the same Soul Link ZIP. Keep your .sav files.'
        if not any(self.local['run']) or not any(self.peer['run']): return 'waiting','Setting up your duo','Both players should leave battles, menus and dialogue. The companion will set up the duo.'
        if not self.compatible(): return 'error','These saves belong to different duos','Check your room code and A/B role. An already linked save keeps its duo.'
        if self.local['config']!=self.peer['config']: return 'error','The games have different rules','Choose the same rules in the Soul Link menu in both games.'
        if not self.local['enabled'] or not self.peer['enabled']: return 'error','Soul Link is disabled in one game','Enable Soul Link under Rules in the Soul Link menu of that game.'
        if self.accepted_team:
            applied=[team_applied(v,self.accepted_team['ids']) for v in (self.local,self.peer)]
            if any(applied) and not all(applied):
                return 'syncing','Team change partly applied','One game has changed its party. Keep both launchers open and leave battles or menus so the change can finish.'
            if self.team_recovery:
                return 'syncing','Resuming the approved team','The companion is checking the saved agreement. Your partner may need to accept it again if their copy is missing.'
            return 'syncing','Team accepted · waiting for the games','Leave battles, menus and dialogue so the games can change your teams.'
        if self.net.epoch or self.local['status'] in (1,2): return 'syncing','Updating both games','Sync in progress. Movement may briefly pause.'
        if self.local['battle']: return 'waiting','Updates waiting for your battle to finish','Your partner can keep playing. Your game applies updates after the battle.'
        if self.peer['battle']: return 'waiting','Your partner is in battle','Keep playing. Readings still travel in the background; their game applies updates when it is free.'
        if not self.local['safe']: return 'waiting','Your game is busy','Updates are queued until you leave this menu or dialogue.'
        if not self.peer['safe']: return 'waiting','Your partner’s game is busy','Keep playing. Their updates are queued until their game is free.'
        if self.failed_signature==self.pair_signature(): return 'error','Check your team','Captures and deaths are up to date. Check rules, held mail and PC space, or propose another team.'
        if self.local['team']!=self.peer['team'] or self.local['locked']!=self.peer['locked']:
            return 'waiting','The teams do not match yet','Check Choose your team. An interrupted change may need your partner’s approval again.'
        if self.synced is None or self.pair_signature()!=self.synced or self.requested: return 'syncing','Sync queued','Updates travel in the background. Each game applies them when it is free.'
        return 'ready','Your duo is up to date','All set. Play as usual; the companion tracks captures and deaths.'
    def write_state(self):
        state,title,detail=self.status(); self.version+=1
        self.activity.save()
        document=dict(version=1,updated=time.time(),commandSession=self.command_session,role='AB'[self.net.role],state=state,title=title,detail=detail,
                      metrics=self.metrics.report(),local=self.local,peer=self.peer,relayConnected=bool(self.net.joined),localActive=self.active(),peerActive=bool(self.net.joined and self.peer_connected and self.peer_active),peerConnected=bool(self.net.joined and self.peer_connected),proposal=self.proposal,note=self.note,lastSuccess=self.last_success,
                      sessionSummary=dict(started=self.activity.session_started,counts=dict(self.activity.session_counts)),history=list(reversed(self.activity.events)),historySaved=not self.activity.dirty,acceptedTeam=self.accepted_team,
                      canPropose=bool(not self.accepted_team and self.compatible() and self.net.joined and self.active() and self.peer_active and not self.net.epoch and self.local['digest']==self.peer['digest'] and self.local['config']==self.peer['config'] and self.local['enabled'] and self.peer['enabled']))
        tmp=self.root/'compagnon-etat.json.tmp'; dest=self.root/'compagnon-etat.json'
        try:
            tmp.write_text(json.dumps(document,ensure_ascii=False,separators=(',',':')),encoding='utf-8'); os.replace(tmp,dest)
        except OSError: pass
    async def run(self):
        last_write=0
        while True:
            self.wakeup.clear()
            try:
                await self.actions(); await self.coordinate()
            except (OSError,asyncio.TimeoutError) as error:
                # Transport failure can reach this task before the connection
                # worker sees it. Keep coordination alive so its durable deaths
                # and team approvals can retry after that worker reconnects.
                LOG.info('Companion transport interrupted; waiting to retry: %s',error)
            if time.monotonic()-last_write>=.5:
                self.write_state(); last_write=time.monotonic()
            try: await asyncio.wait_for(self.wakeup.wait(),.5)
            except asyncio.TimeoutError: pass
