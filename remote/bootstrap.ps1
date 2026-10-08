# Pinned portable downloads only; no machine-wide installation.
function Get-SoulLinkDownload($Spec) {
    $taskCache = Join-Path $PSScriptRoot 'downloads'
    New-Item -ItemType Directory -Path $taskCache -Force | Out-Null
    if ($Spec.name -notmatch '^[A-Za-z0-9._-]+$' -or $Spec.url -notmatch '^https://') { throw 'Invalid download source.' }
    $taskArchive = Join-Path $taskCache $Spec.name
    if (Test-Path -LiteralPath $taskArchive) {
        Assert-SoulLinkHash $taskArchive $Spec.sha256
        return $taskArchive
    }
    Write-Host "Downloading $($Spec.label)..."
    [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
    $taskPartial = $taskArchive + '.partial'
    $taskProgressPreference = $ProgressPreference
    try {
        $ProgressPreference = 'SilentlyContinue'
        Invoke-WebRequest -UseBasicParsing -Uri $Spec.url -OutFile $taskPartial -TimeoutSec 300
        Assert-SoulLinkHash $taskPartial $Spec.sha256
        Move-Item -LiteralPath $taskPartial -Destination $taskArchive -Force
    } catch {
        throw "Download interrupted or invalid file ($($Spec.label)). Check your connection and run play.bat again. $($_.Exception.Message)"
    } finally { $ProgressPreference = $taskProgressPreference }
    return $taskArchive
}

function Install-SoulLinkComponents($Manifest, [switch]$Stable) {
    $taskMgbaComponent = if ($Stable -or $Manifest.downloads.PSObject.Properties.Name -notcontains 'mgba') { 'mgba_stable' } else { 'mgba' }
    foreach ($taskComponent in @('python',$taskMgbaComponent)) {
        $taskSpec = $Manifest.downloads.$taskComponent
        $taskReady = $true
        foreach ($taskProperty in $taskSpec.files.PSObject.Properties) {
            $taskFile = Join-Path $PSScriptRoot $taskProperty.Name
            if (-not (Test-Path -LiteralPath $taskFile)) { $taskReady = $false; break }
            if ((Get-FileHash -LiteralPath $taskFile -Algorithm SHA256).Hash.ToLowerInvariant() -ne $taskProperty.Value.ToLowerInvariant()) { $taskReady = $false; break }
        }
        if ($taskReady) { continue }
        $taskUpgradeLanguage = $false
        if ($taskComponent -eq 'mgba_stable') {
            $taskInstalledEmulator = Join-Path $PSScriptRoot ($taskSpec.destination + '/mGBA.exe')
            if (Get-Process mGBA -ErrorAction SilentlyContinue | Where-Object { $_.Path -eq $taskInstalledEmulator }) {
                throw 'Save in the game and close mGBA before updating the emulator.'
            }
            $taskSourceNotice = Join-Path $PSScriptRoot ($taskSpec.destination + '/SOURCE.txt')
            if (Test-Path -LiteralPath $taskSourceNotice) {
                $taskUpgradeLanguage = [IO.File]::ReadAllText($taskSourceNotice).Contains('mgba-soullink-0.10.5-1-source.zip')
            }
        }
        $taskArchive = Get-SoulLinkDownload $taskSpec
            # A fresh staging directory makes an interrupted extraction retryable.
            $taskStaging = Join-Path $PSScriptRoot ('downloads/extract-' + [guid]::NewGuid().ToString('N'))
            New-Item -ItemType Directory -Path $taskStaging | Out-Null
            if ($taskComponent -eq 'python') {
                Expand-Archive -LiteralPath $taskArchive -DestinationPath $taskStaging
                $taskSource = $taskStaging
                $taskDestination = Join-Path $PSScriptRoot 'runtime'
            } else {
                Expand-Archive -LiteralPath $taskArchive -DestinationPath $taskStaging
                $taskSource = Join-Path $taskStaging $taskSpec.root
                $taskDestination = Join-Path $PSScriptRoot $taskSpec.destination
            }
            New-Item -ItemType Directory -Path $taskDestination -Force | Out-Null
            Get-ChildItem -LiteralPath $taskSource -Force | Copy-Item -Destination $taskDestination -Recurse -Force
        if ($taskComponent -eq 'mgba_stable') {
            # Keep the player's keyboard/video settings when moving from stock stable.
            foreach ($taskSetting in @('config.ini','qt.ini')) {
                $taskOldSetting = Join-Path $PSScriptRoot ('emulator-stable/' + $taskSetting)
                $taskNewSetting = Join-Path $PSScriptRoot ($taskSpec.destination + '/' + $taskSetting)
                if ((Test-Path -LiteralPath $taskOldSetting) -and -not (Test-Path -LiteralPath $taskNewSetting)) {
                    Copy-Item -LiteralPath $taskOldSetting -Destination $taskNewSetting
                }
            }
        }
        if ($taskComponent -eq 'mgba_stable') {
            $taskPreferences = Join-Path $PSScriptRoot ($taskSpec.destination + '/qt.ini')
            $taskPreferenceText = if (Test-Path -LiteralPath $taskPreferences) { [IO.File]::ReadAllText($taskPreferences) } else { '' }
            if ($taskUpgradeLanguage -or $taskPreferenceText -notmatch '(?m)^language=') {
                if ($taskPreferenceText -match '(?m)^language=') {
                    $taskPreferenceText = [regex]::Replace($taskPreferenceText, '(?m)^language=.*$', 'language=en')
                } elseif ($taskPreferenceText -match '(?m)^\[General\]') {
                    $taskPreferenceText = [regex]::Replace($taskPreferenceText, '(?m)^\[General\][^\S\n]*', "[General]`r`nlanguage=en")
                } else {
                    $taskPreferenceText = "[General]`r`nlanguage=en`r`n" + $taskPreferenceText
                }
                [IO.File]::WriteAllText($taskPreferences,$taskPreferenceText,(New-Object Text.UTF8Encoding($false)))
            }
        }
        foreach ($taskProperty in $taskSpec.files.PSObject.Properties) {
            Assert-SoulLinkHash (Join-Path $PSScriptRoot $taskProperty.Name) $taskProperty.Value
        }
        $taskExtractedPath = (Resolve-Path -LiteralPath $taskStaging).Path
        $taskCachePath = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot 'downloads')).Path
        if (-not $taskExtractedPath.StartsWith($taskCachePath + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) { throw 'Invalid temporary folder.' }
        Remove-Item -LiteralPath $taskExtractedPath -Recurse -Force
    }
    # Initialize English once for the web release, without changing saved controls.
    $taskLanguageMarker=Join-Path $PSScriptRoot 'emulator-soullink/soullink-english.txt'
    $taskPreferences=Join-Path $PSScriptRoot 'emulator-soullink/qt.ini'
    if ((Test-Path -LiteralPath (Split-Path -Parent $taskLanguageMarker)) -and -not (Test-Path -LiteralPath $taskLanguageMarker)) {
        $taskText=if(Test-Path -LiteralPath $taskPreferences){[IO.File]::ReadAllText($taskPreferences)}else{''}
        if($taskText -match '(?m)^language='){$taskText=[regex]::Replace($taskText,'(?m)^language=.*$','language=en')}
        elseif($taskText -match '(?m)^\[General\]'){$taskText=[regex]::Replace($taskText,'(?m)^\[General\][^\S\n]*',"[General]`r`nlanguage=en")}
        else{$taskText="[General]`r`nlanguage=en`r`n"+$taskText}
        [IO.File]::WriteAllText($taskPreferences,$taskText,(New-Object Text.UTF8Encoding($false)))
        [IO.File]::WriteAllText($taskLanguageMarker,'1',(New-Object Text.UTF8Encoding($false)))
    }
}
