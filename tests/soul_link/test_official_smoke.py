"""Start the unchanged official binary headlessly; manual script loading is separate."""
import argparse,os,shutil,subprocess,tempfile,time
from pathlib import Path
parser=argparse.ArgumentParser();parser.add_argument('package',type=Path);args=parser.parse_args()
root=Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory(prefix='official-smoke-',dir=root/'build/tests') as folder:
    fixture=Path(folder)
    shutil.copytree(args.package/'emulator-official',fixture/'emulator')
    shutil.copyfile(args.package/'lgfr-sl.gba',fixture/'lgfr-sl.gba')
    (fixture/'emulator/qt.ini').write_text('[General]\ndisplayDriver=0\nlanguage=en\n')
    env=dict(os.environ,QT_QPA_PLATFORM='offscreen',QT_QPA_FONTDIR=str(Path(os.environ['WINDIR'])/'Fonts'))
    startup=subprocess.STARTUPINFO();startup.dwFlags|=subprocess.STARTF_USESHOWWINDOW;startup.wShowWindow=0
    exe=fixture/'emulator/mGBA.exe'
    help_result=subprocess.run([str(exe),'--help'],env=env,capture_output=True,timeout=20,startupinfo=startup)
    text=(help_result.stdout+help_result.stderr).decode(errors='replace')
    (root/'build/tests/official-help.txt').write_text(text)
    assert help_result.returncode==0,text
    with (fixture/'launch.log').open('wb') as log:
        process=subprocess.Popen([str(exe),str(fixture/'lgfr-sl.gba')],cwd=fixture,env=env,stdout=log,stderr=log,startupinfo=startup)
        try:
            time.sleep(5)
            assert process.poll() is None,(process.returncode,(fixture/'launch.log').read_text(errors='replace'))
        finally:
            if process.poll() is None:process.terminate()
            process.wait(timeout=20)
    print('PASS unchanged official mGBA: CLI help and headless ROM startup. Manual scripting and target-PC Smart App Control remain to test.')
