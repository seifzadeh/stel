param([string]$Destination = 'C:/stel-deps')
$ErrorActionPreference = 'Stop'
$Project = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$Lock = Get-Content "$Project/packaging/dependencies.json" -Raw | ConvertFrom-Json
function Checked([scriptblock]$Command) {
    & $Command
    if ($LASTEXITCODE -ne 0) { throw "Command failed: $Command" }
}
function Checkout-Pinned($Dependency, [string]$Directory) {
    Checked { git -c core.longpaths=true clone --depth 1 --branch $Dependency.version $Dependency.url $Directory }
    $Revision = (& git -C $Directory rev-parse HEAD).Trim()
    if ($LASTEXITCODE -or $Revision -ne $Dependency.commit) { throw "Dependency revision mismatch: $Directory" }
    Checked { git -C $Directory -c core.longpaths=true submodule update --init --recursive --depth 1 --jobs 4 }
}
if (-not $env:QT_ROOT_DIR) { throw 'Install Qt 5.15.2 and set QT_ROOT_DIR first' }
New-Item -ItemType Directory -Force "$Destination/src" | Out-Null
$SdkSource = "$Destination/src/linphone-sdk"
$KeychainSource = "$Destination/src/qtkeychain"
Checkout-Pinned $Lock.linphone_sdk $SdkSource
Checkout-Pinned $Lock.qtkeychain $KeychainSource
# Native MSVC/Python/CMake remain first; MSYS provides the SDK's shell tools.
$env:PATH += ';C:/msys64/mingw64/bin;C:/msys64;C:/msys64/usr/bin;C:/Program Files/7-Zip'
$SdkBuild = "$Destination/build-sdk"
Checked { cmake -S $SdkSource -B $SdkBuild -G Ninja `
    -DCMAKE_BUILD_TYPE=Release "-DCMAKE_INSTALL_PREFIX=$Destination/sdk" `
    "-DLINPHONESDK_VERSION=$($Lock.linphone_sdk.version)" `
    -DLINPHONESDK_PLATFORM=Desktop -DBUILD_SHARED_LIBS=ON `
    -DENABLE_WINDOWS_TOOLS_CHECK=OFF -DENABLE_STRICT=OFF `
    -DENABLE_DOC=OFF -DENABLE_TOOLS=OFF -DENABLE_TESTS_COMPONENT=OFF -DENABLE_UNIT_TESTS=OFF `
    -DENABLE_CSHARP_WRAPPER=OFF -DENABLE_CXX_WRAPPER=OFF -DENABLE_JAVA_WRAPPER=OFF `
    -DENABLE_ADVANCED_IM=OFF -DENABLE_DB_STORAGE=OFF -DENABLE_LIME_X3DH=OFF -DENABLE_LDAP=OFF -DENABLE_QRCODE=OFF `
    -DENABLE_DAEMON=OFF -DENABLE_TUTORIALS=OFF -DENABLE_FLEXIAPI=OFF `
    -DENABLE_PQCRYPTO=OFF -DENABLE_QT_GL=OFF `
    -DENABLE_VIDEO=ON -DENABLE_AV1=ON -DENABLE_GPL_THIRD_PARTIES=ON -DENABLE_G729=ON `
    -DENABLE_NON_FREE_FEATURES=ON -DENABLE_OPENH264=ON -DENABLE_EMBEDDED_OPENH264=ON `
    -DENABLE_SRTP=ON -DENABLE_ZRTP=ON }
Checked { cmake --build $SdkBuild --target install --parallel 4 }
Checked { cmake -S $KeychainSource -B "$Destination/build-keychain" -G Ninja `
    -DCMAKE_BUILD_TYPE=Release "-DCMAKE_INSTALL_PREFIX=$Destination/keychain" `
    "-DCMAKE_PREFIX_PATH=$env:QT_ROOT_DIR" -DBUILD_WITH_QT6=OFF -DBUILD_SHARED_LIBS=ON `
    -DBUILD_TEST_APPLICATION=OFF }
Checked { cmake --build "$Destination/build-keychain" --target install --parallel 4 }
New-Item -ItemType Directory -Force "$Destination/notices" | Out-Null
Get-ChildItem "$Destination/src" -Recurse -File | Where-Object {
    $_.Name -match '^(COPYING|LICEN[CS]E|COPYRIGHT|NOTICE)(\.|$)'
} | ForEach-Object {
    $Relative = [IO.Path]::GetRelativePath("$Destination/src", $_.FullName)
    $Target = Join-Path "$Destination/notices" $Relative
    New-Item -ItemType Directory -Force (Split-Path -Parent $Target) | Out-Null
    Copy-Item $_.FullName $Target
}
& git -C $SdkSource submodule status --recursive | Set-Content "$Destination/notices/sdk-submodules.txt"
if ($LASTEXITCODE) { throw 'Cannot record SDK source revisions' }
Copy-Item "$Project/packaging/dependencies.json" "$Destination/notices/dependencies.json"
# Cache installed libraries, exact sources and notices, rather than object files.
Remove-Item $SdkBuild, "$Destination/build-keychain" -Recurse -Force
