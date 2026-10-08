function Assert-SoulLinkPublicHost([string]$HostName) {
    if ($HostName.Length -gt 253 -or $HostName -notmatch '^(?:[a-zA-Z0-9](?:[a-zA-Z0-9-]{0,61}[a-zA-Z0-9])?\.)+[a-zA-Z]{2,63}$') {
        throw 'Invalid Internet relay hostname.'
    }
    return $HostName.ToLowerInvariant()
}
function ConvertFrom-SoulLinkPublicInvitation($Invitation) {
    if ($Invitation.version -ne 2 -or $Invitation.role -ne 'B' -or $Invitation.port -ne 8443 -or $Invitation.secret -cnotmatch '^[0-9a-f]{48}$') {
        throw 'Invalid Internet room code.'
    }
    return [pscustomobject]@{version=2; server=(Assert-SoulLinkPublicHost $Invitation.server); port=8443; role='B'; secret=$Invitation.secret}
}
function ConvertTo-SoulLinkPublicCode($Invitation) {
    $taskValid = ConvertFrom-SoulLinkPublicInvitation $Invitation
    $taskJson = $taskValid | ConvertTo-Json -Compress
    return 'SL2:' + [Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes($taskJson))
}
function ConvertFrom-SoulLinkPublicCode([string]$Code) {
    $Code = $Code.Trim()
    if ($Code.Length -gt 4096 -or $Code -notmatch '^SL2:[A-Za-z0-9+/]+={0,2}$') {
        throw 'Copy the whole room code, including SL2:.'
    }
    try { $taskJson = [Text.Encoding]::UTF8.GetString([Convert]::FromBase64String($Code.Substring(4))) | ConvertFrom-Json }
    catch { throw 'The room code is incomplete or invalid.' }
    return ConvertFrom-SoulLinkPublicInvitation $taskJson
}
function Get-SoulLinkPublicConfiguration([string]$Endpoint, [switch]$ConfigureAgain) {
    $taskEndpoint = Assert-SoulLinkPublicHost $Endpoint
    $taskConfigPath = Join-Path $PSScriptRoot 'connexion.json'
    $taskSecretPath = Join-Path $PSScriptRoot 'connexion-secret.txt'
    if ((Test-Path -LiteralPath $taskConfigPath) -and (Test-Path -LiteralPath $taskSecretPath) -and -not $ConfigureAgain) {
        $taskOld = [IO.File]::ReadAllText($taskConfigPath) | ConvertFrom-Json
        if ($taskOld.PSObject.Properties.Name -contains 'transport' -and $taskOld.transport -eq 'tls') {
            $taskSecret = [IO.File]::ReadAllText($taskSecretPath).Trim()
            if ($taskOld.server -ne $taskEndpoint -or $taskOld.role -notin @('A','B') -or $taskOld.port -ne 8443 -or $taskSecret -cnotmatch '^[0-9a-f]{48}$') {
                throw 'Invalid configuration. Run play.bat -ConfigureAgain.'
            }
            return [pscustomobject]@{config=$taskOld;secret=$taskSecret}
        }
    }
    Write-Host '1 — Create a room'
    Write-Host '2 — Join a room'
    $taskChoice = (Read-Host 'Choose 1 or 2').Trim()
    if ($taskChoice -eq '1') {
        $taskBytes = New-Object byte[] 24
        $taskGenerator = [Security.Cryptography.RandomNumberGenerator]::Create()
        try { $taskGenerator.GetBytes($taskBytes) } finally { $taskGenerator.Dispose() }
        $taskSecret = ([BitConverter]::ToString($taskBytes)).Replace('-','').ToLowerInvariant()
        $taskRole = 'A'
    } elseif ($taskChoice -eq '2') {
        $taskInvite = ConvertFrom-SoulLinkPublicCode (Read-Host 'Paste the room code from your partner')
        if ($taskInvite.server -ne $taskEndpoint) { throw 'This code uses another relay. Use the same package as your partner.' }
        $taskSecret = $taskInvite.secret
        $taskRole = 'B'
    } else { throw 'Choose 1 or 2.' }
    $taskConfig = [pscustomobject]@{role=$taskRole;server=$taskEndpoint;port=8443;hosting=$false;transport='tls'}
    return [pscustomobject]@{config=$taskConfig;secret=$taskSecret}
}
