"""Export first-frame pixel sprites from this ROM and English species names."""
from pathlib import Path
from PIL import Image
import csv,json,re
ROOT=Path(__file__).resolve().parents[1]
TARGET=ROOT/'remote/companion-assets'
TARGET.mkdir(exist_ok=True)
constants={name:int(number) for name,number in re.findall(r'#define (SPECIES_\w+) (\d+)',(ROOT/'include/constants/species.h').read_text())}
english=dict(re.findall(r'\[(SPECIES_\w+)\] = _\("([^"\n]+)"\)',(ROOT/'src/data/text/species_names.h').read_text()))
icons=dict(re.findall(r'\[(SPECIES_\w+)\]\s*= (gMonIcon_\w+)',(ROOT/'src/pokemon_icon.c').read_text()))
files=dict(re.findall(r'const u8 (gMonIcon_\w+)\[\] = INCBIN_U8\(\s*"([^"\n]+)"',(ROOT/'src/data/graphics/pokemon.h').read_text()))
info=(ROOT/'src/data/pokemon/species_info.h').read_text()
blocks=re.split(r'\[(SPECIES_\w+)\]\s*=',info)
species_types={}
for i in range(1,len(blocks),2):
    match=re.search(r'\.types\s*=\s*\{TYPE_(\w+),\s*TYPE_(\w+)\}',blocks[i+1])
    if match: species_types[blocks[i]]=list(match.groups())
    elif 'OLD_UNOWN_SPECIES_INFO' in blocks[i+1]: species_types[blocks[i]]=['NORMAL','NORMAL']
metadata={}
for symbol,number in constants.items():
    if number==0 or number>439: continue
    name=english.get(symbol,symbol.removeprefix('SPECIES_').replace('_',' ').title())
    display=name.title().replace("Farfetch'D","Farfetch'd")
    path=files.get(icons.get(symbol,''))
    icon=None
    if symbol=='SPECIES_EGG': path='graphics/pokemon/egg/icon.4bpp'; display='Egg'
    if path and (ROOT/Path(path).with_suffix('.png')).exists():
        image=Image.open(ROOT/Path(path).with_suffix('.png')).convert('RGBA')
        image=image.crop((0,0,32,32))
        icon=str(number)+'.png'; image.save(TARGET/icon,optimize=True)
    metadata[str(number)]=dict(name=display,original=name,icon=icon,types=species_types.get(symbol,[]))
(TARGET/'species.json').write_text(json.dumps(metadata,ensure_ascii=False,separators=(',',':')),encoding='utf-8')
(TARGET/'SOURCES.txt').write_text('Sprites: graphics/pokemon in FRLG-Plus.\nEnglish names: src/data/text/species_names.h in FRLG-Plus.\nAssets ship with the companion.\n',encoding='utf-8')
print(len(metadata),'species; icons:',len(list(TARGET.glob('*.png'))))
