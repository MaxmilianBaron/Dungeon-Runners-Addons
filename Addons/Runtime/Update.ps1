param([string]$ClientDirectory = '', [switch]$Interactive)
$ErrorActionPreference = 'Stop'

function Get-UpdatePath([string]$Root,[string]$Relative) {
    if ([IO.Path]::IsPathRooted($Relative) -or $Relative -match '(^|[\\/])\.\.?([\\/]|$)' -or $Relative -match ':') { throw 'Invalid update path.' }
    $base = [IO.Path]::GetFullPath($Root).TrimEnd('\')
    $result = [IO.Path]::GetFullPath((Join-Path $base $Relative))
    if (-not $result.StartsWith($base+'\',[StringComparison]::OrdinalIgnoreCase)) { throw 'Update path is outside its folder.' }
    $cursor = $result
    while ($cursor.Length -ge $base.Length) {
        if ((Test-Path -LiteralPath $cursor) -and ((Get-Item -LiteralPath $cursor -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)) { throw 'Linked files or folders are not supported.' }
        $cursor = [IO.Path]::GetDirectoryName($cursor)
    }
    return $result
}

function Get-UpdateRelease {
    [Net.ServicePointManager]::SecurityProtocol = [Net.ServicePointManager]::SecurityProtocol -bor [Net.SecurityProtocolType]::Tls12
    $response = Invoke-WebRequest -UseBasicParsing -Uri 'https://api.github.com/repos/MaxmilianBaron/Dungeon-Runners-Addons/releases/latest' -Headers @{'User-Agent'='Dungeon-Runners-Addons';'Accept'='application/vnd.github+json';'X-GitHub-Api-Version'='2022-11-28'} -TimeoutSec 30
    if ($response.Content.Length -gt 1048576) { throw 'Release metadata is too large.' }
    return ($response.Content | ConvertFrom-Json)
}

function Receive-UpdateAsset($Release,[string]$Name,[string]$Destination,[string]$ExpectedHash = '') {
    if ($Name -notmatch '^[A-Za-z0-9_.-]{1,96}$') { throw 'Invalid release asset name.' }
    $assets = @($Release.assets | Where-Object name -eq $Name)
    if ($assets.Count -ne 1) { throw ('Missing or duplicate release asset: '+$Name) }
    $asset = $assets[0]
    $expectedUrl = 'https://github.com/MaxmilianBaron/Dungeon-Runners-Addons/releases/download/'+$Release.tag_name+'/'+$Name
    $limit = if ($Name -eq 'package.json') { 65536 } else { 16777216 }
    if ($asset.browser_download_url -cne $expectedUrl -or $asset.size -lt 1 -or $asset.size -gt $limit -or $asset.digest -notmatch '^sha256:[a-f0-9]{64}$') { throw ('Invalid release asset: '+$Name) }
    $digest = $asset.digest.Substring(7)
    if ($ExpectedHash -and $ExpectedHash -cne $digest) { throw ('Release checksum mismatch: '+$Name) }
    Invoke-WebRequest -UseBasicParsing -Uri $expectedUrl -OutFile $Destination -TimeoutSec 120 -MaximumRedirection 5 -Headers @{'User-Agent'='Dungeon-Runners-Addons'}
    $download = Get-Item -LiteralPath $Destination
    if ($download.Length -ne $asset.size -or (Get-FileHash -LiteralPath $Destination -Algorithm SHA256).Hash.ToLowerInvariant() -cne $digest) { throw ('Downloaded file failed verification: '+$Name) }
}

function Invoke-AddonUpdate([string]$Game) {
    if (Get-Process -Name DungeonRunners,DungeonRunners118 -ErrorAction SilentlyContinue) { throw 'Close Dungeon Runners before updating addons.' }
    $Game = [IO.Path]::GetFullPath($Game)
    $exe = Get-UpdatePath $Game 'DungeonRunners.exe'
    $loader = Get-UpdatePath $Game 'd3d9.dll'
    if (-not (Test-Path -LiteralPath $exe) -or -not (Test-Path -LiteralPath $loader)) { throw 'Select an existing Dungeon Runners Addons installation.' }
    $release = Get-UpdateRelease
    if ($release.draft -or $release.prerelease -or $release.tag_name -notmatch '^v[0-9][A-Za-z0-9._-]{0,63}$' -or @($release.assets).Count -gt 80) { throw 'Invalid release metadata.' }
    $temporaryBase = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\')
    $stage = Join-Path $temporaryBase ('Dungeon-Runners-Addons-'+[Guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $stage | Out-Null
    try {
        $manifestPath = Get-UpdatePath $stage 'package.json'
        Receive-UpdateAsset $release 'package.json' $manifestPath
        $manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
        if ($manifest.schema -ne 1 -or $manifest.version -cne $release.tag_name.Substring(1) -or $manifest.installerSha256 -notmatch '^[a-f0-9]{64}$' -or @($manifest.files).Count -lt 7 -or @($manifest.files).Count -gt 64) { throw 'Invalid update manifest.' }
        if ((Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash.ToLowerInvariant() -notin $manifest.clients) { throw 'This client version is not supported. No files were changed.' }
        if ((Get-FileHash -LiteralPath $loader -Algorithm SHA256).Hash.ToLowerInvariant() -notin (@($manifest.loaders) + @($manifest.chainLoaders))) { throw 'This d3d9.dll is not supported for coexistence. It was left untouched.' }
        $paths = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
        $assetNames = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
        foreach ($file in $manifest.files) {
            $known = $file.path -in @('d3d9.dll','Addons/Update.cmd','Addons/Runtime/Update.ps1','Addons/Runtime/Addons.dll','Addons/Runtime/ui-resources.json') -or $file.path -match '^Addons/[A-Za-z0-9_-]+/(?:addon\.ini|Addon\.dll)$'
            if (-not $known -or $file.sha256 -notmatch '^[a-f0-9]{64}$' -or $file.asset -notmatch '^[A-Za-z0-9_.-]{1,96}$' -or -not $paths.Add($file.path) -or -not $assetNames.Add($file.asset)) { throw 'Invalid update file entry.' }
            $null = Get-UpdatePath $Game $file.path
            $null = Get-UpdatePath $stage $file.path
        }
        foreach ($required in @('d3d9.dll','Addons/Runtime/Addons.dll','Addons/Runtime/ui-resources.json','Addons/DamageMeter/addon.ini','Addons/HideGoldLabels/addon.ini','Addons/Update.cmd','Addons/Runtime/Update.ps1')) {
            if (-not $paths.Contains($required)) { throw ('Missing update file: '+$required) }
        }
        $downloads = 0
        foreach ($file in $manifest.files) {
            $installed = Get-UpdatePath $Game $file.path
            $destination = Get-UpdatePath $stage $file.path
            New-Item -ItemType Directory -Path ([IO.Path]::GetDirectoryName($destination)) -Force | Out-Null
            if ((Test-Path -LiteralPath $installed) -and (Get-FileHash -LiteralPath $installed -Algorithm SHA256).Hash.ToLowerInvariant() -ceq $file.sha256) { Copy-Item -LiteralPath $installed -Destination $destination }
            else { Receive-UpdateAsset $release $file.asset $destination $file.sha256; $downloads++ }
        }
        $metadata = Get-Content -LiteralPath (Get-UpdatePath $stage 'Addons/Runtime/ui-resources.json') -Raw | ConvertFrom-Json
        $cache = Get-UpdatePath $Game 'Addons/Runtime/ui.bin'
        $cacheValid = (Test-Path -LiteralPath $cache) -and (Get-FileHash -LiteralPath $cache -Algorithm SHA256).Hash.ToLowerInvariant() -ceq $metadata.sha256
        $licensePath = Get-UpdatePath $Game 'Addons/Licenses/LICENSE.txt'
        $licensePresent = Test-Path -LiteralPath $licensePath -PathType Leaf
        $installer = Get-UpdatePath $stage 'Install.ps1'
        Receive-UpdateAsset $release 'Install.ps1' $installer $manifest.installerSha256
        . $installer
        $null = Install-Addons $Game $stage
        foreach ($file in $manifest.files) {
            if ((Get-FileHash -LiteralPath (Get-UpdatePath $Game $file.path) -Algorithm SHA256).Hash.ToLowerInvariant() -cne $file.sha256) { throw ('Installed file failed verification: '+$file.path) }
        }
        if ((Get-FileHash -LiteralPath $cache -Algorithm SHA256).Hash.ToLowerInvariant() -cne $metadata.sha256) { throw 'Installed UI cache failed verification.' }
        if (-not (Test-Path -LiteralPath $licensePath -PathType Leaf)) { throw 'The addon license is missing.' }
        if (-not $licensePresent -and -not $downloads -and $cacheValid) { return ('Updated '+$release.tag_name+'; added the addon license. Existing files were preserved.') }
        if (-not $downloads -and $cacheValid) { return ('Version '+$release.tag_name+' is already installed.') }
        return ('Updated to '+$release.tag_name+'; '+$downloads+' changed files downloaded. Settings and history were preserved.')
    } finally {
        if (Test-Path -LiteralPath $stage) {
            $resolved = (Resolve-Path -LiteralPath $stage).Path
            if ($resolved -eq $stage -and $resolved.StartsWith($temporaryBase+'\Dungeon-Runners-Addons-',[StringComparison]::OrdinalIgnoreCase) -and -not ((Get-Item -LiteralPath $resolved -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)) { Remove-Item -LiteralPath $resolved -Recurse -Force }
        }
    }
}

if ($MyInvocation.InvocationName -eq '.') { return }
try {
    if (-not $ClientDirectory) {
        $candidate = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
        if (Test-Path -LiteralPath (Join-Path $candidate 'DungeonRunners.exe')) { $ClientDirectory = $candidate }
    }
    if (-not $ClientDirectory -and $Interactive) {
        Add-Type -AssemblyName System.Windows.Forms
        $picker = New-Object System.Windows.Forms.FolderBrowserDialog
        $picker.Description = 'Select the Dungeon Runners folder containing DungeonRunners.exe.'
        $picker.ShowNewFolderButton = $false
        if ($picker.ShowDialog() -ne [System.Windows.Forms.DialogResult]::OK) { exit 0 }
        $ClientDirectory = $picker.SelectedPath
        $picker.Dispose()
    }
    if (-not $ClientDirectory) { throw 'Use Update.cmd or provide -ClientDirectory.' }
    $result = Invoke-AddonUpdate $ClientDirectory
    Write-Output $result
    if ($Interactive) { Add-Type -AssemblyName System.Windows.Forms; $null = [System.Windows.Forms.MessageBox]::Show($result,'Dungeon Runners Addons','OK','Information') }
} catch {
    if ($Interactive) { Add-Type -AssemblyName System.Windows.Forms; $null = [System.Windows.Forms.MessageBox]::Show($_.Exception.Message,'Update stopped','OK','Error') }
    Write-Error $_
    exit 1
}
