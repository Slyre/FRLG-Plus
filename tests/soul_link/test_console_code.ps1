$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot '../../remote/setup-common.ps1')
. (Join-Path $PSScriptRoot '../../remote/setup-public.ps1')
$taskFixture=Join-Path $PSScriptRoot ('../../build/tests/console-code-'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $taskFixture | Out-Null
$taskInvitation=Join-Path $taskFixture 'invitation.json'
Write-SoulLinkJson $taskInvitation @{version=2;server='gbfbc.com';port=8443;role='B';secret=('f'*48)}
$script:taskOutput=@();$script:taskClipboard=$null
function Write-Host {param($Object,$ForegroundColor) $script:taskOutput+=[string]$Object}
function Set-Clipboard {param($Value) $script:taskClipboard=$Value}
Copy-SoulLinkCode $taskInvitation
if ($script:taskClipboard -notlike 'SL2:*' -or $script:taskClipboard -notin $script:taskOutput) {throw 'Code missing from console or clipboard'}
$script:taskOutput=@()
function Set-Clipboard {param($Value) throw 'Clipboard unavailable'}
try {Copy-SoulLinkCode $taskInvitation} catch {}
if (-not @($script:taskOutput | Where-Object {$_ -like 'SL2:*'}).Count) {throw 'Clipboard failure hid console code'}
Write-Output 'PASS room code visible in console and clipboard, including clipboard failure'
