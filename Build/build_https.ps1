param([Parameter(Mandatory)][string]$OutputDirectory)
$ErrorActionPreference='Stop'
$repository=Split-Path $PSScriptRoot
$source=Join-Path $repository 'Source/Https'
$dependencies=Join-Path $repository '.build/dependencies'
[IO.Directory]::CreateDirectory($dependencies) | Out-Null
$archive=Join-Path $dependencies 'mbedtls-3.6.7.tar.bz2'
$checksum='a7e8bcbec0e6f761b4af24f25677626b35f762f68eef79c08677a363212d11f6'
if (-not (Test-Path -LiteralPath $archive) -or (Get-FileHash -LiteralPath $archive).Hash -ne $checksum) {
    Invoke-WebRequest -Uri 'https://github.com/Mbed-TLS/mbedtls/releases/download/mbedtls-3.6.7/mbedtls-3.6.7.tar.bz2' -OutFile $archive -UseBasicParsing
}
if ((Get-FileHash -LiteralPath $archive).Hash -ne $checksum) { throw 'TLS dependency verification failed.' }
if (-not (Test-Path -LiteralPath (Join-Path $dependencies 'mbedtls-3.6.7'))) {
    & python -m tarfile -e $archive $dependencies
    if ($LASTEXITCODE -ne 0) { throw 'TLS dependency extraction failed.' }
}
$certificates=Join-Path $source 'cacert.pem'
if ((Get-FileHash -LiteralPath $certificates).Hash -ne 'a41b5d356aea97a529fe27e0f7316d2f9d946d75927476cf9cf1b90637d00505') { throw 'Bundled certificate verification failed.' }
$certificateBytes=[IO.File]::ReadAllBytes($certificates)
$header=[Text.StringBuilder]::new("#pragma once`nstatic const unsigned char HttpsRoots[]={")
for ($offset=0;$offset -lt $certificateBytes.Length;$offset+=256) {
    $last=[Math]::Min($offset+255,$certificateBytes.Length-1)
    [void]$header.AppendLine(($certificateBytes[$offset..$last] -join ',')+',')
}
[void]$header.AppendLine('0};')
[IO.File]::WriteAllText((Join-Path $OutputDirectory 'https_roots.generated.h'),$header.ToString())
$build=Join-Path $OutputDirectory 'https-build'
$compiler=(Get-Command cl.exe -ErrorAction Stop).Source.Replace('\','/')
& cmake -S $source -B $build -G Ninja '-DCMAKE_BUILD_TYPE=Release' '-DCMAKE_POLICY_VERSION_MINIMUM=3.5' "-DCMAKE_C_COMPILER=$compiler" "-DCMAKE_CXX_COMPILER=$compiler" "-DDEPENDENCIES=$($dependencies.Replace('\','/'))" "-DGENERATED=$($OutputDirectory.Replace('\','/'))"
if ($LASTEXITCODE -ne 0) { throw 'TLS library configuration failed.' }
& cmake --build $build --parallel 4
if ($LASTEXITCODE -ne 0) { throw 'TLS library build failed.' }
$licenses=Join-Path $OutputDirectory 'Addons/Licenses'
[IO.Directory]::CreateDirectory($licenses) | Out-Null
Copy-Item -LiteralPath (Join-Path $dependencies 'mbedtls-3.6.7/LICENSE') -Destination (Join-Path $licenses 'MbedTLS-LICENSE.txt') -Force
Copy-Item -LiteralPath (Join-Path $source 'MPL-2.0.txt') -Destination (Join-Path $licenses 'MozillaCA-LICENSE.txt') -Force
$notices="Dungeon Runners Addons third-party notices`r`n`r`nMbed TLS 3.6.7, used under Apache-2.0.`r`nSource: https://github.com/Mbed-TLS/mbedtls/releases/tag/mbedtls-3.6.7`r`n`r`n"+[IO.File]::ReadAllText((Join-Path $licenses 'MbedTLS-LICENSE.txt'))+"`r`nMozilla CA certificate bundle, used under MPL-2.0.`r`nSource: https://github.com/MaxmilianBaron/Dungeon-Runners-Addons/tree/main/Source/Https`r`n`r`n"+[IO.File]::ReadAllText((Join-Path $licenses 'MozillaCA-LICENSE.txt'))
$noticePath=Join-Path $OutputDirectory 'notices.generated.txt'
[IO.File]::WriteAllText($noticePath,$notices,[Text.UTF8Encoding]::new($false))
$resourcePath=Join-Path $OutputDirectory 'notices.generated.rc'
[IO.File]::WriteAllText($resourcePath,('301 RCDATA "'+$noticePath.Replace('\','/')+'"'),[Text.UTF8Encoding]::new($false))
& rc.exe /nologo ("/fo"+(Join-Path $OutputDirectory 'notices.res')) $resourcePath
if ($LASTEXITCODE -ne 0) { throw 'License resource compilation failed.' }
