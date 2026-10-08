"""Build an isolated test kit using unchanged files from the official release."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import zipfile
from package_minimal import build, SOURCE

def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def build_official(rom, symbols, target, official, archive):
    if not (official/'mGBA.exe').is_file():
        raise ValueError('Choose the extracted official Windows portable release folder.')
    build(rom,symbols,target)
    shutil.copytree(official,target/'emulator-official')
    shutil.copyfile(SOURCE/'official-launch.ps1',target/'official-launch.ps1')
    setup=(SOURCE/'setup.ps1').read_text(encoding='utf-8-sig')
    setup=setup.replace(". (Join-Path $PSScriptRoot 'setup-common.ps1')", ". (Join-Path $PSScriptRoot 'setup-common.ps1')\n. (Join-Path $PSScriptRoot 'official-launch.ps1')")
    setup=setup.replace('emulator-soullink','emulator-official')
    setup=setup.replace("$taskGameArgs = '-C ' + (Quote-SoulLinkArgument ('soulLinkScript=' + $taskScript)) + ' ' + (Quote-SoulLinkArgument $taskRom)","Copy-Item -LiteralPath $taskScript -Destination (Join-Path $PSScriptRoot 'soul-link.lua') -Force\n    $taskGameArgs = Quote-SoulLinkArgument $taskRom")
    setup=setup.replace('Loading the script for player {0}…','Official mGBA test, player {0}. Load soul-link.lua in Tools > Scripting.')
    a=setup.index('    $taskScriptReady = $false')
    b=setup.index('    $taskGuiArguments =',a)
    setup=setup[:a]+setup[b:]
    a=setup.index('    Write-Host ("Player {0} script loaded.')
    setup=setup[:a]+'''    Write-Host 'In mGBA: Tools > Scripting, then File > Load script.' -ForegroundColor Yellow
    Write-Host 'Choose soul-link.lua from this test folder. Repeat after restarting mGBA or resetting the script.'
    Write-Host 'Leave the scripting window open until the companion sees your game.'
    $taskScriptStarted = ([DateTime]::UtcNow - [DateTime]'1970-01-01').TotalSeconds
    Wait-SoulLinkOfficialScript $taskGame $taskCompanion $PSScriptRoot $taskScriptStarted
'''+setup[a:]
    (target/'setup.ps1').write_text(setup,encoding='utf-8-sig')
    bootstrap=(SOURCE/'bootstrap.ps1').read_text(encoding='utf-8-sig')
    bootstrap=bootstrap.replace("@('python',$taskMgbaComponent)","@('python')").replace('emulator-soullink','emulator-official')
    (target/'bootstrap.ps1').write_text(bootstrap,encoding='utf-8-sig')
    page=(target/'companion-web/index.html').read_text(encoding='utf-8')
    page=page.replace('<div class="connection">','<p class="small">Official mGBA test: at each launch, open Tools → Scripting → File → Load script and choose soul-link.lua.</p><div class="connection">',1)
    (target/'companion-web/index.html').write_text(page,encoding='utf-8')
    guide='''LGFR-SL — OFFICIAL mGBA TEST

This is a separate experiment. Keep your regular lgfr-sl folder and ZIP.
The emulator is the unmodified official mGBA 0.10.5 Windows 64-bit portable
release. Its executable is unsigned; Smart App Control may still block it.
This test does not change Windows security settings.

1. Extract this ZIP to its own folder. Close the regular Soul Link launcher.
2. Run play.bat. It downloads portable Python on the first launch.
3. Create or join a room as usual. The companion opens in your browser.
4. In mGBA, open Tools > Scripting, then File > Load script.
5. Select soul-link.lua from this test folder. The launcher selects A or B
   for you. Wait for the companion to display your game before playing.

Load soul-link.lua again each time you restart mGBA. If the script is reset,
reload it as well. This release does not have our automatic script patch.

If Windows blocks the emulator, send launch-diagnostic.txt to the maintainer.
The official version is not guaranteed to pass Smart App Control.

To test an existing save, close both games and copy lgfr-sl.sav into this
folder. Keep the original save as a backup. Never run both kits at once on
the same PC. Start a new room with fresh saves for a separate test run.

Only move back a test save after saving in-game and closing both games.
The Soul Link connection and death journals also belong to the same run;
do not mix them between unrelated runs.

Keyboard/controller: Tools > Settings > Keyboard or Controllers.
companion.bat reopens the local companion. Dark theme / Light theme switches
its appearance. Save in Pokemon and keep the launcher console open.

Official download: https://mgba.io/downloads.html
'''
    (target/'README.txt').write_text(guide,encoding='utf-8')
    manifest=json.loads((target/'installation-manifest.json').read_text())
    manifest['defaultEmulator']='official-test'
    manifest['downloads']={'python':manifest['downloads']['python']}
    manifest['officialEmulator']=dict(version='0.10.5',source='https://github.com/mgba-emu/mgba/releases/download/0.10.5/mGBA-0.10.5-win64.7z',archiveSha256=digest(archive),executableSha256=digest(official/'mGBA.exe'),modified=False,scriptLoading='manual')
    manifest['packaged']={p.relative_to(target).as_posix():digest(p) for p in target.rglob('*') if p.is_file() and p.name!='installation-manifest.json'}
    manifest['files']=dict(manifest['packaged'])
    manifest['files'].update(manifest['downloads']['python']['files'])
    (target/'installation-manifest.json').write_text(json.dumps(manifest,indent=2),encoding='utf-8')
    with zipfile.ZipFile(target.with_suffix('.zip'),'w',zipfile.ZIP_DEFLATED) as z:
        for p in sorted(target.rglob('*')):
            if p.is_file():z.write(p,Path(target.name)/p.relative_to(target))

if __name__=='__main__':
    parser=argparse.ArgumentParser()
    for name in ('rom','symbols','target','official','archive'):parser.add_argument(name,type=Path)
    args=parser.parse_args()
    build_official(args.rom,args.symbols,args.target,args.official,args.archive)
