"""Isolated live fixture for browser QA; never touches a player's game."""
import asyncio
import json
from pathlib import Path
import shutil
import sys
import time
sys.path.insert(0,str(Path(__file__).resolve().parents[2]))
from remote.companion_web import CompanionWeb

async def main():
    source=Path(__file__).resolve().parents[2]/'remote'
    root=source.parent/'build/tests/web-preview'
    root.mkdir(parents=True,exist_ok=True)
    for name in ('companion-web','companion-assets'):
        shutil.copytree(source/name,root/name,dirs_exist_ok=True)
    pairs=[dict(id=1,area=87,state=2,species=[7,4]),dict(id=2,area=8,state=2,species=[16,19]),dict(id=3,area=9,state=2,species=[25,133]),dict(id=4,area=33,state=1,species=[10,0]),dict(id=5,area=34,state=3,species=[74,41])]
    def view(player):
        party=[dict(species=p['species'][player],pair=p['id'],hp=19 if p['id']==1 else 32,maxHp=20 if p['id']==1 else 38,level=5 if p['id']==1 else 12) for p in pairs[:3]]
        return dict(player=player,run=[1,2],participants=[10,20],safe=True,locked=True,team=[1,2,3],party=party,pairs=pairs)
    state=dict(commandSession='b'*32,role='A',state='ready',title='Your duo is up to date',detail='All set. Play as usual; the companion tracks captures and deaths.',local=view(0),peer=view(1),peerActive=True,canPropose=True,proposal=None,note=None,lastSuccess=time.time())
    web=await CompanionWeb(root,0).start()
    (root/'invitation-soul-link.json').write_text(json.dumps(dict(version=2,server='gbfbc.com',port=8443,role='B',secret='f'*48)))
    print(web.url,flush=True)
    try:
        while True:
            if (root/'scenario.json').is_file():
                state.update(json.loads((root/'scenario.json').read_text()))
            state['updated']=state.get('previewUpdated') or time.time()
            (root/'compagnon-etat.json').write_text(json.dumps(state),encoding='utf-8')
            await asyncio.sleep(.5)
    finally: await web.close()

asyncio.run(main())
