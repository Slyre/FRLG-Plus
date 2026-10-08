$ErrorActionPreference='Stop'
try {
    $taskPath=Join-Path $PSScriptRoot 'compagnon-web.json'
    if(-not (Test-Path -LiteralPath $taskPath)){throw 'Run play.bat before opening the companion.'}
    if((Get-Item -LiteralPath $taskPath).Length -gt 4096){throw 'Invalid local address.'}
    $taskRegistration=[IO.File]::ReadAllText($taskPath) | ConvertFrom-Json
    if($taskRegistration.url -cnotmatch '^http://127\.0\.0\.1:[0-9]{1,5}/session/[0-9a-f]{48}/$'){throw 'Invalid local address.'}
    $taskResponse=Invoke-WebRequest -UseBasicParsing -Uri ($taskRegistration.url+'health') -TimeoutSec 3
    if($taskResponse.StatusCode -ne 200){throw 'The companion has stopped. Run play.bat again.'}
    Start-Process -FilePath $taskRegistration.url
} catch {
    Write-Host ('Cannot open the companion: '+$_.Exception.Message) -ForegroundColor Red
    exit 1
}