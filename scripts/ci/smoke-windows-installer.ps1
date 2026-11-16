$ErrorActionPreference = 'Stop'
$Project = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$Installer = Join-Path $Project 'dist/Stel-setup-windows-x64.exe'
$Install = Join-Path $env:LOCALAPPDATA 'Stel installer تست'
$Registry = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\Stel'
function Run-Setup {
    $Process = Start-Process -FilePath $Installer -ArgumentList "/S /D=$Install" -Wait -PassThru
    if ($Process.ExitCode) { throw "Installer failed: $($Process.ExitCode)" }
}
if ($env:GITHUB_ACTIONS -ne 'true') { throw 'Installer lifecycle testing is restricted to the disposable Windows CI runner' }
if (Test-Path $Install) { throw 'Refusing to reuse an existing installation test directory' }
$Canary = Join-Path $Install 'user-file-to-preserve.txt'
try {
    Run-Setup
    if (-not (Test-Path "$Install/Stel.exe") -or -not (Test-Path "$Install/Uninstall.exe")) { throw 'Installed executable or uninstaller is missing' }
    $Expected = (python "$Project/scripts/release.py" metadata | ConvertFrom-Json).version
    if ($LASTEXITCODE) { throw 'Cannot read release metadata' }
    if ((Get-ItemProperty $Registry).DisplayVersion -ne $Expected) { throw 'Wrong installed version' }
    if (-not (Test-Path "$env:APPDATA/Microsoft/Windows/Start Menu/Programs/Stel/Stel.lnk")) { throw 'Start-menu shortcut is missing' }
    if (-not (Test-Path ([IO.Path]::Combine([Environment]::GetFolderPath('Desktop'), 'Stel.lnk')))) { throw 'Desktop shortcut is missing' }
    Set-Content -Path $Canary -Value 'preserve files that do not belong to the installer'
    Run-Setup
    if (-not (Test-Path $Canary)) { throw 'Upgrade deleted an unrelated file' }
    python "$Project/scripts/ci/smoke-portable.py" "$Install/Stel.exe"
    if ($LASTEXITCODE) { throw 'Installed application smoke test failed' }
    # _?= runs the uninstaller in place and lets CI wait for actual completion.
    $Process = Start-Process -FilePath "$Install/Uninstall.exe" -ArgumentList "/S _?=$Install" -Wait -PassThru
    if ($Process.ExitCode) { throw "Uninstaller failed: $($Process.ExitCode)" }
    if ((Test-Path "$Install/Stel.exe") -or (Test-Path $Registry)) { throw 'Application or uninstall registration was left behind' }
    if (-not (Test-Path $Canary)) { throw 'Uninstall deleted an unrelated file' }
    Write-Host 'PASS Windows installer: install, shortcuts, reinstall, application launch and uninstall'
} finally {
    # The test directory is private; user profiles and application accounts are untouched.
    if (Test-Path $Install) { Remove-Item -LiteralPath $Install -Recurse -Force }
}
