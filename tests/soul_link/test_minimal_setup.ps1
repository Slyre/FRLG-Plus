param([Parameter(Mandatory=$true)][string]$Package)
$ErrorActionPreference = 'Stop'
$taskFixture = Join-Path (Join-Path $PSScriptRoot '../../build/tests') ('minimal-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $taskFixture -Force | Out-Null
$taskSourceManifest = Get-Content -LiteralPath (Join-Path $Package 'installation-manifest.json') -Raw | ConvertFrom-Json
foreach ($taskName in @($taskSourceManifest.packaged.PSObject.Properties.Name) + @('installation-manifest.json')) {
    $taskDestination = Join-Path $taskFixture $taskName
    New-Item -ItemType Directory -Path (Split-Path -Parent $taskDestination) -Force | Out-Null
    Copy-Item -LiteralPath (Join-Path $Package $taskName) -Destination $taskDestination
}
foreach ($taskScript in Get-ChildItem -LiteralPath $taskFixture -Filter '*.ps1') {
    $taskTokens = $null; $taskErrors = $null
    [Management.Automation.Language.Parser]::ParseFile($taskScript.FullName, [ref]$taskTokens, [ref]$taskErrors) | Out-Null
    if ($taskErrors.Count) { throw "Erreur de syntaxe : $($taskScript.Name)" }
}
. (Join-Path $taskFixture 'setup-common.ps1')
. (Join-Path $taskFixture 'bootstrap.ps1')
$taskManifest = Get-Content -LiteralPath (Join-Path $taskFixture 'installation-manifest.json') -Raw | ConvertFrom-Json
foreach ($taskProperty in $taskManifest.packaged.PSObject.Properties) {
    Assert-SoulLinkHash (Join-Path $taskFixture $taskProperty.Name) $taskProperty.Value
}
New-Item -ItemType Directory -Path (Join-Path $taskFixture 'downloads') | Out-Null
$taskBadArchive = Join-Path $taskFixture 'downloads/python.zip'
[IO.File]::WriteAllText($taskBadArchive, 'bad archive')
$taskRejected = $false
try { Get-SoulLinkDownload $taskManifest.downloads.python | Out-Null } catch { $taskRejected = $true }
if (-not $taskRejected -or (Test-Path -LiteralPath (Join-Path $taskFixture 'runtime'))) { throw 'Corrupted archive was not rejected before extraction.' }
Remove-Item -LiteralPath $taskBadArchive
$taskOldSettings = Join-Path $taskFixture 'emulator-stable'
New-Item -ItemType Directory -Path $taskOldSettings | Out-Null
[IO.File]::WriteAllText((Join-Path $taskOldSettings 'config.ini'), "[ports.qt]`nvolume=75`n")
[IO.File]::WriteAllText((Join-Path $taskOldSettings 'qt.ini'), "[General]`ndisplayDriver=0`n")
Install-SoulLinkComponents $taskManifest
foreach ($taskSetting in @('config.ini')) {
    if ((Get-FileHash -LiteralPath (Join-Path $taskOldSettings $taskSetting)).Hash -ne (Get-FileHash -LiteralPath (Join-Path $taskFixture ('emulator-soullink/' + $taskSetting))).Hash) { throw 'Player settings migration failed.' }
}
$taskPreferences = Join-Path $taskFixture 'emulator-soullink/qt.ini'
$taskText = [IO.File]::ReadAllText($taskPreferences)
if ($taskText -notmatch '(?m)^language=en\r?$' -or $taskText -notmatch '(?m)^displayDriver=0\r?$') { throw 'French initialization or video settings preservation failed.' }
# Reproduce an upgrade from revision 1: old immutable files require replacement,
# while personal button bindings must survive and the old English locale becomes French.
[IO.File]::WriteAllText((Join-Path $taskFixture 'emulator-soullink/SOURCE.txt'), 'mgba-soullink-0.10.5-1-source.zip')
[IO.File]::WriteAllText($taskPreferences, "[General]`nlanguage=en`ndisplayDriver=0`n[custom-controls]`nbuttonA=9`n")
Install-SoulLinkComponents $taskManifest
$taskText = [IO.File]::ReadAllText($taskPreferences)
if ($taskText -notmatch '(?m)^language=en\r?$' -or $taskText -notmatch '(?m)^buttonA=9\r?$') { throw 'Revision 1 settings migration failed.' }
# A subsequent user choice is respected on an ordinary launch.
[IO.File]::WriteAllText($taskPreferences, $taskText.Replace('language=en','language=de'))
Install-SoulLinkComponents $taskManifest -Stable
foreach ($taskProperty in $taskManifest.files.PSObject.Properties) {
    Assert-SoulLinkHash (Join-Path $taskFixture $taskProperty.Name) $taskProperty.Value
}
# A second preparation must not require any downloads.
function Get-SoulLinkDownload { throw 'Unexpected repeated download' }
Install-SoulLinkComponents $taskManifest
Install-SoulLinkComponents $taskManifest -Stable
& (Join-Path $taskFixture 'runtime/python.exe') -X utf8 -c 'import ssl, asyncio, json, sys; print(sys.version)'
if ($LASTEXITCODE -ne 0) { throw 'Portable runtime failed.' }
& (Join-Path $taskFixture 'runtime/python.exe') -X utf8 (Join-Path $taskFixture 'network.py') --help | Out-Null
if ($LASTEXITCODE -ne 0) { throw 'Portable network entry point could not import its companion module.' }
if ([IO.File]::ReadAllText($taskPreferences) -notmatch '(?m)^language=de\r?$') { throw 'Explicit language preference was reset.' }
Write-Host "PASS minimal setup : $taskFixture"
