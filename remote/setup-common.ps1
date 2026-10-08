Set-StrictMode -Version Latest
function Clear-SoulLinkOrphanNetwork([string]$Root) {
    $taskPythonPath = Join-Path $Root 'runtime/python.exe'
    $taskNetworkPath = Join-Path $Root 'network.py'
    $taskEmulatorPaths = @((Join-Path $Root 'emulator-soullink/mGBA.exe'), (Join-Path $Root 'emulator-official/mGBA.exe'))
    $taskGames = @(Get-Process mGBA -ErrorAction SilentlyContinue)
    if ($taskGames | Where-Object { $_.Path -in $taskEmulatorPaths }) {
        throw 'A game is already open in this folder. Return to it, or save and close mGBA before relaunching.'
    }
    foreach ($taskProcess in @(Get-CimInstance Win32_Process -Filter "Name = 'python.exe'")) {
        if (-not $taskProcess.ExecutablePath -or -not $taskProcess.CommandLine) { continue }
        $taskProcessRoot = $Root
        if ($taskProcess.ExecutablePath -ne $taskPythonPath) {
            # Only consider another verified Soul Link kit owning our bridge port.
            if ((Split-Path -Leaf (Split-Path -Parent $taskProcess.ExecutablePath)) -ne 'runtime') { continue }
            $taskProcessRoot = Split-Path -Parent (Split-Path -Parent $taskProcess.ExecutablePath)
            $taskOtherNetwork = Join-Path $taskProcessRoot 'network.py'
            if ($taskProcess.CommandLine.IndexOf($taskOtherNetwork, [StringComparison]::OrdinalIgnoreCase) -lt 0) { continue }
            try {
                $taskOtherManifest = Get-Content -LiteralPath (Join-Path $taskProcessRoot 'installation-manifest.json') -Raw -ErrorAction Stop | ConvertFrom-Json
                $taskOtherRegistration = Get-Content -LiteralPath (Join-Path $taskProcessRoot 'compagnon-web.json') -Raw -ErrorAction Stop | ConvertFrom-Json
                if ($taskOtherManifest.version -ne 2 -or $taskOtherManifest.publicRelay -ne 'gbfbc.com' -or $taskOtherRegistration.pid -ne $taskProcess.ProcessId) { continue }
                Assert-SoulLinkHash $taskOtherNetwork $taskOtherManifest.packaged.'network.py'
                Assert-SoulLinkHash $taskProcess.ExecutablePath $taskOtherManifest.files.'runtime/python.exe'
                $taskOwners = @(Get-NetTCPConnection -LocalPort 8766 -State Listen -ErrorAction Stop)
                if ($taskProcess.ProcessId -notin $taskOwners.OwningProcess) { continue }
            } catch { continue }
        } elseif ($taskProcess.CommandLine.IndexOf($taskNetworkPath, [StringComparison]::OrdinalIgnoreCase) -lt 0) { continue }
        $taskOtherGames = @((Join-Path $taskProcessRoot 'emulator-soullink/mGBA.exe'), (Join-Path $taskProcessRoot 'emulator-official/mGBA.exe'))
        if ($taskGames | Where-Object { $_.Path -in $taskOtherGames }) { throw 'Another Soul Link game is using the local connection. Save and close it before switching kits.' }
        $taskParent = Get-CimInstance Win32_Process -Filter ('ProcessId = ' + $taskProcess.ParentProcessId)
        if ($taskParent -and $taskParent.CreationDate -le $taskProcess.CreationDate) {
            throw 'Another launcher is already open in this folder. Return to its console or close it before relaunching.'
        }
        Stop-Process -Id $taskProcess.ProcessId -ErrorAction Stop
        Write-Host 'An abandoned network session was closed automatically.'
    }
}
function Read-SoulLinkLiveLog([string]$Path) {
    # Start-Process keeps redirected output open for writing. The reader must
    # permit that existing writer as well as log rotation/deletion.
    $taskStream = $null
    $taskReader = $null
    try {
        $taskStream = [IO.File]::Open($Path, [IO.FileMode]::Open, [IO.FileAccess]::Read,
            ([IO.FileShare]::ReadWrite -bor [IO.FileShare]::Delete))
        $taskReader = New-Object IO.StreamReader($taskStream)
        return $taskReader.ReadToEnd()
    } catch [IO.IOException] {
        # Missing, rotating or temporarily exclusive log: retry on next poll.
        return ''
    } finally {
        if ($taskReader) { $taskReader.Dispose() }
        elseif ($taskStream) { $taskStream.Dispose() }
    }
}
function Copy-SoulLinkCode([string]$InvitationPath) {
    if (-not (Test-Path -LiteralPath $InvitationPath)) { throw 'Run play.bat and choose Create before copying the code.' }
    if ((Get-Item -LiteralPath $InvitationPath).Length -gt 4096) { throw 'Invitation is too large.' }
    $taskJson = [IO.File]::ReadAllText($InvitationPath) | ConvertFrom-Json
    $taskRoomCode = ConvertTo-SoulLinkPublicCode $taskJson
    Write-Host ''
    Write-Host 'Your room code — send it to your partner:' -ForegroundColor Cyan
    Write-Host $taskRoomCode
    Write-Host ''
    Set-Clipboard -Value $taskRoomCode
    Write-Host 'The code is also copied to the clipboard.' -ForegroundColor Green
}
function Write-SoulLinkJson([string]$Path, $Value) {
    [IO.File]::WriteAllText($Path, ($Value | ConvertTo-Json -Depth 4), (New-Object Text.UTF8Encoding($false)))
}
function Assert-SoulLinkHash([string]$Path, [string]$Expected) {
    if ((Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash -ne $Expected) { throw "Incomplete or modified file: $Path. Extract the package again." }
}
function Quote-SoulLinkArgument([string]$Value) {
    # Windows command-line quoting; no shell interpolation or Invoke-Expression.
    return '"' + [regex]::Replace([regex]::Replace($Value, '(\\*)"', '$1$1\"'), '(\\+)$', '$1$1') + '"'
}
function Start-SoulLinkEmulatorProcess([hashtable]$Parameters) {
    try { return Start-Process @Parameters -ErrorAction Stop }
    catch {
        $taskLaunchError = $_
        $taskNativeCode = $null
        $taskException = $_.Exception
        while ($taskException) {
            if ($taskException -is [ComponentModel.Win32Exception]) { $taskNativeCode = $taskException.NativeErrorCode }
            $taskException = $taskException.InnerException
        }
        $taskReport = New-Object 'Collections.Generic.List[string]'
        $taskReport.Add(('Time: {0:o}' -f (Get-Date)))
        $taskReport.Add('Executable: ' + $Parameters.FilePath)
        $taskReport.Add('Windows error code: ' + $taskNativeCode)
        $taskReport.Add('Error: ' + $taskLaunchError.Exception.Message)
        try {
            $taskSignature = Get-AuthenticodeSignature -LiteralPath $Parameters.FilePath -ErrorAction Stop
            $taskReport.Add('Executable signature: ' + $taskSignature.Status)
        } catch { $taskReport.Add('Executable signature: unavailable') }
        $taskFolderName = Split-Path -Leaf (Split-Path -Parent $Parameters.FilePath)
        try {
            $taskEvents = Get-WinEvent -FilterHashtable @{LogName='Microsoft-Windows-CodeIntegrity/Operational'; Id=3033,3077; StartTime=(Get-Date).AddMinutes(-2)} -MaxEvents 30 -ErrorAction Stop
            foreach ($taskEvent in $taskEvents) {
                if ($taskEvent.Message -and $taskEvent.Message.IndexOf($taskFolderName, [StringComparison]::OrdinalIgnoreCase) -ge 0) {
                    $taskReport.Add(('Code Integrity event {0}: {1}' -f $taskEvent.Id, $taskEvent.Message))
                }
            }
        } catch { $taskReport.Add('Code Integrity events: unavailable or no recent matching event') }
        $taskReportPath = Join-Path $Parameters.WorkingDirectory 'launch-diagnostic.txt'
        try { [IO.File]::WriteAllLines($taskReportPath, $taskReport, (New-Object Text.UTF8Encoding($false))) } catch {}
        if ($taskNativeCode -in @(577,1260,4551)) {
            throw 'Windows application control blocked mGBA or one of its DLLs. Check launch-diagnostic.txt and Windows Security > App & browser control. Share the diagnostic with the package maintainer.'
        }
        throw ('mGBA could not start. Check launch-diagnostic.txt. ' + $taskLaunchError.Exception.Message)
    }
}
