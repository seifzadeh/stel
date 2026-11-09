param(
    [Parameter(Mandatory=$true)][string]$QtPrefix,
    [Parameter(Mandatory=$true)][string]$LinphonePrefix,
    [Parameter(Mandatory=$true)][string]$CaBundle,
    [string]$KeychainPrefix = '',
    [switch]$Static
)
$ErrorActionPreference = 'Stop'
$Project = Split-Path -Parent $PSScriptRoot
$Build = Join-Path $Project 'build-windows'
$Stage = Join-Path $Project 'dist/Stel-windows-x64'
# Run in an x64 Native Tools Command Prompt (MSVC 2019/v142 for legacy Qt).
$StaticOption = if ($Static) { 'ON' } else { 'OFF' }
cmake -S $Project -B $Build -G Ninja -DCMAKE_BUILD_TYPE=Release `
  -DSTEL_QT_MAJOR=5 "-DSTEL_STATIC=$StaticOption" `
  "-DSTEL_LINPHONE_ROOT=$LinphonePrefix" "-DCMAKE_PREFIX_PATH=$QtPrefix;$LinphonePrefix;$KeychainPrefix"
if ($LASTEXITCODE) { throw 'CMake configure failed' }
cmake --build $Build --parallel 4
if ($LASTEXITCODE) { throw 'Build failed' }
ctest --test-dir $Build --output-on-failure -R '^core$'
if ($LASTEXITCODE) { throw 'Core tests failed' }
New-Item -ItemType Directory -Force $Stage | Out-Null
Copy-Item "$Build/stel.exe" $Stage
if (-not $Static) {
    & "$QtPrefix/bin/windeployqt.exe" --release --compiler-runtime --no-translations "$Stage/stel.exe"
    if ($LASTEXITCODE) { throw 'Qt deployment failed' }
    Get-ChildItem "$LinphonePrefix/bin" -Filter '*.dll' | Copy-Item -Destination $Stage
    if ($KeychainPrefix -and (Test-Path "$KeychainPrefix/bin")) {
        Get-ChildItem "$KeychainPrefix/bin" -Filter '*.dll' | Copy-Item -Destination $Stage
    }
}
if (Test-Path "$LinphonePrefix/share") { Copy-Item "$LinphonePrefix/share" $Stage -Recurse -Force }
if (Test-Path "$LinphonePrefix/lib/mediastreamer/plugins") {
    New-Item -ItemType Directory -Force "$Stage/lib/mediastreamer" | Out-Null
    Copy-Item "$LinphonePrefix/lib/mediastreamer/plugins" "$Stage/lib/mediastreamer" -Recurse -Force
}
New-Item -ItemType Directory -Force "$Stage/share/certs" | Out-Null
Copy-Item $CaBundle "$Stage/share/certs/ca-certificates.crt"
Copy-Item "$Project/LICENSE", "$Project/THIRD_PARTY.md", "$Project/README.fa.md" $Stage
if (Test-Path "$Stage/Stel.cmd") { Remove-Item "$Stage/Stel.cmd" }
# Static builds still need Windows system DLLs and SDK resource files.
& "$Stage/stel.exe" --config-dir "$Build/smoke-config" --smoke-test
if ($LASTEXITCODE) { throw 'Packaged application smoke test failed' }
Compress-Archive -Path "$Stage/*" -DestinationPath "$Project/dist/Stel-windows-x64.zip" -Force
python "$Project/scripts/package-single.py" --launcher "$Build/stel_portable_launcher.exe" --folder $Stage --output "$Project/dist/Stel-portable-windows-x64.exe"
if ($LASTEXITCODE) { throw 'Native single-file packaging failed' }
& "$Project/dist/Stel-portable-windows-x64.exe" --config-dir "$Build/single-smoke-config" --smoke-test
if ($LASTEXITCODE) { throw 'Single-file application smoke test failed' }
Write-Host 'Built on this Windows host. Test the exact archive on Windows 7 SP1, 8.1, 10 and 11 before release.'
