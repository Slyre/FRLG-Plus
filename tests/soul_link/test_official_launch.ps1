$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot '../../remote/setup-common.ps1')
. (Join-Path $PSScriptRoot '../../remote/official-launch.ps1')
$taskFixture = Join-Path $PSScriptRoot ('../../build/tests/official-wait-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $taskFixture | Out-Null
$taskAlive = [pscustomobject]@{HasExited=$false}
$taskClosed = [pscustomobject]@{HasExited=$true}
$taskNow = ([DateTime]::UtcNow - [DateTime]'1970-01-01').TotalSeconds
function AssertFails([scriptblock]$Action) { $taskFailed=$false; try { & $Action } catch {$taskFailed=$true}; if (-not $taskFailed) { throw 'Invalid script readiness accepted' } }
Write-SoulLinkJson (Join-Path $taskFixture 'compagnon-etat.json') @{updated=$taskNow;local=$null}
AssertFails { Wait-SoulLinkOfficialScript $taskAlive $taskAlive $taskFixture ($taskNow-1) -TimeoutSeconds 1 }
Write-SoulLinkJson (Join-Path $taskFixture 'compagnon-etat.json') @{updated=1;local=@{player=0}}
AssertFails { Wait-SoulLinkOfficialScript $taskAlive $taskAlive $taskFixture ($taskNow-1) -TimeoutSeconds 1 }
Write-SoulLinkJson (Join-Path $taskFixture 'compagnon-etat.json') @{updated=([DateTime]::UtcNow - [DateTime]'1970-01-01').TotalSeconds;local=@{player=0}}
Wait-SoulLinkOfficialScript $taskAlive $taskAlive $taskFixture ($taskNow-1) -TimeoutSeconds 1
AssertFails { Wait-SoulLinkOfficialScript $taskClosed $taskAlive $taskFixture $taskNow -TimeoutSeconds 1 }
AssertFails { Wait-SoulLinkOfficialScript $taskAlive $taskClosed $taskFixture $taskNow -TimeoutSeconds 1 }
Write-Output 'PASS official launcher: fresh game telemetry required, stale state and stopped processes refused'
