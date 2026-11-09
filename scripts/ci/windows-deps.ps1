param([string]$Destination = 'C:/stel-deps', [switch]$DownloadOnly)
$ErrorActionPreference = 'Stop'
$Project = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$Lock = Get-Content "$Project/packaging/dependencies.json" -Raw | ConvertFrom-Json
$Python = if (Get-Command python -ErrorAction SilentlyContinue) { 'python' } else { 'python3' }
function Checked([scriptblock]$Command) {
    & $Command
    if ($LASTEXITCODE -ne 0) { throw "Command failed: $Command" }
}
function Checkout-Pinned($Dependency, [string]$Directory) {
    if (-not (Test-Path "$Directory/.git")) {
        Checked { git -c core.longpaths=true clone --depth 1 --branch $Dependency.version $Dependency.url $Directory }
    }
    $Revision = (& git -C $Directory rev-parse HEAD).Trim()
    if ($LASTEXITCODE -or $Revision -ne $Dependency.commit) { throw "Dependency revision mismatch: $Directory" }
    if ($Dependency.windows_submodules) {
        Checked { git -C $Directory submodule init }
        foreach ($Mirror in $Dependency.source_mirrors.PSObject.Properties) {
            Checked { git -C $Directory config "submodule.$($Mirror.Name).url" $Mirror.Value }
        }
        $ArchivePaths = @($Dependency.source_archives | ForEach-Object { $_.path })
        $Paths = @($Dependency.windows_submodules | Where-Object { $_ -notin $ArchivePaths })
        # Keep the authoritative SDK gitlinks. Mirrors only supply the same Git
        # objects; no branch head or alternative revision is accepted.
        Checked { git -C $Directory -c core.longpaths=true `
            -c 'url.https://github.com/BelledonneCommunications/.insteadOf=https://gitlab.linphone.org/BC/public/' `
            submodule update --init --recursive --depth 1 --jobs 4 -- @Paths }
        foreach ($Path in $Paths) {
            $Expected = (& git -C $Directory rev-parse "HEAD:$Path").Trim()
            if ($LASTEXITCODE) { throw "Cannot read SDK gitlink: $Path" }
            $Actual = (& git -C "$Directory/$Path" rev-parse HEAD).Trim()
            if ($LASTEXITCODE -or $Actual -ne $Expected) { throw "SDK submodule revision mismatch: $Path" }
        }
        if ($Dependency.source_archives) {
            Checked { & $Python "$Project/scripts/ci/fetch-source-archives.py" --sdk $Directory }
        }
    } else {
        Checked { git -C $Directory -c core.longpaths=true submodule update --init --recursive --depth 1 --jobs 4 }
    }
}
if (-not $DownloadOnly -and -not $env:QT_ROOT_DIR) { throw 'Install Qt 5.15.2 and set QT_ROOT_DIR first' }
if (-not $DownloadOnly) {
    $MsysRoot = if ($env:STEL_MSYS2_ROOT) { $env:STEL_MSYS2_ROOT } else { 'C:/msys64' }
    $CompilerBin = Split-Path -Parent (Get-Command cl.exe -ErrorAction Stop).Source
    $PythonBin = Split-Path -Parent (Get-Command $Python -ErrorAction Stop).Source
    $CmakeBin = Split-Path -Parent (Get-Command cmake.exe -ErrorAction Stop).Source
    $NinjaBin = Split-Path -Parent (Get-Command ninja.exe -ErrorAction Stop).Source
    # Native compiler, linker and Python precede MSYS tools. Use the action's
    # actual location, which is normally on D: on hosted Windows runners.
    $env:PATH = "$CompilerBin;$env:WindowsSdkVerBinPath/x64;$PythonBin;$PythonBin/Scripts;$CmakeBin;$NinjaBin;$MsysRoot/ucrt64/bin;$MsysRoot;$MsysRoot/usr/bin;$env:PATH"
    foreach ($Tool in @('nasm.exe','yasm.exe','gcc.exe','sh.exe','msys2_shell.cmd')) {
        $Resolved = Get-Command $Tool -ErrorAction Stop
        Write-Host "$Tool => $($Resolved.Source)"
    }
}
New-Item -ItemType Directory -Force "$Destination/src" | Out-Null
$SdkSource = "$Destination/src/linphone-sdk"
$KeychainSource = "$Destination/src/qtkeychain"
Checkout-Pinned $Lock.linphone_sdk $SdkSource
Checkout-Pinned $Lock.qtkeychain $KeychainSource
if ($DownloadOnly) { Write-Host 'Pinned dependency sources and SDK gitlinks verified.'; return }
Checked { & $Python "$Project/scripts/ci/patch-sdk.py" $SdkSource }
$env:PATH += ';C:/Program Files/7-Zip'
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
Copy-Item "$Project/packaging/patches" "$Destination/notices" -Recurse -Force
# Cache installed libraries, exact sources and notices, rather than object files.
Remove-Item $SdkBuild, "$Destination/build-keychain" -Recurse -Force
