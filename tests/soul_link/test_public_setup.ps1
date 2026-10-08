$ErrorActionPreference = 'Stop'
$taskRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
. (Join-Path $taskRoot 'remote/setup-common.ps1')
. (Join-Path $taskRoot 'remote/setup-public.ps1')
function MustReject([scriptblock]$Action) {
    $taskRejected = $false
    try { & $Action | Out-Null } catch { $taskRejected = $true }
    if (-not $taskRejected) { throw 'Unsafe invitation accepted' }
}
foreach ($taskPath in @('setup.ps1','setup-public.ps1','bootstrap.ps1')) {
    $taskTokens = $null; $taskErrors = $null
    [Management.Automation.Language.Parser]::ParseFile((Join-Path $taskRoot "remote/$taskPath"), [ref]$taskTokens, [ref]$taskErrors) | Out-Null
    if ($taskErrors.Count) { throw ($taskErrors | Out-String) }
}
$taskInvite = @{version=2;server='soullink.gbfbc.com';port=8443;role='B';secret=('a'*48)}
$taskCode = ConvertTo-SoulLinkPublicCode $taskInvite
$taskDecoded = ConvertFrom-SoulLinkPublicCode $taskCode
if ($taskDecoded.server -ne 'soullink.gbfbc.com' -or $taskDecoded.secret -ne ('a'*48)) { throw 'Code changed' }
foreach ($taskBadHost in @('127.0.0.1','host','https://soullink.gbfbc.com','a..com','-bad.example.com','a.example.com/path')) {
    MustReject { Assert-SoulLinkPublicHost $taskBadHost }
}
foreach ($taskBadCode in @('SL1:AAAA','SL2:@@','SL2:A',('SL2:' + ('A'*4096)))) {
    MustReject { ConvertFrom-SoulLinkPublicCode $taskBadCode }
}
foreach ($taskBadInvite in @(
    @{version=2;server='soullink.gbfbc.com';port=80;role='B';secret=('a'*48)},
    @{version=2;server='soullink.gbfbc.com';port=8443;role='A';secret=('a'*48)},
    @{version=2;server='soullink.gbfbc.com';port=8443;role='B';secret=('A'*48)})) {
    MustReject { ConvertTo-SoulLinkPublicCode $taskBadInvite }
}
function Read-Host { return '1' }
$taskCreated = Get-SoulLinkPublicConfiguration 'soullink.gbfbc.com' -ConfigureAgain
if ($taskCreated.config.hosting -or $taskCreated.config.transport -ne 'tls' -or $taskCreated.config.role -ne 'A' -or $taskCreated.secret -cnotmatch '^[0-9a-f]{48}$') { throw 'Unsafe public configuration' }
$script:taskReads = @('2',$taskCode)
function Read-Host { $taskAnswer = $script:taskReads[0]; $script:taskReads = @($script:taskReads | Select-Object -Skip 1); return $taskAnswer }
$taskJoined = Get-SoulLinkPublicConfiguration 'soullink.gbfbc.com' -ConfigureAgain
if ($taskJoined.config.role -ne 'B' -or $taskJoined.secret -ne ('a'*48)) { throw 'Join failed' }
$script:taskReads = @('2',$taskCode)
MustReject { Get-SoulLinkPublicConfiguration 'other.example.org' -ConfigureAgain }
Write-Host 'Public setup: parsing, invitation validation, creation, join and endpoint mismatch passed'
