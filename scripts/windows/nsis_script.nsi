; Set search path
!addincludedir "${__FILEDIR__}"

; Installer initial constants
!define PRODUCT_NAME "CrossDesk"
!define PRODUCT_VERSION "${VERSION}"
!define PRODUCT_PUBLISHER "CrossDesk"
!define PRODUCT_WEB_SITE "https://www.crossdesk.cn/"
!define APP_NAME "CrossDesk"
!define UNINSTALL_REG_KEY "CrossDesk"

; Installer icon path
!define MUI_ICON "${__FILEDIR__}\..\..\icons\windows\crossdesk.ico"

; Compression settings
SetCompressor /FINAL lzma

; Request admin privileges (needed to write HKLM)
RequestExecutionLevel admin

; ------ MUI Modern UI Definition ------
!include "MUI.nsh"
!define MUI_ABORTWARNING
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES

; Add run-after-install option
!define MUI_FINISHPAGE_RUN
!define MUI_FINISHPAGE_RUN_TEXT "Run ${PRODUCT_NAME}"
!define MUI_FINISHPAGE_RUN_FUNCTION LaunchApp

!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_LANGUAGE "SimpChinese"
!insertmacro MUI_RESERVEFILE_INSTALLOPTIONS
; ------ End of MUI Definition ------

; Include LogicLib for process handling
!include "LogicLib.nsh"

Name "${PRODUCT_NAME} ${PRODUCT_VERSION}"
OutFile "crossdesk-win-x64-${PRODUCT_VERSION}.exe"
InstallDir "$PROGRAMFILES\CrossDesk"
InstallDirRegKey HKCU "Software\${PRODUCT_NAME}" "InstallDir"
ShowInstDetails show

Section "MainSection"
    ; Check if CrossDesk is running
    StrCpy $1 "CrossDesk.exe"
    
    nsProcess::_FindProcess "$1"
    Pop $R0
    ${If} $R0 = 0  ;
        MessageBox MB_ICONQUESTION|MB_YESNO "CrossDesk is running. Do you want to close it and continue the installation?" IDYES closeApp IDNO cancelInstall
    ${Else}
        Goto installApp
    ${EndIf}

closeApp:
    nsProcess::_KillProcess "$1"
    Pop $R0
    Sleep 500
    Goto installApp

cancelInstall:
    SetDetailsPrint both
    MessageBox MB_ICONEXCLAMATION|MB_OK "Installation has been aborted."
    Abort

installApp:
    SetOutPath "$INSTDIR"
    SetOverwrite ifnewer

    ; Main application executable path
    File /oname=CrossDesk.exe "..\..\build\windows\x64\release\crossdesk.exe"

    ; Write uninstall information
    WriteUninstaller "$INSTDIR\uninstall.exe"

    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${UNINSTALL_REG_KEY}" "DisplayName" "${PRODUCT_NAME}"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${UNINSTALL_REG_KEY}" "UninstallString" "$INSTDIR\uninstall.exe"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${UNINSTALL_REG_KEY}" "DisplayVersion" "${PRODUCT_VERSION}"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${UNINSTALL_REG_KEY}" "Publisher" "${PRODUCT_PUBLISHER}"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${UNINSTALL_REG_KEY}" "URLInfoAbout" "${PRODUCT_WEB_SITE}"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${UNINSTALL_REG_KEY}" "DisplayIcon" "$INSTDIR\CrossDesk.exe"
    WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${UNINSTALL_REG_KEY}" "NoModify" 1
    WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${UNINSTALL_REG_KEY}" "NoRepair" 1
    WriteRegStr HKCU "Software\${PRODUCT_NAME}" "InstallDir" "$INSTDIR"
SectionEnd

Section -AdditionalIcons
    ; Desktop shortcut
    CreateShortCut "$DESKTOP\${PRODUCT_NAME}.lnk" "$INSTDIR\CrossDesk.exe" "" "$INSTDIR\CrossDesk.exe"

    ; Start menu shortcut
    CreateShortCut "$SMPROGRAMS\${PRODUCT_NAME}.lnk" "$INSTDIR\CrossDesk.exe" "" "$INSTDIR\CrossDesk.exe"
SectionEnd

Section "Uninstall"
    ; Check if CrossDesk is running
    StrCpy $1 "CrossDesk.exe"
    
    nsProcess::_FindProcess "$1"
    Pop $R0
    ${If} $R0 = 0
        MessageBox MB_ICONQUESTION|MB_YESNO "CrossDesk is running. Do you want to close it and uninstall?" IDYES closeApp IDNO cancelUninstall
    ${Else}
        Goto uninstallApp
    ${EndIf}

closeApp:
    nsProcess::_KillProcess "$1"
    Pop $R0
    Sleep 500
    Goto uninstallApp

cancelUninstall:
    SetDetailsPrint both
    MessageBox MB_ICONEXCLAMATION|MB_OK "Uninstallation has been aborted."
    Abort

uninstallApp:
    ; Delete main executable and uninstaller
    Delete "$INSTDIR\CrossDesk.exe"
    Delete "$INSTDIR\uninstall.exe"

    ; Recursively delete installation directory
    RMDir /r "$INSTDIR"

    ; Delete desktop and start menu shortcuts
    Delete "$DESKTOP\${PRODUCT_NAME}.lnk"
    Delete "$SMPROGRAMS\${PRODUCT_NAME}.lnk"

    ; Delete registry uninstall entry
    DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${UNINSTALL_REG_KEY}"

    ; Delete remembered install dir
    DeleteRegKey HKCU "Software\${PRODUCT_NAME}"

    ; Recursively delete CrossDesk folder in user AppData
    RMDir /r "$APPDATA\CrossDesk"
    RMDir /r "$LOCALAPPDATA\CrossDesk"
SectionEnd

; ------ Functions ------
Function LaunchApp
    Exec "$INSTDIR\CrossDesk.exe"
FunctionEnd
