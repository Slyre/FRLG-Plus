$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot '../../remote/setup-common.ps1')
. (Join-Path $PSScriptRoot '../../remote/manual-launch.ps1')
$taskFixture=Join-Path $PSScriptRoot ('../../build/tests/manual-'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $taskFixture | Out-Null
$script:taskStarted=@()
function Start-Process {param($FilePath,$ArgumentList,$WorkingDirectory,$WindowStyle) $script:taskStarted+=$FilePath}
function Get-SoulLinkStopRequested {return $true}
foreach($taskRole in @('a','b')) {
    $taskScript=Join-Path $taskFixture ('player-'+$taskRole+'.lua')
    [IO.File]::WriteAllText($taskScript,$taskRole)
    Start-SoulLinkManualSession ([pscustomobject]@{HasExited=$false}) $taskFixture 'test/mGBA.exe' 'test/lgfr-sl.gba' $taskScript
    if ([IO.File]::ReadAllText((Join-Path $taskFixture 'soul-link.lua')) -ne $taskRole) {throw 'Wrong player script'}
}
if ($script:taskStarted.Count -ne 2 -or @($script:taskStarted | Where-Object {$_ -ne 'powershell.exe'}).Count) {throw 'Manual mode started the emulator'}
Write-Output 'PASS manual mode: prepares A/B script, opens companion, never launches mGBA'
