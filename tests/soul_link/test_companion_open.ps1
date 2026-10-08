$ErrorActionPreference='Stop'
$taskFixture=Join-Path $PSScriptRoot ('../../build/tests/web-open-'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $taskFixture | Out-Null
Copy-Item -LiteralPath (Join-Path $PSScriptRoot '../../remote/companion.ps1') -Destination $taskFixture
$taskAddress='http://127.0.0.1:45678/session/'+('a'*48)+'/'
[IO.File]::WriteAllText((Join-Path $taskFixture 'compagnon-web.json'),(@{url=$taskAddress;pid=1}|ConvertTo-Json))
function Invoke-WebRequest {param([switch]$UseBasicParsing,$Uri,$TimeoutSec) if($Uri -ne ($taskAddress+'health')){throw 'Unexpected health address'};[pscustomobject]@{StatusCode=200} }
function Start-Process {param($FilePath) $script:openedAddress=$FilePath }
. (Join-Path $taskFixture 'companion.ps1')
if($script:openedAddress -ne $taskAddress){throw 'Default browser did not receive the registered local URL'}
foreach($taskScript in Get-ChildItem -LiteralPath (Join-Path $PSScriptRoot '../../remote') -Filter '*.ps1'){
    $taskTokens=$null;$taskErrors=$null
    [System.Management.Automation.Language.Parser]::ParseFile($taskScript.FullName,[ref]$taskTokens,[ref]$taskErrors)|Out-Null
    if($taskErrors){throw ($taskErrors | Out-String)}
}
Write-Host 'PASS Windows PowerShell 5.1: local companion opener and launcher syntax.'
