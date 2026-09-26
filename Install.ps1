param([string]$ClientDirectory = '', [switch]$Interactive)
$ErrorActionPreference = 'Stop'

function Get-AddonLicenseBytes {
    $text = @'
MIT License

Copyright (c) 2026 MaxmilianBaron

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
'@
    return [Text.Encoding]::UTF8.GetBytes($text.Replace("`r",'')+"`n")
}

function Get-BytesHash([byte[]]$Bytes) {
    $algorithm = [Security.Cryptography.SHA256]::Create()
    try { return [BitConverter]::ToString($algorithm.ComputeHash($Bytes)).Replace('-','').ToLowerInvariant() }
    finally { $algorithm.Dispose() }
}

function Get-ChildPath([string]$Root, [string]$Relative) {
    if ([IO.Path]::IsPathRooted($Relative) -or $Relative -match '(^|[\\/])\.\.?([\\/]|$)' -or $Relative -match ':') { throw 'Invalid package path.' }
    $base = [IO.Path]::GetFullPath($Root).TrimEnd('\') + '\'
    $result = [IO.Path]::GetFullPath((Join-Path $base $Relative))
    if (-not $result.StartsWith($base,[StringComparison]::OrdinalIgnoreCase)) { throw 'Package path is outside its folder.' }
    $cursor = $result
    while ($cursor.Length -gt $base.Length) {
        if ((Test-Path -LiteralPath $cursor) -and ((Get-Item -LiteralPath $cursor -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)) { throw 'Linked files or folders are not supported.' }
        $cursor = [IO.Path]::GetDirectoryName($cursor)
    }
    return $result
}

function Read-GameResource($Stream, $Entry) {
    if ($Entry.stored_size -lt 1 -or $Entry.stored_size -gt 2097152 -or $Entry.decoded_size -lt 1 -or $Entry.decoded_size -gt 2097152) { throw 'Invalid UI resource size.' }
    if ($Entry.package_offset -lt 0 -or $Entry.package_offset + $Entry.stored_size -gt $Stream.Length) { throw 'Incomplete game.pkg.' }
    $null = $Stream.Seek([long]$Entry.package_offset,[IO.SeekOrigin]::Begin)
    $stored = New-Object byte[] ([int]$Entry.stored_size)
    $read = 0
    while ($read -lt $stored.Length) {
        $count = $Stream.Read($stored,$read,$stored.Length-$read)
        if ($count -eq 0) { throw 'Incomplete game resource.' }
        $read += $count
    }
    if ((Get-BytesHash $stored) -ne $Entry.storedSha256) { throw ('Unsupported game resource: '+$Entry.name) }
    if ($Entry.flags -band 1) {
        if ($stored.Length -lt 6) { throw 'Invalid compressed resource.' }
        $input = [IO.MemoryStream]::new($stored,2,$stored.Length-6,$false)
        $inflater = [IO.Compression.DeflateStream]::new($input,[IO.Compression.CompressionMode]::Decompress)
        try {
            $decoded = New-Object byte[] ([int]$Entry.decoded_size)
            $read = 0
            while ($read -lt $decoded.Length) {
                $count = $inflater.Read($decoded,$read,$decoded.Length-$read)
                if ($count -eq 0) { throw 'Incomplete decompressed resource.' }
                $read += $count
            }
            if ($inflater.ReadByte() -ne -1) { throw 'Resource exceeds its expected size.' }
        } finally { $inflater.Dispose(); $input.Dispose() }
    } else { $decoded = $stored }
    if ($decoded.Length -ne $Entry.decoded_size -or (Get-BytesHash $decoded) -ne $Entry.decodedSha256) { throw 'Game UI resource verification failed.' }
    return ,$decoded
}

function New-LocalUiCache([string]$Game, $Metadata) {
    if ((Get-FileHash -LiteralPath (Join-Path $Game 'game.pki') -Algorithm SHA1).Hash.ToLowerInvariant() -ne $Metadata.pkiSha1) { throw 'This game data version is not supported.' }
    if (@($Metadata.entries).Count -ne 7) { throw 'Invalid resource manifest.' }
    $resources = @{}
    $stream = [IO.File]::OpenRead((Join-Path $Game 'game.pkg'))
    try {
        foreach ($entry in $Metadata.entries) {
            if ($resources.ContainsKey($entry.name)) { throw 'Duplicate resource.' }
            $resources[$entry.name] = Read-GameResource $stream $entry
        }
    } finally { $stream.Dispose() }
    $components = [Collections.Generic.List[byte[]]]::new()
    foreach ($name in @('InGameUI4','NewUI','Font_Outline')) {
        [byte[]]$raw = $resources[$name]
        $size = if ($name -eq 'InGameUI4') { 1024 } else { 512 }
        if ($raw.Length -lt 128+$size*$size -or [Text.Encoding]::ASCII.GetString($raw,0,4) -ne 'DDS ' -or [Text.Encoding]::ASCII.GetString($raw,84,4) -ne 'DXT3' -or [BitConverter]::ToInt32($raw,12) -ne $size -or [BitConverter]::ToInt32($raw,16) -ne $size) { throw 'Unsupported texture layout.' }
        $pixels = New-Object byte[] ($size*$size)
        [Array]::Copy($raw,128,$pixels,0,$pixels.Length)
        $components.Add($pixels)
    }
    [byte[]]$metrics = $resources['fonts\Font_Outline_Metrics']
    if ($metrics.Length -ne 512) { throw 'Unsupported font metrics.' }
    $advance = New-Object byte[] 256
    for ($i=0;$i -lt 256;$i++) { $advance[$i] = ([BitConverter]::ToUInt16($metrics,$i*2)+2) -band 255 }
    $components.Add($advance)
    $components.Add([byte[]]$resources['sylfaen'])
    $output = [IO.MemoryStream]::new()
    $writer = [IO.BinaryWriter]::new($output)
    try {
        $writer.Write([Text.Encoding]::ASCII.GetBytes('DRUI0001'))
        foreach ($component in $components) { $writer.Write([uint32]$component.Length) }
        foreach ($component in $components) { $writer.Write($component) }
        $writer.Flush()
        $bytes = $output.ToArray()
    } finally { $writer.Dispose(); $output.Dispose() }
    if ($bytes.Length -ne $Metadata.size -or (Get-BytesHash $bytes) -ne $Metadata.sha256) { throw 'The local UI cache does not match this release.' }
    return ,$bytes
}

function Get-CompletedAddonBackup([string]$Game,[string]$Name) {
    if ($Name -cnotmatch '^[0-9]{8}-[0-9]{6}-[a-f0-9]{8}$') { throw 'Unknown backup name.' }
    $backup = Get-ChildPath $Game ('Addons/Backups/'+$Name)
    $record = Get-ChildPath $backup 'installation.json'
    $stat = Get-Item -LiteralPath $record -Force
    if ($stat.PSIsContainer -or $stat.Length -gt 65536) { throw 'Unknown backup metadata.' }
    $metadata = Get-Content -LiteralPath $record -Raw | ConvertFrom-Json
    if ($metadata.version -isnot [string] -or $metadata.version -cnotmatch '^[0-9][A-Za-z0-9._-]{0,63}$' -or $metadata.files -isnot [array] -or $metadata.files.Count -gt 67) { throw 'Unknown backup file list.' }
    $allowed = @('Addons/Licenses/LICENSE.txt','d3d9.dll','d3d9.previous.dll','Addons/Update.cmd','Addons/Update.sh','Addons/Update.command',
        'Addons/Runtime/Addons.dll','Addons/Runtime/ui.bin','Addons/Runtime/ui-resources.json','Addons/Runtime/Update.ps1','Addons/Runtime/macOS.py',
        'Addons/DamageMeter/DamageMeter.dll','Addons/DamageMeter/Dear-ImGui-LICENSE.txt','Addons/DamageMeter/MinHook-LICENSE.txt')
    $expected = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
    $directories = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
    $seen = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    $null = $expected.Add('installation.json')
    foreach ($entry in $metadata.files) {
        if ($entry.backup -isnot [string] -or -not $entry.backup.StartsWith($backup+'\',[StringComparison]::OrdinalIgnoreCase) -or $entry.existed -isnot [bool]) { throw 'Unknown backup file.' }
        $relative = $entry.backup.Substring($backup.Length+1).Replace('\','/')
        if ($relative -cnotin $allowed -and $relative -cnotmatch '^Addons/[A-Za-z0-9_-]+/addon\.ini$') { throw 'Unknown backup target.' }
        if (-not $seen.Add($relative) -or (Get-ChildPath $Game $relative) -ine $entry.destination -or (Get-ChildPath $backup $relative) -ine $entry.backup) { throw 'Invalid backup path.' }
        if ($entry.existed) { $null = $expected.Add($relative) }
        $parts = $relative.Split('/')
        for ($i=1; $i -lt $parts.Length; $i++) { $null = $directories.Add(($parts[0..($i-1)] -join '/')) }
    }
    $files = [Collections.Generic.List[string]]::new()
    $folders = [Collections.Generic.List[string]]::new()
    $queue = [Collections.Generic.Stack[string]]::new()
    $queue.Push($backup)
    while ($queue.Count) {
        $folder = $queue.Pop()
        $folders.Add($folder)
        foreach ($item in Get-ChildItem -LiteralPath $folder -Force) {
            if ($files.Count+$folders.Count+$queue.Count -ge 256 -or ($item.Attributes -band [IO.FileAttributes]::ReparsePoint)) { throw 'Unknown backup content.' }
            $relative = $item.FullName.Substring($backup.Length+1).Replace('\','/')
            $null = Get-ChildPath $backup $relative
            if ($item.PSIsContainer) {
                if (-not $directories.Contains($relative)) { throw 'Unknown backup folder.' }
                $queue.Push($item.FullName)
            } else {
                if (-not $expected.Remove($relative) -or $item.Length -gt 16777216) { throw 'Unknown backup content.' }
                $files.Add($item.FullName)
            }
        }
    }
    if ($expected.Count) { throw 'Incomplete backup.' }
    return [pscustomobject]@{path=$backup; files=$files; folders=$folders}
}

function Remove-OldAddonBackups([string]$Game) {
    try {
        if ((Get-Item -LiteralPath $Game -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) { return }
        $root = Get-ChildPath $Game 'Addons/Backups'
        if (-not (Test-Path -LiteralPath $root -PathType Container)) { return }
        $entries = @(Get-ChildItem -LiteralPath $root -Force | Select-Object -First 1025)
        if ($entries.Count -gt 1024) { return }
        $candidates = [Collections.Generic.List[object]]::new()
        foreach ($entry in ($entries | Sort-Object Name -Descending)) {
            try { $candidates.Add((Get-CompletedAddonBackup $Game $entry.Name)) } catch { }
        }
        $kept = 0
        $failed = $false
        foreach ($candidate in $candidates) {
            if ($candidate.files.Count -gt 1) { $kept++; if ($kept -le 3) { continue } }
            try {
                $candidate = Get-CompletedAddonBackup $Game ([IO.Path]::GetFileName($candidate.path))
                foreach ($file in ($candidate.files | Sort-Object { [IO.Path]::GetFileName($_) -eq 'installation.json' })) {
                    $relative = $file.Substring($Game.TrimEnd('\').Length+1)
                    $checked = Get-ChildPath $Game $relative
                    if (-not $checked.StartsWith($root+'\',[StringComparison]::OrdinalIgnoreCase)) { throw 'Backup is outside its folder.' }
                    [IO.File]::Delete($checked)
                }
                for ($i=$candidate.folders.Count-1; $i -ge 0; $i--) {
                    $checked = Get-ChildPath $Game ($candidate.folders[$i].Substring($Game.TrimEnd('\').Length+1))
                    if (-not $checked.StartsWith($root+'\',[StringComparison]::OrdinalIgnoreCase)) { throw 'Backup is outside its folder.' }
                    [IO.Directory]::Delete($checked,$false)
                }
            } catch { $failed = $true }
        }
        if ($failed) { Write-Warning 'Some older addon backups could not be removed.' }
    } catch { }
}

function Install-Addons([string]$Game,[string]$Package) {
    $Game = [IO.Path]::GetFullPath($Game)
    $Package = [IO.Path]::GetFullPath($Package)
    if (Get-Process -Name DungeonRunners,DungeonRunners118 -ErrorAction SilentlyContinue) { throw 'Close Dungeon Runners before installing or updating addons.' }
    $manifest = Get-Content -LiteralPath (Join-Path $Package 'package.json') -Raw | ConvertFrom-Json
    if ($manifest.schema -ne 1 -or @($manifest.files).Count -lt 4 -or @($manifest.files).Count -gt 64) { throw 'Invalid release package.' }
    $exe = Get-ChildPath $Game 'DungeonRunners.exe'
    if (-not (Test-Path -LiteralPath $exe)) { throw 'Choose the folder containing DungeonRunners.exe.' }
    $clientHash = (Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($clientHash -notin $manifest.clients) { throw 'This client version is not supported. No files were changed.' }
    $licenseRelative = 'Addons/Licenses/LICENSE.txt'
    $licensePath = Get-ChildPath $Game $licenseRelative
    if ((Test-Path -LiteralPath $licensePath) -and -not (Test-Path -LiteralPath $licensePath -PathType Leaf)) { throw 'The addon license target is a directory.' }
    $paths = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    foreach ($file in $manifest.files) {
        if ($file.path -eq $licenseRelative) { throw 'The addon license is managed separately from package files.' }
        if ($file.path -notin @('d3d9.dll','Addons/Update.cmd','Addons/Runtime/Update.ps1') -and $file.path -notmatch '^Addons/[A-Za-z0-9_-]+/(?:[A-Za-z0-9_.-]+\.(?:dll|txt|md|json)|addon\.ini)$') { throw 'The package contains an unsupported installation target.' }
        if (-not $paths.Add($file.path)) { throw 'Duplicate package file.' }
        $source = Get-ChildPath $Package $file.path
        $null = Get-ChildPath $Game $file.path
        if ((Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash.ToLowerInvariant() -ne $file.sha256) { throw ('Damaged release package: '+$file.path) }
    }
    foreach ($required in @('d3d9.dll','Addons/Runtime/Addons.dll','Addons/Runtime/ui-resources.json','Addons/DamageMeter/addon.ini','Addons/HideGoldLabels/addon.ini','Addons/Update.cmd','Addons/Runtime/Update.ps1')) {
        if (-not $paths.Contains($required)) { throw ('Missing release file: '+$required) }
    }
    $loader = Get-ChildPath $Game 'd3d9.dll'
    $preserved = Get-ChildPath $Game 'd3d9.previous.dll'
    $chainLoaders = @($manifest.chainLoaders | Where-Object { $_ })
    if ($chainLoaders.Count -gt 64 -or @($chainLoaders | Where-Object { $_ -notmatch '^[a-f0-9]{64}$' }).Count) { throw 'Invalid graphics compatibility list.' }
    $localFiles = @{}
    $loaderHash = if (Test-Path -LiteralPath $loader) { (Get-FileHash -LiteralPath $loader).Hash.ToLowerInvariant() } else { '' }
    if ($loaderHash -in $chainLoaders) {
        if ((Get-Item -LiteralPath $loader).Length -gt 16777216) { throw 'The existing d3d9.dll is too large.' }
        $localFiles['d3d9.previous.dll'] = [IO.File]::ReadAllBytes($loader)
        if ((Get-BytesHash $localFiles['d3d9.previous.dll']) -ne $loaderHash) { throw 'The existing d3d9.dll changed during installation.' }
    } elseif ($loaderHash -and $loaderHash -notin $manifest.loaders) {
        throw 'This d3d9.dll is not supported for coexistence. It was left untouched.'
    }
    if (Test-Path -LiteralPath $preserved) {
        $preservedHash = (Get-FileHash -LiteralPath $preserved).Hash.ToLowerInvariant()
        if ($preservedHash -notin $chainLoaders -or ($localFiles.ContainsKey('d3d9.previous.dll') -and $preservedHash -ne $loaderHash)) { throw 'A different d3d9.previous.dll already exists. Both libraries were left untouched.' }
    }
    $metadata = Get-Content -LiteralPath (Join-Path $Package 'Addons\Runtime\ui-resources.json') -Raw | ConvertFrom-Json
    $ui = New-LocalUiCache $Game $metadata
    $localFiles['Addons/Runtime/ui.bin'] = $ui
    if (-not (Test-Path -LiteralPath $licensePath)) { $localFiles[$licenseRelative] = Get-AddonLicenseBytes }
    $identity = [DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss')+'-'+[Guid]::NewGuid().ToString('N').Substring(0,8)
    $backupRoot = Get-ChildPath $Game ('Addons\Backups\'+$identity)
    $pendingRoot = Get-ChildPath $Game ('Addons\Runtime\.install-'+$identity)
    $files = @($manifest.files | Where-Object path -ne 'd3d9.dll') + @($localFiles.Keys | ForEach-Object { [pscustomobject]@{path=$_; sha256=(Get-BytesHash $localFiles[$_])} }) + @($manifest.files | Where-Object path -eq 'd3d9.dll')
    $needed = @($files | Where-Object {
        $destination = Get-ChildPath $Game $_.path
        -not (Test-Path -LiteralPath $destination) -or (Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash.ToLowerInvariant() -ne $_.sha256
    })
    if (-not $needed.Count) {
        Remove-OldAddonBackups $Game
        return ('Version V'+$manifest.version+' is already installed.')
    }
    New-Item -ItemType Directory -Path $backupRoot,$pendingRoot -Force | Out-Null
    $changed = [Collections.Generic.List[object]]::new()
    try {
        foreach ($file in $files) {
            $destination = Get-ChildPath $Game $file.path
            $expected = $file.sha256
            if ($file.path -eq $licenseRelative -and (Test-Path -LiteralPath $destination)) {
                if (-not (Test-Path -LiteralPath $destination -PathType Leaf)) { throw 'The addon license target is a directory.' }
                continue
            }
            if ((Test-Path -LiteralPath $destination) -and (Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash.ToLowerInvariant() -eq $expected) { continue }
            $pending = Get-ChildPath $pendingRoot $file.path
            $backup = Get-ChildPath $backupRoot $file.path
            New-Item -ItemType Directory -Path ([IO.Path]::GetDirectoryName($destination)),([IO.Path]::GetDirectoryName($pending)) -Force | Out-Null
            if ($localFiles.ContainsKey($file.path)) { [IO.File]::WriteAllBytes($pending,$localFiles[$file.path]) }
            else { Copy-Item -LiteralPath (Get-ChildPath $Package $file.path) -Destination $pending }
            if ((Get-FileHash -LiteralPath $pending).Hash.ToLowerInvariant() -ne $expected) { throw 'An installation file changed while being staged.' }
            if (Get-Process -Name DungeonRunners,DungeonRunners118 -ErrorAction SilentlyContinue) { throw 'Close Dungeon Runners before installing or updating addons.' }
            if ($file.path -eq 'd3d9.dll' -and $loaderHash -and (Get-FileHash -LiteralPath $loader).Hash.ToLowerInvariant() -ne $loaderHash) { throw 'The existing d3d9.dll changed during installation.' }
            $existed = Test-Path -LiteralPath $destination
            if ($existed -and $file.path -eq $licenseRelative) {
                $null = Get-ChildPath $Game $licenseRelative
                if (-not (Test-Path -LiteralPath $destination -PathType Leaf)) { throw 'The addon license target is a directory.' }
                continue
            }
            if ($existed) {
                New-Item -ItemType Directory -Path ([IO.Path]::GetDirectoryName($backup)) -Force | Out-Null
                [IO.File]::Replace($pending,$destination,$backup)
            } else { [IO.File]::Move($pending,$destination) }
            $changed.Add([pscustomobject]@{destination=$destination; backup=$backup; existed=$existed})
        }
        foreach ($file in $files) {
            if ($file.path -eq $licenseRelative -and -not @($changed | Where-Object destination -eq $licensePath).Count) {
                if (-not (Test-Path -LiteralPath (Get-ChildPath $Game $licenseRelative) -PathType Leaf)) { throw 'The addon license is missing.' }
                continue
            }
            if ((Get-FileHash -LiteralPath (Get-ChildPath $Game $file.path)).Hash.ToLowerInvariant() -ne $file.sha256) { throw 'Installed file verification failed.' }
        }
        [pscustomobject]@{version=$manifest.version; installedUtc=[DateTime]::UtcNow.ToString('o'); files=$changed; clientSha256=$clientHash} | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $backupRoot 'installation.json') -Encoding UTF8
    } catch {
        $failure = $_
        $restoreFailed = $false
        for ($i=$changed.Count-1;$i -ge 0;$i--) {
            $item = $changed[$i]
            try {
                if ($item.existed) { Copy-Item -LiteralPath $item.backup -Destination $item.destination -Force }
                elseif (Test-Path -LiteralPath $item.destination) { Remove-Item -LiteralPath $item.destination }
            } catch { $restoreFailed = $true }
        }
        if ($restoreFailed) { throw 'Installation stopped. Some files could not be restored; backups are in Addons/Backups.' }
        throw $failure
    } finally {
        if (Test-Path -LiteralPath $pendingRoot) {
            $resolved = (Resolve-Path -LiteralPath $pendingRoot).Path
            $allowed = [IO.Path]::GetFullPath((Join-Path $Game 'Addons\Runtime')).TrimEnd('\')+'\'
            if ($resolved -eq $pendingRoot -and $resolved.StartsWith($allowed,[StringComparison]::OrdinalIgnoreCase)) {
                Remove-Item -LiteralPath $resolved -Recurse -Force -ErrorAction SilentlyContinue
            }
        }
    }
    Remove-OldAddonBackups $Game
    return ('Installed V'+$manifest.version+'. Start the game normally, then open ESC > Addons. Existing settings and history were preserved.')
}

if ($MyInvocation.InvocationName -eq '.') { return }
try {
    if ($Interactive) {
        Add-Type -AssemblyName System.Windows.Forms
        $picker = New-Object System.Windows.Forms.FolderBrowserDialog
        $picker.Description = 'Select the Dungeon Runners folder containing DungeonRunners.exe.'
        $picker.ShowNewFolderButton = $false
        if ($ClientDirectory) { $picker.SelectedPath = $ClientDirectory }
        if ($picker.ShowDialog() -ne [System.Windows.Forms.DialogResult]::OK) { exit 0 }
        $ClientDirectory = $picker.SelectedPath
        $picker.Dispose()
    }
    if (-not $ClientDirectory) { throw 'Use Install.cmd or provide -ClientDirectory.' }
    $result = Install-Addons $ClientDirectory $PSScriptRoot
    Write-Output $result
    if ($Interactive) { $null = [System.Windows.Forms.MessageBox]::Show($result,'Dungeon Runners Addons','OK','Information') }
} catch {
    if ($Interactive) { $null = [System.Windows.Forms.MessageBox]::Show($_.Exception.Message,'Installation stopped','OK','Error') }
    Write-Error $_
    exit 1
}
