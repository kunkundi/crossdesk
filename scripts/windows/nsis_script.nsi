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

; Certificate path
!define CERT_FILE "${__FILEDIR__}\..\..\certs\crossdesk.cn_root.crt"

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

Name "${PRODUCT_NAME} ${PRODUCT_VERSION}"
OutFile "crossdesk-win-x64-${PRODUCT_VERSION}.exe"
InstallDir "$PROGRAMFILES\CrossDesk"
InstallDirRegKey HKCU "Software\${PRODUCT_NAME}" "InstallDir"
ShowInstDetails show

Function CheckVCRedist
  ReadRegDWORD $0 HKLM "SOFTWARE\Microsoft\VisualStudio\14.0\VC\Runtimes\x64" "Installed"
  StrCmp $0 1 done

  DetailPrint "Downloading Microsoft Visual C++ Redistributable..."
  inetc::get /popup "" /caption "Downloading VC++ Runtime..." \
    "https://aka.ms/vs/17/release/vc_redist.x64.exe" \
    "$TEMP\\vc_redist.x64.exe"
  Pop $0
  StrCmp $0 "OK" +2
    MessageBox MB_OK "Failed to download VC++ Redistributable. Please install it manually." IDOK done

  ExecWait '"$TEMP\\vc_redist.x64.exe" /quiet /norestart'

done:
FunctionEnd

Section "VC++ Runtime"
    Call CheckVCRedist
SectionEnd

Section "MainSection"
    SetOutPath "$INSTDIR"
    SetOverwrite ifnewer

    ; Main application executable path
    File /oname=crossdesk.exe "..\..\build\windows\x64\release\crossdesk.exe"
    
    ; Copy icon file to installation directory
    File "${MUI_ICON}"

    ; Write uninstall information
    WriteUninstaller "$INSTDIR\uninstall.exe"

    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${UNINSTALL_REG_KEY}" "DisplayName" "${PRODUCT_NAME}"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${UNINSTALL_REG_KEY}" "UninstallString" "$INSTDIR\uninstall.exe"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${UNINSTALL_REG_KEY}" "DisplayVersion" "${PRODUCT_VERSION}"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${UNINSTALL_REG_KEY}" "Publisher" "${PRODUCT_PUBLISHER}"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${UNINSTALL_REG_KEY}" "URLInfoAbout" "${PRODUCT_WEB_SITE}"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${UNINSTALL_REG_KEY}" "DisplayIcon" "$INSTDIR\crossdesk.ico"
    WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${UNINSTALL_REG_KEY}" "NoModify" 1
    WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${UNINSTALL_REG_KEY}" "NoRepair" 1
    WriteRegStr HKCU "Software\${PRODUCT_NAME}" "InstallDir" "$INSTDIR"
SectionEnd

; After installation
Section -Post
    ExecWait '"C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x86\mt.exe" -manifest "$INSTDIR\crossdesk.manifest" -outputresource:"$INSTDIR\crossdesk.exe";1'
SectionEnd

Section "Cert"
    SetOutPath "$APPDATA\CrossDesk\certs"
    File /r "${CERT_FILE}"
SectionEnd

Section -AdditionalIcons
    ; Desktop shortcut
    CreateShortCut "$DESKTOP\${PRODUCT_NAME}.lnk" "$INSTDIR\crossdesk.exe" "" "$INSTDIR\crossdesk.ico"

    ; Start menu shortcut
    CreateShortCut "$SMPROGRAMS\${PRODUCT_NAME}.lnk" "$INSTDIR\crossdesk.exe" "" "$INSTDIR\crossdesk.ico"
SectionEnd

Section "Uninstall"
    ; Delete main executable and uninstaller
    Delete "$INSTDIR\crossdesk.exe"
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
    Exec "$INSTDIR\crossdesk.exe"
FunctionEnd