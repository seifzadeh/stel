Unicode true
!include "MUI2.nsh"
!include "LogicLib.nsh"
!include "WinVer.nsh"
!include "x64.nsh"
!ifndef VERSION
  !error "Build with scripts/package-installers.py windows"
!endif
Name "Stel"
OutFile "${OUTPUT}"
InstallDir "$LOCALAPPDATA\Programs\Stel"
InstallDirRegKey HKCU "Software\Stel\Installer" "InstallLocation"
RequestExecutionLevel user
ManifestSupportedOS all
SetCompressor zlib
SetCompress off
Icon "${PROJECT}\packaging\stel.ico"
UninstallIcon "${PROJECT}\packaging\stel.ico"
VIProductVersion "${VERSION}.0"
VIAddVersionKey /LANG=1033 "ProductName" "Stel"
VIAddVersionKey /LANG=1033 "ProductVersion" "${VERSION}"
VIAddVersionKey /LANG=1033 "FileVersion" "${VERSION}"
VIAddVersionKey /LANG=1033 "FileDescription" "Stel Setup"
VIAddVersionKey /LANG=1033 "LegalCopyright" "Mehrdad Seifzadeh - GPL-3.0-or-later"
!define UNINSTALL_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\Stel"
!define MUI_ABORTWARNING
!define MUI_FINISHPAGE_RUN "$INSTDIR\Stel.exe"
!define MUI_FINISHPAGE_RUN_NOTCHECKED
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE "${PROJECT}\LICENSE"
!insertmacro MUI_PAGE_COMPONENTS
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "English"

Function .onInit
  SetShellVarContext current
  SetRegView 64
  ${IfNot} ${RunningX64}
    MessageBox MB_ICONSTOP "Stel requires 64-bit Windows."
    SetErrorLevel 1
    Quit
  ${EndIf}
  ${IfNot} ${AtLeastWin7}
    MessageBox MB_ICONSTOP "Stel requires Windows 7 or newer."
    SetErrorLevel 1
    Quit
  ${EndIf}
FunctionEnd

!macro RequireClosed PREFIX
Function ${PREFIX}RequireClosed
retry:
  IfFileExists "$INSTDIR\Stel.exe" 0 done
  ClearErrors
  FileOpen $0 "$INSTDIR\Stel.exe" a
  ${If} ${Errors}
    IfSilent cancel
    MessageBox MB_RETRYCANCEL|MB_ICONEXCLAMATION "Close Stel before continuing. Active calls will not be closed automatically." IDRETRY retry
cancel:
    SetErrorLevel 1
    Abort
  ${EndIf}
  FileClose $0
done:
FunctionEnd
!macroend
!insertmacro RequireClosed ""
!insertmacro RequireClosed "un."

Section "Stel (required)"
  SectionIn RO
  Call RequireClosed
  SetOutPath "$INSTDIR"
  File /oname=Stel.exe "${PAYLOAD}"
  File /oname=stel.ico "${PROJECT}\packaging\stel.ico"
  File /oname=LICENSE.txt "${PROJECT}\LICENSE"
  File "${PROJECT}\THIRD_PARTY.md"
  WriteUninstaller "$INSTDIR\Uninstall.exe"
  CreateDirectory "$SMPROGRAMS\Stel"
  CreateShortcut "$SMPROGRAMS\Stel\Stel.lnk" "$INSTDIR\Stel.exe" "" "$INSTDIR\stel.ico"
  CreateShortcut "$SMPROGRAMS\Stel\Uninstall Stel.lnk" "$INSTDIR\Uninstall.exe"
  WriteRegStr HKCU "Software\Stel\Installer" "InstallLocation" "$INSTDIR"
  WriteRegStr HKCU "${UNINSTALL_KEY}" "DisplayName" "Stel"
  WriteRegStr HKCU "${UNINSTALL_KEY}" "DisplayVersion" "${VERSION}"
  WriteRegStr HKCU "${UNINSTALL_KEY}" "Publisher" "Mehrdad Seifzadeh"
  WriteRegStr HKCU "${UNINSTALL_KEY}" "URLInfoAbout" "https://github.com/seifzadeh/stel"
  WriteRegStr HKCU "${UNINSTALL_KEY}" "InstallLocation" "$INSTDIR"
  WriteRegStr HKCU "${UNINSTALL_KEY}" "DisplayIcon" "$INSTDIR\stel.ico"
  WriteRegStr HKCU "${UNINSTALL_KEY}" "UninstallString" '$\"$INSTDIR\Uninstall.exe$\"'
  WriteRegStr HKCU "${UNINSTALL_KEY}" "QuietUninstallString" '$\"$INSTDIR\Uninstall.exe$\" /S'
  WriteRegDWORD HKCU "${UNINSTALL_KEY}" "EstimatedSize" ${ESTIMATED_SIZE}
  WriteRegDWORD HKCU "${UNINSTALL_KEY}" "NoModify" 1
  WriteRegDWORD HKCU "${UNINSTALL_KEY}" "NoRepair" 1
SectionEnd

Section "Desktop shortcut"
  CreateShortcut "$DESKTOP\Stel.lnk" "$INSTDIR\Stel.exe" "" "$INSTDIR\stel.ico"
  WriteRegDWORD HKCU "Software\Stel\Installer" "DesktopShortcut" 1
SectionEnd

Section "Uninstall"
  SetShellVarContext current
  SetRegView 64
  Call un.RequireClosed
  ClearErrors
  Delete "$INSTDIR\Stel.exe"
  ${If} ${Errors}
    SetErrorLevel 1
    Abort
  ${EndIf}
  ReadRegDWORD $0 HKCU "Software\Stel\Installer" "DesktopShortcut"
  ${If} $0 == 1
    Delete "$DESKTOP\Stel.lnk"
  ${EndIf}
  Delete "$SMPROGRAMS\Stel\Stel.lnk"
  Delete "$SMPROGRAMS\Stel\Uninstall Stel.lnk"
  RMDir "$SMPROGRAMS\Stel"
  Delete "$INSTDIR\stel.ico"
  Delete "$INSTDIR\LICENSE.txt"
  Delete "$INSTDIR\THIRD_PARTY.md"
  Delete "$INSTDIR\Uninstall.exe"
  RMDir "$INSTDIR"
  DeleteRegKey HKCU "${UNINSTALL_KEY}"
  DeleteRegKey HKCU "Software\Stel\Installer"
  ; Deliberately preserve accounts, credentials and history in the user profile.
SectionEnd
