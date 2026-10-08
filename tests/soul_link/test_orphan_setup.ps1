$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot '../../remote/setup-common.ps1')
$script:testRoot = 'C:\Test Soul Link'
$script:testGame = $false
$script:testParent = $null
$script:testStopped = @()
$script:testProcesses = @()
function Get-Process { param($Name,$ErrorAction) if ($script:testGame) { [pscustomobject]@{Path=(Join-Path $script:testRoot 'emulator-soullink/mGBA.exe')} } }
function Get-CimInstance { param($ClassName,$Filter) if ($Filter -eq "Name = 'python.exe'") { $script:testProcesses } else { $script:testParent } }
function Stop-Process { param($Id,$ErrorAction) $script:testStopped += $Id }
$script:testProcesses = @(
    [pscustomobject]@{ProcessId=10;ParentProcessId=1;CreationDate=[datetime]'2026-10-06T12:00:00';ExecutablePath='C:\Other\python.exe';CommandLine='other'},
    [pscustomobject]@{ProcessId=20;ParentProcessId=2;CreationDate=[datetime]'2026-10-06T12:00:00';ExecutablePath=(Join-Path $script:testRoot 'runtime/python.exe');CommandLine=('"'+(Join-Path $script:testRoot 'network.py')+'"')})
Clear-SoulLinkOrphanNetwork $script:testRoot
if ($script:testStopped.Count -ne 1 -or $script:testStopped[0] -ne 20) { throw 'Wrong orphan selection' }
$script:testStopped=@();$script:testParent=[pscustomobject]@{CreationDate=[datetime]'2026-10-06T11:00:00'}
$testRejected=$false;try { Clear-SoulLinkOrphanNetwork $script:testRoot } catch { $testRejected=$true }
if (-not $testRejected -or $script:testStopped.Count) { throw 'Live launcher was not protected' }
$script:testParent=[pscustomobject]@{CreationDate=[datetime]'2026-10-06T13:00:00'}
Clear-SoulLinkOrphanNetwork $script:testRoot
if ($script:testStopped.Count -ne 1) { throw 'Reused parent PID prevented orphan cleanup' }
$script:testStopped=@();$script:testGame=$true
$testRejected=$false;try { Clear-SoulLinkOrphanNetwork $script:testRoot } catch { $testRejected=$true }
if (-not $testRejected -or $script:testStopped.Count) { throw 'Live game was not protected' }
Write-Host 'PASS orphan cleanup: exact kit, foreign process preserved, live launcher/game protected, parent PID reuse handled.'
$script:testRootOther = Join-Path $PSScriptRoot ('../../build/tests/other-kit-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path (Join-Path $script:testRootOther 'runtime') -Force | Out-Null
$taskOtherPython = Join-Path $script:testRootOther 'runtime/python.exe'
$taskOtherNetwork = Join-Path $script:testRootOther 'network.py'
[IO.File]::WriteAllText($taskOtherPython, 'fake python for cleanup test')
[IO.File]::WriteAllText($taskOtherNetwork, 'fake network for cleanup test')
Write-SoulLinkJson (Join-Path $script:testRootOther 'installation-manifest.json') @{version=2;publicRelay='gbfbc.com';packaged=@{'network.py'=(Get-FileHash $taskOtherNetwork).Hash};files=@{'runtime/python.exe'=(Get-FileHash $taskOtherPython).Hash}}
Write-SoulLinkJson (Join-Path $script:testRootOther 'compagnon-web.json') @{pid=90}
$script:testProcesses=@([pscustomobject]@{ProcessId=90;ParentProcessId=3;CreationDate=[datetime]'2026-10-06T12:00:00';ExecutablePath=$taskOtherPython;CommandLine=('"'+$taskOtherNetwork+'"')})
$script:testGame=$false;$script:testParent=$null;$script:testStopped=@();$script:testOwner=90
function Get-NetTCPConnection { param($LocalPort,$State,$ErrorAction) [pscustomobject]@{OwningProcess=$script:testOwner} }
Clear-SoulLinkOrphanNetwork $script:testRoot
if ($script:testStopped.Count -ne 1 -or $script:testStopped[0] -ne 90) {throw 'Verified other-kit orphan not cleared'}
$script:testStopped=@();$script:testOwner=91
Clear-SoulLinkOrphanNetwork $script:testRoot
if ($script:testStopped.Count) {throw 'Non-owning other kit was stopped'}
$script:testOwner=90;$script:testParent=[pscustomobject]@{CreationDate=[datetime]'2026-10-06T11:00:00'}
$testRejected=$false;try {Clear-SoulLinkOrphanNetwork $script:testRoot} catch {$testRejected=$true}
if (-not $testRejected -or $script:testStopped.Count) {throw 'Active other-kit launcher was stopped'}
$script:testParent=$null
function Get-Process {param($Name,$ErrorAction) [pscustomobject]@{Path=(Join-Path $script:testRootOther 'emulator-official/mGBA.exe')} }
$testRejected=$false;try {Clear-SoulLinkOrphanNetwork $script:testRoot} catch {$testRejected=$true}
if (-not $testRejected -or $script:testStopped.Count) {throw 'Active official game was stopped'}
Write-Host 'PASS cross-kit cleanup: verified bridge owner only, active launcher and official game protected.'
