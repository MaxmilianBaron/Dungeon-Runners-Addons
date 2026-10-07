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
$installation = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.v141.x86.x64 -property installationPath
$compilerRoot = Join-Path $installation 'VC/Tools/MSVC/14.16.27023'
if (-not (Test-Path -LiteralPath (Join-Path $compilerRoot 'bin/Hostx64/x86/cl.exe'))) { throw 'Install the Visual Studio v141 x86/x64 and Windows XP support components.' }
$sdkRoot = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits/10'
$sdk = @(Get-ChildItem -LiteralPath (Join-Path $sdkRoot 'Lib') -Directory | Where-Object { Test-Path -LiteralPath (Join-Path $_.FullName 'um/x86/kernel32.lib') } | Sort-Object Name -Descending)[0].Name
$crt = '10.0.10240.0'
if (-not (Test-Path -LiteralPath (Join-Path $sdkRoot "Lib/$crt/ucrt/x86/libucrt.lib"))) { throw 'The Windows 10 10240 static CRT is required for XP targeting.' }
$env:PATH = (Join-Path $compilerRoot 'bin/Hostx64/x86') + ';' + (Join-Path $compilerRoot 'bin/Hostx64/x64') + ';' + (Join-Path $sdkRoot "bin/$sdk/x64") + ';' + $env:PATH
$env:INCLUDE = @((Join-Path $compilerRoot 'include'), (Join-Path $sdkRoot "Include/$crt/ucrt"), (Join-Path $sdkRoot "Include/$sdk/shared"), (Join-Path $sdkRoot "Include/$sdk/um")) -join ';'
$env:LIB = @((Join-Path $compilerRoot 'lib/x86'), (Join-Path $sdkRoot "Lib/$crt/ucrt/x86"), (Join-Path $sdkRoot "Lib/$sdk/um/x86")) -join ';'
$env:VSLANG = '1033'
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
& (Join-Path $PSScriptRoot 'build_https.ps1') -OutputDirectory $OutputDirectory
$httpsLibraries = @((Join-Path $OutputDirectory 'https-build/AddonHttps.lib')) + @('mbedtls','mbedx509','mbedcrypto') | ForEach-Object { if ($_ -like '*.lib') { $_ } else { Join-Path $OutputDirectory ("https-build/mbedtls/library/$_.lib") } }
$options = @('/nologo','/O2','/MT','/EHsc','/std:c++17','/Zc:threadSafeInit-','/utf-8','/W4','/DUNICODE','/D_UNICODE','/DWIN32_LEAN_AND_MEAN','/DNOMINMAX','/DWINVER=0x0501','/D_WIN32_WINNT=0x0501','/D_USING_V110_SDK71_',"/I$OutputDirectory","/I$audioDirectory\include")
Push-Location -LiteralPath $OutputDirectory
try {
    & cl.exe @options /LD (Join-Path $sourceDirectory 'addon_loader.cpp') /link /SUBSYSTEM:WINDOWS,5.01 /OSVERSION:5.1 /DYNAMICBASE /NXCOMPAT /OUT:d3d9.dll "/DEF:$sourceDirectory\d3d9.def"
    if ($LASTEXITCODE -ne 0) { throw 'Addon loader compilation failed' }
    $audioSources = @('audio.cpp','wave.cpp','mp3.cpp') | ForEach-Object { Join-Path $audioDirectory ('src\'+$_) }
    & cl.exe @options /LD (Join-Path $sourceDirectory 'native_addon.cpp') (Join-Path $sourceDirectory 'native_hooks.cpp') (Join-Path $sourceDirectory 'mouse_look_hooks.cpp') (Join-Path $sourceDirectory 'overlay.cpp') (Join-Path $sourceDirectory 'AardvarkUI\ui.cpp') @audioSources /link /SUBSYSTEM:WINDOWS,5.01 /OSVERSION:5.1 /DYNAMICBASE /NXCOMPAT /OUT:Addons.dll "/DEF:$sourceDirectory\damage_meter.def" @httpsLibraries notices.res user32.lib gdi32.lib advapi32.lib winhttp.lib ws2_32.lib
    if ($LASTEXITCODE -ne 0) { throw 'Native addon compilation failed' }
} finally { Pop-Location }
foreach ($library in @('d3d9.dll','Addons.dll')) {
    & (Join-Path $PSScriptRoot 'Validate-WindowsImports.ps1') -Executable (Join-Path $OutputDirectory $library)
}
Get-Item -LiteralPath (Join-Path $OutputDirectory 'd3d9.dll'),(Join-Path $OutputDirectory 'Addons.dll') | Select-Object FullName,Length

foreach ($addon in @('DamageMeter','HideGoldLabels','CooldownTimers','Nameplates','BetterCharacterSheet','MythicDropSounds','WishingWellTracker','CursorCircle','SortBankPages','Moveeverything','Loadouts','Leaderboard','Controller','EnhancedSettings')) {
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
