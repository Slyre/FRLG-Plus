"""Build a clean first-player kit from the current ROM and its symbols.

No downloaded dependencies or player data are copied into the distribution.
"""
import argparse
import hashlib
import json
import re
from pathlib import Path
import shutil
import zipfile
from package import generate_game

SOURCE = Path(__file__).parent


def build(rom, symbols, target, relay='gbfbc.com'):
    if not isinstance(relay, str) or len(relay) > 253 or re.fullmatch(r'(?:[a-zA-Z0-9](?:[a-zA-Z0-9-]{0,61}[a-zA-Z0-9])?\.)+[a-zA-Z]{2,63}', relay) is None:
        raise ValueError('The relay must be a valid DNS hostname.')
    if target.exists():
        raise ValueError('Choose a new empty folder to keep player data out of the package.')
    target.mkdir(parents=True)
    generate_game(rom, symbols, target)
    names = ['network.py', 'companion_core.py', 'companion_web.py', 'companion.ps1', 'setup.ps1', 'setup-common.ps1', 'setup-public.ps1', 'bootstrap.ps1', 'manual-launch.ps1', 'README.txt']
    if relay:
        names.append('relay-ca.pem')
    for name in names:
        shutil.copyfile(SOURCE / name, target / name)
    shutil.copytree(SOURCE / 'companion-assets', target / 'companion-assets')
    shutil.copytree(SOURCE / 'companion-web', target / 'companion-web')
    (target / 'companion.bat').write_text(
        '@echo off\npowershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0companion.ps1"\nif errorlevel 1 pause\n',
        encoding='ascii', newline='\r\n')
    (target / 'play.bat').write_text(
        '@echo off\npowershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0setup.ps1" -Stable %*\nif errorlevel 1 pause\n',
        encoding='ascii', newline='\r\n')
    (target / 'connect.bat').write_text(
        '@echo off\npowershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0setup.ps1" -Stable -ManualEmulator %*\nif errorlevel 1 pause\n',
        encoding='ascii', newline='\r\n')
    # Installed-file pins are independent of an old bundled distribution.
    installed = {
        'runtime/python.exe': '434b361fe0c5960d404974feed493db47f2603a1ace2574312104fbed0182c6f',
        'runtime/python314.dll': '854fe7549eb4b2ac478935a38ae2ca1b774c6fdc3d87f0263fbd697ff86d7149',
        'runtime/python314.zip': 'dfa08b46ce09089b7e6bc139784d5609a07770bab66df8d37938ca9bd508e436',
    }
    def component(label, name, url, digest, prefix):
        return dict(label=label, name=name, url=url, sha256=digest,
                    files={k: v for k, v in installed.items() if k.startswith(prefix)})
    downloads = {
        'python': component('Python portable 3.14.8', 'python.zip',
            'https://www.python.org/ftp/python/3.14.8/python-3.14.8-embed-amd64.zip',
            'a93abe456ab01bd96d7a085b3cdb6566b3063f4241360d114142fbdb07f0a310', 'runtime/'),
    }
    downloads['mgba_stable'] = json.loads((SOURCE / 'mgba-autoload.json').read_text(encoding='utf-8'))
    packaged = {p.relative_to(target).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest() for p in target.rglob('*') if p.is_file()}
    files = dict(packaged)
    for spec in downloads.values():
        files.update(spec.get('files', {}))
    manifest = dict(version=2, defaultEmulator='stable', packaged=packaged, files=files, downloads=downloads)
    if relay:
        manifest['publicRelay'] = relay.lower()
    (target / 'installation-manifest.json').write_text(json.dumps(manifest, indent=2), encoding='utf-8')
    with zipfile.ZipFile(target.with_suffix('.zip'), 'w', zipfile.ZIP_DEFLATED) as archive:
        for p in sorted(target.rglob('*')):
            if p.is_file():
                archive.write(p, target.name + '/' + p.relative_to(target).as_posix())


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('rom', type=Path)
    parser.add_argument('symbols', type=Path)
    parser.add_argument('target', type=Path)
    parser.add_argument('--relay', default='gbfbc.com', help='VPS relay hostname; players do not need Tailscale')
    args = parser.parse_args()
    build(args.rom, args.symbols, args.target, args.relay)
