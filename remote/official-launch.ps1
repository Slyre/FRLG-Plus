function Wait-SoulLinkOfficialScript($Game, $Network, [string]$Root, [double]$Started, [int]$TimeoutSeconds = 300) {
    $taskDeadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    while ([DateTime]::UtcNow -lt $taskDeadline) {
        if ($Game.HasExited) { throw 'mGBA closed before the Soul Link script connected.' }
        if ($Network.HasExited) { throw 'The network companion stopped. Check reseau-erreurs.log.' }
        try {
            $taskView = Read-SoulLinkLiveLog (Join-Path $Root 'compagnon-etat.json') | ConvertFrom-Json
            $taskNow = ([DateTime]::UtcNow - [DateTime]'1970-01-01').TotalSeconds
            if ($taskView.local -and $taskView.updated -ge $Started -and $taskNow - $taskView.updated -lt 8) { return }
        } catch {
            # The file may not exist yet or may be replaced during the read.
        }
        Start-Sleep -Milliseconds 200
    }
    throw 'The Soul Link script has not connected. In mGBA, open Tools > Scripting, then File > Load script and choose soul-link.lua from this folder.'
}
