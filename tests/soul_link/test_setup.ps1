param([string]$Python)
$ErrorActionPreference = 'Stop'
$taskRepo = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
. (Join-Path $taskRepo 'remote\setup-common.ps1')
. (Join-Path $taskRepo 'remote\setup-public.ps1')
function MustReject([scriptblock]$Action) {
    $taskRejected = $false
    try { & $Action | Out-Null } catch { $taskRejected = $true }
    if (-not $taskRejected) { throw 'Invalid input accepted' }
}
foreach ($taskFile in (Get-ChildItem (Join-Path $taskRepo 'remote') -Filter '*.ps1')) {
    $taskTokens = $null; $taskErrors = $null
    [void][Management.Automation.Language.Parser]::ParseFile($taskFile.FullName,[ref]$taskTokens,[ref]$taskErrors)
    if ($taskErrors.Count) { throw ($taskErrors | Out-String) }
}
$taskFixture = Join-Path $taskRepo 'build\tests\setup-fixtures'
New-Item -ItemType Directory -Path $taskFixture -Force | Out-Null
$taskLivePath = Join-Path $taskFixture 'live.log'
if ((Read-SoulLinkLiveLog (Join-Path $taskFixture 'absent.log')) -ne '') { throw 'Missing log did not return empty text' }
$taskWriterStream = [IO.File]::Open($taskLivePath, [IO.FileMode]::Create, [IO.FileAccess]::Write, [IO.FileShare]::Read)
$taskWriter = New-Object IO.StreamWriter($taskWriterStream)
try {
    $taskWriter.Write('Relais Internet actif'); $taskWriter.Flush()
    if ((Read-SoulLinkLiveLog $taskLivePath) -ne 'Relais Internet actif') { throw 'Active authentication log not readable' }
    $taskWriter.Write("`nRelais connecte"); $taskWriter.Flush()
    if ((Read-SoulLinkLiveLog $taskLivePath) -notmatch 'Relais connecte') { throw 'Active log updates not readable' }
} finally { $taskWriter.Dispose() }
$taskExclusive = [IO.File]::Open($taskLivePath, [IO.FileMode]::Open, [IO.FileAccess]::Write, [IO.FileShare]::None)
try {
    if ((Read-SoulLinkLiveLog $taskLivePath) -ne '') { throw 'Exclusive log did not defer reading' }
} finally { $taskExclusive.Dispose() }
if ((Read-SoulLinkLiveLog $taskLivePath) -notmatch 'Relais connecte') { throw 'Log did not recover after unlock' }
$taskInvitation = Join-Path $taskFixture 'invitation.json'
Write-SoulLinkJson $taskInvitation @{version=2;role='B';server='gbfbc.com';port=8443;secret=('a'*48)}
$taskCode = ConvertTo-SoulLinkPublicCode ([IO.File]::ReadAllText($taskInvitation) | ConvertFrom-Json)
function Set-Clipboard([string]$Value) { $script:taskClipboardProbe = $Value }
Copy-SoulLinkCode $taskInvitation
if ($script:taskClipboardProbe -ne $taskCode) { throw 'Clipboard code differs from invitation' }
MustReject { Copy-SoulLinkCode (Join-Path $taskFixture 'no-invitation.json') }
[IO.File]::WriteAllText($taskInvitation,('x'*4097))
MustReject {Copy-SoulLinkCode $taskInvitation}
$taskHash = (Get-FileHash -LiteralPath $taskInvitation -Algorithm SHA256).Hash
Assert-SoulLinkHash $taskInvitation $taskHash
[IO.File]::AppendAllText($taskInvitation,'changed')
MustReject {Assert-SoulLinkHash $taskInvitation $taskHash}
$taskProbe = Join-Path $taskFixture 'argument-probe.py'
[IO.File]::WriteAllText($taskProbe,'import json,sys; open(sys.argv[1],"w",encoding="utf-8").write(json.dumps(sys.argv[2:]))')
$taskResult = Join-Path $taskFixture 'arguments.json'
$taskValues = @('C:\folder with spaces\file.py','literal"quote','ends\','with$()&stuff','')
$taskPython = if ($Python) { (Resolve-Path -LiteralPath $Python).ProviderPath } else { (Get-Command python.exe -ErrorAction Stop).Source }
$taskArguments = @($taskProbe,$taskResult)+$taskValues
$taskChild = Start-Process -FilePath $taskPython -ArgumentList (($taskArguments | ForEach-Object {Quote-SoulLinkArgument $_}) -join ' ') -WindowStyle Hidden -PassThru -Wait
if ($taskChild.ExitCode -ne 0) { throw 'Argument probe failed' }
$taskActual = [IO.File]::ReadAllText($taskResult) | ConvertFrom-Json
for ($taskIndex=0; $taskIndex -lt $taskValues.Count; $taskIndex++) {
    if ($taskActual[$taskIndex] -cne $taskValues[$taskIndex]) { throw "Command-line argument changed at $taskIndex" }
}
'Windows PowerShell setup: parse, clipboard, size bounds, hash refusal and real process argument quoting passed'
