; Set search path
!addincludedir "${__FILEDIR__}"

; Installer initial constants
!define PRODUCT_NAME "CrossDesk"
!define PRODUCT_VERSION "${VERSION}"
!define PRODUCT_VERSION_NUMERIC "${VERSION_NUMERIC}"
!define PRODUCT_PUBLISHER "CrossDesk"
!define PRODUCT_WEB_SITE "https://www.crossdesk.cn/"
!define APP_NAME "CrossDesk"
!define UNINSTALL_REG_KEY "CrossDesk"
!define PRODUCT_SERVICE_NAME "CrossDeskService"

; Installer icon path
!define MUI_ICON "${__FILEDIR__}\..\..\resources\windows\crossdesk.ico"

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
VIProductVersion "${PRODUCT_VERSION_NUMERIC}"
VIAddVersionKey /LANG=2052 "CompanyName" "CrossDesk"
VIAddVersionKey /LANG=2052 "FileDescription" "CrossDesk Installer"
VIAddVersionKey /LANG=2052 "FileVersion" "${PRODUCT_VERSION}"
VIAddVersionKey /LANG=2052 "LegalCopyright" "Copyright (C) CrossDesk contributors"
VIAddVersionKey /LANG=2052 "OriginalFilename" "crossdesk-win-x64-${PRODUCT_VERSION}.exe"
VIAddVersionKey /LANG=2052 "ProductName" "${PRODUCT_NAME}"
VIAddVersionKey /LANG=2052 "ProductVersion" "${PRODUCT_VERSION}"
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
    Call StopInstalledService

    SetOutPath "$INSTDIR"
    SetOverwrite ifnewer

    ; Main application executable path
    File /oname=CrossDesk.exe "..\..\..\..\build\windows\x64\release\crossdesk.exe"
    ; Bundle service-side binaries required by the Windows service flow
    File "..\..\..\..\build\windows\x64\release\crossdesk_service.exe"
    File "..\..\..\..\build\windows\x64\release\crossdesk_session_helper.exe"
    ; Bundle runtime DLLs from the release output directory
    File "..\..\..\..\build\windows\x64\release\*.dll"

    Call RegisterInstalledService

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
    Call un.UnregisterInstalledService

    ; Delete main executable and uninstaller
    Delete "$INSTDIR\CrossDesk.exe"
    Delete "$INSTDIR\crossdesk_service.exe"
    Delete "$INSTDIR\crossdesk_session_helper.exe"
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

Function StopInstalledService
    IfFileExists "$INSTDIR\CrossDesk.exe" 0 stop_with_sc
    IfFileExists "$INSTDIR\crossdesk_service.exe" 0 stop_with_sc

    DetailPrint "Stopping existing CrossDesk service"
    ExecWait '"$INSTDIR\CrossDesk.exe" --service-stop' $0
    ${If} $0 = 0
        Return
    ${EndIf}

stop_with_sc:
    DetailPrint "Stopping existing CrossDesk service via Service Control Manager"
    ExecWait '"$SYSDIR\sc.exe" stop ${PRODUCT_SERVICE_NAME}' $0
    ${If} $0 != 0
    ${AndIf} $0 != 1060
    ${AndIf} $0 != 1062
        MessageBox MB_ICONSTOP|MB_OK "Failed to stop the existing CrossDesk service. The installation will be aborted."
        Abort
    ${EndIf}
    Sleep 1500
FunctionEnd

Function RegisterInstalledService
    IfFileExists "$INSTDIR\CrossDesk.exe" 0 missing_service_binary
    IfFileExists "$INSTDIR\crossdesk_service.exe" 0 missing_service_binary
    IfFileExists "$INSTDIR\crossdesk_session_helper.exe" 0 missing_service_binary

    DetailPrint "Registering CrossDesk service"
    ExecWait '"$INSTDIR\CrossDesk.exe" --service-install' $0
    ${If} $0 != 0
        MessageBox MB_ICONSTOP|MB_OK "Failed to register the CrossDesk service. The installation will be aborted."
        Abort
    ${EndIf}

    DetailPrint "CrossDesk service registered for on-demand start"

    Return

missing_service_binary:
    MessageBox MB_ICONSTOP|MB_OK "CrossDesk service files are missing from the installer package. The installation will be aborted."
    Abort
FunctionEnd

Function un.UnregisterInstalledService
    IfFileExists "$INSTDIR\CrossDesk.exe" 0 unregister_with_sc

    DetailPrint "Stopping CrossDesk service"
    ExecWait '"$INSTDIR\CrossDesk.exe" --service-stop' $0
    ${If} $0 = 0
        DetailPrint "Removing CrossDesk service"
        ExecWait '"$INSTDIR\CrossDesk.exe" --service-uninstall' $0
        ${If} $0 = 0
            Return
        ${EndIf}
    ${EndIf}

unregister_with_sc:
    DetailPrint "Removing CrossDesk service via Service Control Manager"
    ExecWait '"$SYSDIR\sc.exe" stop ${PRODUCT_SERVICE_NAME}' $0
    ${If} $0 != 0
    ${AndIf} $0 != 1060
    ${AndIf} $0 != 1062
        MessageBox MB_ICONSTOP|MB_OK "Failed to stop the CrossDesk service. Uninstall will be aborted."
        Abort
    ${EndIf}
    Sleep 1500

    ExecWait '"$SYSDIR\sc.exe" delete ${PRODUCT_SERVICE_NAME}' $0
    ${If} $0 != 0
    ${AndIf} $0 != 1060
        MessageBox MB_ICONSTOP|MB_OK "Failed to remove the CrossDesk service. Uninstall will be aborted."
        Abort
    ${EndIf}
FunctionEnd
