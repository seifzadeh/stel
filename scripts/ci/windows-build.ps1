$ErrorActionPreference = 'Stop'
$Project = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$Deps = 'C:/stel-deps'
function Checked([scriptblock]$Command) { & $Command; if ($LASTEXITCODE) { throw "Command failed: $Command" } }
$env:PATH = "$Deps/sdk/bin;$Deps/keychain/bin;$env:QT_ROOT_DIR/bin;$env:PATH"
$env:STEL_RESOURCE_PREFIX = "$Deps/sdk"
$env:QT_QPA_PLATFORM = 'offscreen'
$env:QT_PLUGIN_PATH = "$env:QT_ROOT_DIR/plugins"
Checked { cmake -S $Project -B "$Project/build-windows" -G Ninja -DCMAKE_BUILD_TYPE=Release `
    -DSTEL_QT_MAJOR=5 "-DSTEL_LINPHONE_ROOT=$Deps/sdk" `
    "-DCMAKE_PREFIX_PATH=$Deps/sdk;$Deps/keychain;$env:QT_ROOT_DIR" }
Checked { cmake --build "$Project/build-windows" --parallel 4 }
Checked { ctest --test-dir "$Project/build-windows" --output-on-failure }
$Crt = Get-ChildItem "$env:VCToolsRedistDir/x64" -Directory -Filter 'Microsoft.VC*.CRT' | Select-Object -First 1
$Ucrt = Get-ChildItem "$env:UniversalCRTSdkDir/Redist" -Directory | Sort-Object Name -Descending |
    ForEach-Object { Join-Path $_.FullName 'ucrt/DLLs/x64' } | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $Crt -or -not $Ucrt) { throw 'MSVC CRT and Universal CRT redistributables are required for a self-contained package' }
New-Item -ItemType Directory -Force "$Project/build-windows/certs" | Out-Null
$Ca = "$Project/build-windows/certs/ca-certificates.crt"
Invoke-WebRequest 'https://curl.se/ca/cacert.pem' -OutFile $Ca
Checked { python "$Project/scripts/package-windows.py" --qt $env:QT_ROOT_DIR --sdk "$Deps/sdk" `
    --keychain "$Deps/keychain" --ca $Ca --crt $Crt.FullName --ucrt $Ucrt --notices "$Deps/notices" }
Checked { python "$Project/scripts/ci/smoke-portable.py" "$Project/dist/Stel-portable-windows-x64.exe" }
