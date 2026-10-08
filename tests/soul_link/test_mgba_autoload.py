"""Exercise the actual adapted Windows Qt emulator, without UI automation."""
from pathlib import Path
import argparse, os, shutil, socket, subprocess, tempfile, time
parser=argparse.ArgumentParser()
parser.add_argument('emulator',type=Path)
parser.add_argument('package',type=Path)
a=parser.parse_args()
exe=a.emulator.resolve(); package=a.package.resolve()
base=Path(__file__).resolve().parents[2]/'build/tests'; base.mkdir(parents=True,exist_ok=True)
with tempfile.TemporaryDirectory(prefix='autoload-',dir=base) as folder:
    fixture=Path(folder)
    shutil.copytree(exe.parent,fixture/'emulator'); exe=fixture/'emulator'/exe.name
    (exe.parent/'portable.ini').write_bytes(b'')
    (exe.parent/'qt.ini').write_text('[General]\ndisplayDriver=0\n',encoding='utf-8')
    rom=fixture/'lgfr-sl.gba'; shutil.copy2(package/rom.name,rom)
    env=dict(os.environ,QT_QPA_PLATFORM='offscreen',QT_QPA_FONTDIR=str(Path(os.environ['WINDIR'])/'Fonts'))
    startup=subprocess.STARTUPINFO(); startup.dwFlags|=subprocess.STARTF_USESHOWWINDOW; startup.wShowWindow=0
    for role,number in [('a',0),('b',1)]:
        script=fixture/('player-'+role+'.lua'); shutil.copy2(package/script.name,script)
        with socket.socket() as server:
            server.setsockopt(socket.SOL_SOCKET,socket.SO_REUSEADDR,1); server.bind(('127.0.0.1',0)); server.listen(); server.settimeout(25)
            script.write_text('SOUL_LINK_LOCAL_PORT = '+str(server.getsockname()[1])+'\n'+script.read_text(encoding='utf-8'),encoding='utf-8')
            with (fixture/(role+'.log')).open('wb') as log:
                process=subprocess.Popen([str(exe),'-C','soulLinkScript='+str(script),str(rom)],cwd=fixture,env=env,stdout=log,stderr=log,startupinfo=startup)
                try:
                    conn,_=server.accept(); conn.settimeout(20)
                    with conn:
                        # Telemetry is asynchronous and may precede either heartbeat.
                        deadline=time.monotonic()+20; heartbeats=0; views=0
                        while heartbeats<2:
                            assert time.monotonic()<deadline
                            data=b''
                            while len(data)<76:
                                chunk=conn.recv(76-len(data)); assert chunk; data+=chunk
                            assert data[:4]==b'SLN1' and data[5]==number,repr(data)
                            if data[4]==8: heartbeats+=1
                            else:
                                assert data[4]==9,repr(data); views+=1
                        assert views>0,'Companion telemetry missing'
                    assert process.poll() is None
                except Exception:
                    print('Emulator exit:',process.poll())
                    print((fixture/(role+'.log')).read_text(encoding='utf-8',errors='replace'))
                    raise
                finally:
                    if process.poll() is None: process.terminate()
                    process.wait(timeout=20)
            text=(fixture/(role+'.log')).read_text(encoding='utf-8',errors='replace')
            assert 'SoulLink autoload: ready' in text,text
            assert 'compagnon local connecté ('+role.upper()+')' in text,text
            assert 'SoulLink autoload: failed' not in text,text
            print('PASS automatic script '+role.upper()+' and two real emulator heartbeats')
    # Without a ROM, the controls mode must not attach a script.
    with (fixture/'controls.log').open('wb') as log:
        process=subprocess.Popen([str(exe),'-C','soulLinkScript='+str(script)],cwd=fixture,env=env,stdout=log,stderr=log,startupinfo=startup)
        try:
            time.sleep(2); assert process.poll() is None
        finally:
            process.terminate(); process.wait(timeout=20)
    assert 'SoulLink autoload:' not in (fixture/'controls.log').read_text(errors='replace')
    print('PASS no script when no game is loaded')
