param([Parameter(Mandatory=$true)][string]$ClientDirectory, [string]$OutputDirectory = '')
$ErrorActionPreference = 'Stop'
$repositoryDirectory = Split-Path -Parent $PSScriptRoot
$sourceDirectory = Join-Path $repositoryDirectory 'Source'
$ClientDirectory = (Resolve-Path -LiteralPath $ClientDirectory).ProviderPath
foreach ($file in @('game.pki', 'game.pkg')) {
    if (-not (Test-Path -LiteralPath (Join-Path $ClientDirectory $file) -PathType Leaf)) {
        throw "Missing $file in ClientDirectory."
    }
}
$python = (Get-Command python.exe -CommandType Application -ErrorAction Stop | Select-Object -First 1).Source
& $python -c 'import sys; sys.exit(0 if sys.version_info >= (3, 8) else 1)'
if ($LASTEXITCODE -ne 0) { throw 'Python 3.8 or later is required on PATH.' }
if (-not $OutputDirectory) { $OutputDirectory = Join-Path $repositoryDirectory '.build\native' }
$OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)
$clientPrefix = $ClientDirectory.TrimEnd('\') + '\'
if (($OutputDirectory.TrimEnd('\') + '\').StartsWith($clientPrefix, [StringComparison]::OrdinalIgnoreCase)) {
    throw 'OutputDirectory must be outside the game directory.'
}
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path -LiteralPath $vswhere)) { throw 'Install Visual Studio 2022 Build Tools with Desktop development with C++.' }
$installation = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $installation) { throw 'The Visual C++ x86 compiler is not installed.' }
$vsDev = Join-Path $installation 'Common7\Tools\VsDevCmd.bat'
$environmentLines = cmd.exe /d /s /c "`"$vsDev`" -arch=x86 -host_arch=x64 && set"
if ($LASTEXITCODE -ne 0) { throw 'The x86 C++ toolchain is unavailable' }
foreach ($line in $environmentLines) {
    $separator = $line.IndexOf('=')
    if ($separator -gt 0) {
        $key = $line.Substring(0,$separator)
        if ($key -match '^[A-Za-z_][A-Za-z0-9_]*$') { Set-Item -Path "Env:$key" -Value $line.Substring($separator+1) }
    }
}
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
& $python (Join-Path $PSScriptRoot 'build_client_compatibility.py') --output $OutputDirectory
if ($LASTEXITCODE -ne 0) { throw 'Client compatibility generation failed.' }
& $python (Join-Path $PSScriptRoot 'build_native_catalog.py') $ClientDirectory $OutputDirectory
if ($LASTEXITCODE -ne 0) { throw 'Verified skill catalog generation failed' }
& $python (Join-Path $PSScriptRoot 'build_native_skin.py') $ClientDirectory $OutputDirectory
if ($LASTEXITCODE -ne 0) { throw 'Native UI resource generation failed' }
& $python (Join-Path $PSScriptRoot 'build_bank_catalog.py') $ClientDirectory $OutputDirectory
if ($LASTEXITCODE -ne 0) { throw 'Bank item catalog generation failed' }
$audioDirectory = Join-Path $sourceDirectory 'AardvarkAudio'
$options = @('/nologo','/O2','/MT','/EHsc','/std:c++17','/utf-8','/W4','/DUNICODE','/D_UNICODE','/DWIN32_LEAN_AND_MEAN','/DNOMINMAX',"/I$OutputDirectory","/I$audioDirectory\include")
Push-Location -LiteralPath $OutputDirectory
try {
    & cl.exe @options /LD (Join-Path $sourceDirectory 'addon_loader.cpp') /link /DYNAMICBASE /NXCOMPAT /OUT:d3d9.dll "/DEF:$sourceDirectory\d3d9.def"
    if ($LASTEXITCODE -ne 0) { throw 'Addon loader compilation failed' }
    $audioSources = @('audio.cpp','wave.cpp','mp3.cpp') | ForEach-Object { Join-Path $audioDirectory ('src\'+$_) }
    & cl.exe @options /LD (Join-Path $sourceDirectory 'native_addon.cpp') (Join-Path $sourceDirectory 'native_hooks.cpp') (Join-Path $sourceDirectory 'overlay.cpp') (Join-Path $sourceDirectory 'AardvarkUI\ui.cpp') @audioSources /link /DYNAMICBASE /NXCOMPAT /OUT:Addons.dll "/DEF:$sourceDirectory\damage_meter.def" user32.lib gdi32.lib bcrypt.lib winhttp.lib
    if ($LASTEXITCODE -ne 0) { throw 'Native addon compilation failed' }
} finally { Pop-Location }
Get-Item -LiteralPath (Join-Path $OutputDirectory 'd3d9.dll'),(Join-Path $OutputDirectory 'Addons.dll') | Select-Object FullName,Length

foreach ($addon in @('DamageMeter','HideGoldLabels','CooldownTimers','Nameplates','BetterCharacterSheet','MythicDropSounds','WishingWellTracker','CursorCircle','SortBankPages','Moveeverything','Loadouts','Leaderboard','Controller')) {
    $destination = Join-Path $OutputDirectory ("Addons\"+$addon)
    New-Item -ItemType Directory -Path $destination -Force | Out-Null
    Copy-Item -LiteralPath (Join-Path $repositoryDirectory ("Addons\"+$addon+"\addon.ini")) -Destination $destination -Force
}
$licenseDirectory = Join-Path $OutputDirectory 'Addons\Licenses'
New-Item -ItemType Directory -Path $licenseDirectory -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $repositoryDirectory 'LICENSE') -Destination (Join-Path $licenseDirectory 'LICENSE.txt') -Force
foreach ($component in @('AardvarkUI','AardvarkHook','AardvarkAudio')) {
    Copy-Item -LiteralPath (Join-Path $sourceDirectory ($component+'\LICENSE')) -Destination (Join-Path $licenseDirectory ($component+'-LICENSE.txt')) -Force
}
