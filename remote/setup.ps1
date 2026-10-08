param([switch]$Stable, [switch]$ConfigureAgain, [switch]$Controls, [switch]$ManualEmulator)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'setup-common.ps1')
. (Join-Path $PSScriptRoot 'setup-public.ps1')
Set-Location -LiteralPath $PSScriptRoot
$taskCompanion = $null
$taskLaunchMutex = $null
$taskOwnMutex = $false
try {
    Write-Host 'Pokémon Soul Link — setup and launch' -ForegroundColor Cyan
    $taskArch = if ($env:PROCESSOR_ARCHITEW6432) { $env:PROCESSOR_ARCHITEW6432 } else { $env:PROCESSOR_ARCHITECTURE }
    if ($taskArch -ne 'AMD64' -or [Environment]::OSVersion.Version.Major -lt 10) { throw 'This package needs 64-bit Windows 10 or 11.' }
    if ($PSScriptRoot.StartsWith('\\')) { throw 'Extract the package to a local folder, not a network share.' }
    $taskManifest = [IO.File]::ReadAllText((Join-Path $PSScriptRoot 'installation-manifest.json')) | ConvertFrom-Json
    $taskBootstrap = $taskManifest.PSObject.Properties.Name -contains 'downloads'
    if ($taskManifest.PSObject.Properties.Name -notcontains 'publicRelay') { throw 'Use the current Internet player package.' }
    if (($taskManifest.PSObject.Properties.Name -contains 'defaultEmulator') -and $taskManifest.defaultEmulator -eq 'stable') {
        $Stable = [switch]$true
    }
    $taskCheckFiles = if ($taskBootstrap) { $taskManifest.packaged } else { $taskManifest.files }
    foreach ($taskProperty in $taskCheckFiles.PSObject.Properties) {
        $taskRelative = $taskProperty.Name
        if ([IO.Path]::IsPathRooted($taskRelative) -or $taskRelative -match '(^|[/\\])\.\.([/\\]|$)') { throw 'Invalid verification file.' }
        Assert-SoulLinkHash (Join-Path $PSScriptRoot $taskRelative) $taskProperty.Value
    }
    $taskHasher = [Security.Cryptography.SHA256]::Create()
    try { $taskFolderDigest = [BitConverter]::ToString($taskHasher.ComputeHash([Text.Encoding]::UTF8.GetBytes($PSScriptRoot.ToLowerInvariant()))).Replace('-','') }
    finally { $taskHasher.Dispose() }
    $taskLaunchMutex = New-Object Threading.Mutex($false, ('Local\SoulLink-' + $taskFolderDigest))
    try { $taskOwnMutex = $taskLaunchMutex.WaitOne(0) }
    catch [Threading.AbandonedMutexException] { $taskOwnMutex = $true }
    if (-not $taskOwnMutex) { throw 'The Soul Link launcher is already open in this folder. Return to its console.' }
    Clear-SoulLinkOrphanNetwork $PSScriptRoot
    if ($taskBootstrap) {
        . (Join-Path $PSScriptRoot 'bootstrap.ps1')
        Install-SoulLinkComponents $taskManifest -Stable:$Stable
    }
    if ($Controls) {
        $taskControlEmulator = Join-Path $PSScriptRoot 'emulator-soullink/mGBA.exe'
        if (Get-Process mGBA -ErrorAction SilentlyContinue | Where-Object { $_.Path -eq $taskControlEmulator }) {
            throw 'Close the game before changing controls.'
        }
        Write-Host 'Open Tools > Settings > Keyboard or Controllers, then close mGBA.'
        Write-Host 'This mode opens the emulator without the game or Soul Link script.'
        $taskControlGame = Start-SoulLinkEmulatorProcess @{FilePath=$taskControlEmulator; WorkingDirectory=$PSScriptRoot; PassThru=$true; RedirectStandardOutput=(Join-Path $PSScriptRoot 'touches.log'); RedirectStandardError=(Join-Path $PSScriptRoot 'touches-erreurs.log')}
        $taskControlGame.WaitForExit()
        Add-Content -LiteralPath (Join-Path $PSScriptRoot 'emulateur-diagnostic.log') -Encoding UTF8 -Value ("{0:o} mGBA closed without ROM/script; code={1}" -f (Get-Date), $taskControlGame.ExitCode)
        if ($taskControlGame.ExitCode -ne 0) { throw 'mGBA stopped while changing controls. Check touches-erreurs.log.' }
        return
    }
    $taskPublic = Get-SoulLinkPublicConfiguration $taskManifest.publicRelay -ConfigureAgain:$ConfigureAgain
    $taskConfig = $taskPublic.config
    $taskSecret = $taskPublic.secret
    $taskServer = $taskConfig.server
    $taskConfigFile = Join-Path $PSScriptRoot 'connexion.json'
    $taskSecretFile = Join-Path $PSScriptRoot 'connexion-secret.txt'
    $taskPython = Join-Path $PSScriptRoot 'runtime\python.exe'
    if ($taskConfig.role -eq 'A') {
        Write-SoulLinkJson (Join-Path $PSScriptRoot 'invitation-soul-link.json') @{version=2;server=$taskServer;port=8443;role='B';secret=$taskSecret}
        try { Copy-SoulLinkCode (Join-Path $PSScriptRoot 'invitation-soul-link.json') }
        catch { Write-Host 'Clipboard unavailable. Copy the room code shown above.' }
    }
    Write-Host 'Connecting to the Internet relay…'
    $taskConfig.server = $taskServer
    Write-SoulLinkJson $taskConfigFile $taskConfig
    [IO.File]::WriteAllText($taskSecretFile,$taskSecret,(New-Object Text.UTF8Encoding($false)))
    $taskArguments = @('-X','utf8',(Join-Path $PSScriptRoot 'network.py'),'--server',$taskServer,'--role',$taskConfig.role,'--secret-file',$taskSecretFile,'--ui-root',$PSScriptRoot)
    $taskArguments += @('--tls','--port','8443','--launcher-pid',[string]$PID)
    $taskCompanion = Start-Process -FilePath $taskPython -ArgumentList (($taskArguments | ForEach-Object {Quote-SoulLinkArgument $_}) -join ' ') -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $PSScriptRoot 'reseau.log') -RedirectStandardError (Join-Path $PSScriptRoot 'reseau-erreurs.log')
    for ($taskAttempt = 0; $taskAttempt -lt 150; $taskAttempt++) {
        if ($taskCompanion.HasExited) {
            $taskStartupError = Read-SoulLinkLiveLog (Join-Path $PSScriptRoot 'reseau-erreurs.log')
            if ($taskStartupError -match '10048|address already in use') { throw 'Port 8766 is occupied by another local session. Save and close any Soul Link game and its launcher, then retry. Check reseau-erreurs.log.' }
            throw ('The network companion cannot start. reseau-erreurs.log: ' + (($taskStartupError -split "`n" | Select-Object -Last 8) -join ' ').Trim())
        }
        $taskReadyLog = Read-SoulLinkLiveLog (Join-Path $PSScriptRoot 'reseau-erreurs.log')
        if ($taskReadyLog.Contains('Relay connected')) { break }
        Start-Sleep -Milliseconds 200
    }
    if (-not $taskReadyLog.Contains('Relay connected')) { throw 'The relay is not connected. Check your Internet connection. The room creator must keep their session open. Check reseau-erreurs.log.' }
    $taskEmulatorFolder = 'emulator-soullink'
    $taskEmulator = Join-Path $PSScriptRoot ($taskEmulatorFolder + '\mGBA.exe')
    # Keep an existing player's save usable after the ROM filename change.
    foreach ($taskSaveExtension in @('.sav', '.rtc')) {
        $taskPreviousSave = Join-Path $PSScriptRoot ('SoulLink-Internet' + $taskSaveExtension)
        $taskCurrentSave = Join-Path $PSScriptRoot ('lgfr-sl' + $taskSaveExtension)
        if ((Test-Path -LiteralPath $taskPreviousSave) -and -not (Test-Path -LiteralPath $taskCurrentSave)) {
            Copy-Item -LiteralPath $taskPreviousSave -Destination $taskCurrentSave
        }
    }
    $taskRom = Join-Path $PSScriptRoot 'lgfr-sl.gba'
    $taskScript = Join-Path $PSScriptRoot ('player-' + $taskConfig.role.ToLowerInvariant() + '.lua')
    if ($ManualEmulator) {
        . (Join-Path $PSScriptRoot 'manual-launch.ps1')
        Start-SoulLinkManualSession $taskCompanion $PSScriptRoot $taskEmulator $taskRom $taskScript
        return
    }
    $taskGameArgs = '-C ' + (Quote-SoulLinkArgument ('soulLinkScript=' + $taskScript)) + ' ' + (Quote-SoulLinkArgument $taskRom)
    Write-Host 'All set. The companion tracks captures and deaths, and lets you choose your team.'
    Write-Host ("Loading the script for player {0}…" -f $taskConfig.role)
    # This is the interactive game explicitly requested by the launcher user.
    $taskEmulatorOutput = Join-Path $PSScriptRoot 'emulateur.log'
    $taskEmulatorErrors = Join-Path $PSScriptRoot 'emulateur-erreurs.log'
    $taskDiagnostic = Join-Path $PSScriptRoot 'emulateur-diagnostic.log'
    Add-Content -LiteralPath $taskDiagnostic -Encoding UTF8 -Value ("{0:o} mGBA launch requested; SHA256={1}" -f (Get-Date), (Get-FileHash -LiteralPath $taskEmulator -Algorithm SHA256).Hash)
    $taskGame = Start-SoulLinkEmulatorProcess @{FilePath=$taskEmulator; ArgumentList=$taskGameArgs; WorkingDirectory=$PSScriptRoot; PassThru=$true; RedirectStandardOutput=$taskEmulatorOutput; RedirectStandardError=$taskEmulatorErrors}
    $taskScriptReady = $false
    for ($taskAttempt = 0; $taskAttempt -lt 100; $taskAttempt++) {
        if ($taskGame.HasExited) { throw 'The emulator stopped at startup. Check emulateur-erreurs.log.' }
        $taskScriptLog = Read-SoulLinkLiveLog $taskEmulatorErrors
        if ($taskScriptLog.Contains('SoulLink autoload: failed')) { throw 'The Soul Link script could not load. Check emulateur-erreurs.log.' }
        if ($taskScriptLog.Contains('SoulLink autoload: ready') -and $taskScriptLog.Contains('Soul Link Internet enabled')) { $taskScriptReady = $true; break }
        Start-Sleep -Milliseconds 200
    }
    if (-not $taskScriptReady) { throw 'Script loading was not confirmed. Check emulateur-erreurs.log.' }
    $taskGuiArguments = @('-NoProfile','-ExecutionPolicy','Bypass','-File',(Join-Path $PSScriptRoot 'companion.ps1'))
    Start-Process -FilePath 'powershell.exe' -ArgumentList (($taskGuiArguments | ForEach-Object {Quote-SoulLinkArgument $_}) -join ' ') -WorkingDirectory $PSScriptRoot -WindowStyle Hidden | Out-Null
    Write-Host ("Player {0} script loaded." -f $taskConfig.role)
    Write-Host 'Keep this console open while playing. Save in the game before closing the emulator.'
    while (-not $taskGame.HasExited) {
        if ($taskCompanion.HasExited) { throw 'The network companion stopped. Save and close the game, then run play.bat again.' }
        Start-Sleep -Seconds 1
    }
    $taskGame.WaitForExit()
    Add-Content -LiteralPath $taskDiagnostic -Encoding UTF8 -Value ("{0:o} mGBA closed; code={1}" -f (Get-Date), $taskGame.ExitCode)
    if ($taskGame.ExitCode -ne 0) { throw "mGBA stopped with code $($taskGame.ExitCode). Check emulateur-erreurs.log and emulateur-diagnostic.log." }
} catch {
    Write-Host ("Stopped: " + $_.Exception.Message) -ForegroundColor Red
    exit 1
} finally {
    if ($taskCompanion -and -not $taskCompanion.HasExited) { Stop-Process -Id $taskCompanion.Id -ErrorAction SilentlyContinue }
    if ($taskLaunchMutex) {
        if ($taskOwnMutex) { $taskLaunchMutex.ReleaseMutex() }
        $taskLaunchMutex.Dispose()
    }
}
