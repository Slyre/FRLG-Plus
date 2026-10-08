function Get-SoulLinkStopRequested {
    try {
        if ($Host.UI.RawUI.KeyAvailable) { return $Host.UI.RawUI.ReadKey('NoEcho,IncludeKeyDown').VirtualKeyCode -eq 81 }
    } catch {}
    return $false
}
function Start-SoulLinkManualSession($Network, [string]$Root, [string]$Emulator, [string]$Rom, [string]$Script) {
    Copy-Item -LiteralPath $Script -Destination (Join-Path $Root 'soul-link.lua') -Force
    $taskBrowserArguments = @('-NoProfile','-ExecutionPolicy','Bypass','-File',(Join-Path $Root 'companion.ps1'))
    Start-Process -FilePath 'powershell.exe' -ArgumentList (($taskBrowserArguments | ForEach-Object {Quote-SoulLinkArgument $_}) -join ' ') -WorkingDirectory $Root -WindowStyle Hidden | Out-Null
    Write-Host 'The network is ready. Open mGBA yourself:' -ForegroundColor Cyan
    Write-Host $Emulator
    Write-Host ('In mGBA, File > Load ROM: ' + $Rom)
    Write-Host 'Then Tools > Scripting > File > Load script: choose soul-link.lua from this folder.'
    Write-Host 'Keep this console open. Save and close the game before pressing Q to stop the connection.'
    while (-not $Network.HasExited) {
        if (Get-SoulLinkStopRequested) { return }
        Start-Sleep -Milliseconds 500
    }
    throw 'The network companion stopped. Check reseau-erreurs.log. Save and close mGBA before reconnecting.'
}
