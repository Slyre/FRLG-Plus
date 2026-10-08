"""Windows integration: abrupt launcher exit closes the detached network child."""
import ctypes,json,os,shutil,socket,subprocess,sys,tempfile,time
from ctypes import wintypes
from pathlib import Path
root=Path(__file__).resolve().parents[2]
assert sys.platform=='win32'
kernel=ctypes.WinDLL('kernel32',use_last_error=True)
kernel.OpenProcess.argtypes=[wintypes.DWORD,wintypes.BOOL,wintypes.DWORD];kernel.OpenProcess.restype=wintypes.HANDLE
kernel.WaitForSingleObject.argtypes=[wintypes.HANDLE,wintypes.DWORD];kernel.WaitForSingleObject.restype=wintypes.DWORD
kernel.TerminateProcess.argtypes=[wintypes.HANDLE,wintypes.UINT]
kernel.CloseHandle.argtypes=[wintypes.HANDLE]
def ps(value):return "'"+str(value).replace("'","''")+"'"
with tempfile.TemporaryDirectory(prefix='lifetime-',dir=root/'build/tests') as folder:
    fixture=Path(folder)
    shutil.copyfile(root/'pokefirered.gba',fixture/'lgfr-sl.gba')
    shutil.copytree(root/'remote/companion-web',fixture/'companion-web')
    (fixture/'companion-assets').mkdir()
    shutil.copyfile(root/'remote/companion-assets/species.json',fixture/'companion-assets/species.json')
    (fixture/'secret.txt').write_text('test-launcher-lifetime-secret-123456')
    with socket.socket() as free:free.bind(('127.0.0.1',0));port=free.getsockname()[1]
    script=f"""
. {ps(root/'remote/setup-common.ps1')}
$taskArgs=@('-B','-X','utf8',{ps(root/'remote/network.py')},'--server','127.0.0.1','--role','A','--secret-file',{ps(fixture/'secret.txt')},'--port','1','--local-port','{port}','--ui-root',{ps(fixture)},'--launcher-pid',[string]$PID)
$taskChild=Start-Process -FilePath {ps(sys.executable)} -ArgumentList (($taskArgs | ForEach-Object {{Quote-SoulLinkArgument $_}}) -join ' ') -WindowStyle Hidden -PassThru -RedirectStandardError {ps(fixture/'network.log')}
[IO.File]::WriteAllText({ps(fixture/'child.pid')},[string]$taskChild.Id)
while($true){{Start-Sleep -Seconds 1}}
"""
    (fixture/'owner.ps1').write_text(script,encoding='utf-8-sig')
    parent_log=(fixture/'parent.log').open('wb')
    parent=subprocess.Popen(['powershell.exe','-NoProfile','-ExecutionPolicy','Bypass','-File',str(fixture/'owner.ps1')],env=dict(os.environ),creationflags=subprocess.CREATE_NO_WINDOW,stdout=parent_log,stderr=parent_log)
    handle=None
    try:
        deadline=time.monotonic()+15
        while not (fixture/'child.pid').exists():
            assert time.monotonic()<deadline and parent.poll() is None,('Launcher startup failed',parent.poll(),(fixture/'parent.log').read_text(errors='replace'))
            time.sleep(.05)
        child=int((fixture/'child.pid').read_text())
        handle=kernel.OpenProcess(0x100001,False,child);assert handle,'Network child missing'
        while not (fixture/'compagnon-web.json').exists():
            assert time.monotonic()<deadline and kernel.WaitForSingleObject(handle,0)==258,(fixture/'network.log').read_text(errors='replace')
            time.sleep(.05)
        with socket.create_connection(('127.0.0.1',port),timeout=2):pass
        started=time.monotonic();parent.terminate();parent.wait(timeout=5)
        while kernel.WaitForSingleObject(handle,0)==258:
            assert time.monotonic()-started<3,'Detached network survived launcher closure'
            time.sleep(.05)
        assert not (fixture/'compagnon-web.json').exists(),'Stale web registration'
        with socket.socket() as released:released.bind(('127.0.0.1',port))
        print(f'PASS abrupt PowerShell launcher exit: network stopped, browser registration removed and bridge port released in {time.monotonic()-started:.2f}s')
    finally:
        if parent.poll() is None:parent.terminate();parent.wait(timeout=5)
        parent_log.close()
        if handle:
            if kernel.WaitForSingleObject(handle,0)==258:kernel.TerminateProcess(handle,1)
            kernel.CloseHandle(handle)
