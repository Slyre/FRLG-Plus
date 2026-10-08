$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot '../../remote/setup-common.ps1')
$taskFixture = Join-Path $PSScriptRoot ('../../build/tests/launch-block-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $taskFixture | Out-Null
$taskParameters = @{FilePath=(Join-Path $taskFixture 'emulator-soullink/mGBA.exe'); WorkingDirectory=$taskFixture; PassThru=$true}
function Start-Process { param($FilePath,$WorkingDirectory,$PassThru,$ErrorAction) throw (New-Object ComponentModel.Win32Exception(4551)) }
function Get-AuthenticodeSignature { param($LiteralPath,$ErrorAction) return [pscustomobject]@{Status='NotSigned'} }
function Get-WinEvent { param($FilterHashtable,$MaxEvents,$ErrorAction) return @([pscustomobject]@{Id=3077;Message='Blocked emulator-soullink\dependency.dll'},[pscustomobject]@{Id=3077;Message='Unrelated personal event'}) }
$taskBlocked = $false
try { Start-SoulLinkEmulatorProcess $taskParameters | Out-Null } catch { $taskBlocked = $_.Exception.Message.Contains('Windows application control blocked') }
if (-not $taskBlocked) { throw 'Windows block was not identified' }
$taskReport = Get-Content -LiteralPath (Join-Path $taskFixture 'launch-diagnostic.txt') -Raw
if ($taskReport -notmatch '4551' -or $taskReport -notmatch 'dependency.dll' -or $taskReport -match 'Unrelated personal') { throw 'Diagnostic incomplete or unrelated events included' }
function Start-Process { param($FilePath,$WorkingDirectory,$PassThru,$ErrorAction) return [pscustomobject]@{Id=123} }
if ((Start-SoulLinkEmulatorProcess $taskParameters).Id -ne 123) { throw 'Successful launch changed' }
Write-Output 'PASS application-control failure reporting and successful launch'
