; The installer of ImapKit Desktop, built with NSIS 3.08 or newer (makensis runs on Windows, macOS and Linux):
;
;   makensis -NOCD -DVERSION=0.1.0 -DSTAGING=dist/ImapKitDesktop -DOUTFILE=dist/ImapKitDesktop-Setup-0.1.0.exe \
;            [-DSIGN] installer/imapkit-desktop.nsi
;
; Run it from the root of the repository, -NOCD keeps the relative paths there. STAGING holds what gets installed:
; ImapKitDesktop.exe, sidecar\ and node\ (scripts/win-package.cmd makes it).
; With SIGN the uninstaller and the installer are signed while they are built, with scripts/sign-win.sh.
;
; Updates: every version installs to the same per-user folder and registry keys, so running the installer of a
; newer version upgrades in place. A running copy is closed first (it saves its mailboxes when it closes), the
; bundled sidecar and Node.js are replaced as a whole, and the user data in %APPDATA%\ImapKitDesktop stays.

Unicode true
ManifestDPIAware true
SetCompressor /SOLID lzma

!include MUI2.nsh
!include LogicLib.nsh
!include WordFunc.nsh
!include FileFunc.nsh

!ifndef VERSION
    !error "VERSION is not defined, pass -DVERSION=x.y.z"
!endif
!ifndef STAGING
    !error "STAGING is not defined, pass -DSTAGING=<folder with the files to install>"
!endif
!ifndef OUTFILE
    !define OUTFILE "ImapKitDesktop-Setup-${VERSION}.exe"
!endif

!define PRODUCT "ImapKit Desktop"
!define EXE "ImapKitDesktop.exe"
!define PUBLISHER "Postal Systems OÜ"
!define HOMEPAGE "https://github.com/postalsys/imapkit-desktop"
; the same keys for every version, this is how a newer installer finds the installed one
!define REGKEY "Software\Postal Systems\ImapKit Desktop"
!define UNINSTKEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\ImapKitDesktop"
; forward slashes work for makensis on every platform, and for the shell that runs the signing script
!define ROOT "${__FILEDIR__}/.."

!ifdef SIGN
    ; signs the uninstaller before it is packed into the installer, and the installer once it is written
    !uninstfinalize '"${ROOT}/scripts/sign-win.sh" "%1"' = 0
    !finalize '"${ROOT}/scripts/sign-win.sh" "%1"' = 0
!endif

Name "${PRODUCT}"
OutFile "${OUTFILE}"
; per user, no administrator rights needed. A mock server for development is a per user tool
RequestExecutionLevel user
InstallDir "$LOCALAPPDATA\Programs\${PRODUCT}"
InstallDirRegKey HKCU "${REGKEY}" "InstallDir"
BrandingText "${PRODUCT} ${VERSION}"

VIProductVersion "${VERSION}.0"
VIFileVersion "${VERSION}.0"
VIAddVersionKey "ProductName" "${PRODUCT}"
VIAddVersionKey "ProductVersion" "${VERSION}"
VIAddVersionKey "FileVersion" "${VERSION}"
VIAddVersionKey "CompanyName" "${PUBLISHER}"
VIAddVersionKey "FileDescription" "${PRODUCT} Setup"
VIAddVersionKey "LegalCopyright" "Copyright (c) ${PUBLISHER}"

Var InstalledVersion
Var WelcomeText

!define MUI_ICON "${ROOT}/host-win/res/app.ico"
!define MUI_UNICON "${ROOT}/host-win/res/app.ico"
!define MUI_ABORTWARNING
!define MUI_WELCOMEPAGE_TEXT "$WelcomeText"
!define MUI_FINISHPAGE_RUN "$INSTDIR\${EXE}"
!define MUI_FINISHPAGE_RUN_TEXT "Start ${PRODUCT}"

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE "${ROOT}/LICENSE"
; an upgrade goes where the installed version is, the folder is chosen on the first install only
!define MUI_PAGE_CUSTOMFUNCTION_PRE SkipDirectoryOnUpgrade
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

!insertmacro MUI_LANGUAGE "English"

; Closes a running ImapKit Desktop of this user. taskkill without /f sends WM_CLOSE, so the app stops its servers
; and saves the mailboxes. A running exe can not be opened for writing (it can be renamed, so that is no test), and
; neither can node.exe while the sidecar is still saving; after 15 seconds the app is ended forcefully, and its
; Node.js sidecar with it (they share a job object)
!macro CLOSE_APP prefix
    Function ${prefix}IsRunning
        StrCpy $3 0
        ${If} ${FileExists} "$INSTDIR\${EXE}"
            ClearErrors
            FileOpen $2 "$INSTDIR\${EXE}" a
            ${If} ${Errors}
                StrCpy $3 1
            ${Else}
                FileClose $2
            ${EndIf}
        ${EndIf}
        ${If} ${FileExists} "$INSTDIR\node\node.exe"
            ClearErrors
            FileOpen $2 "$INSTDIR\node\node.exe" a
            ${If} ${Errors}
                StrCpy $3 1
            ${Else}
                FileClose $2
            ${EndIf}
        ${EndIf}
    FunctionEnd

    Function ${prefix}CloseApp
        Call ${prefix}IsRunning
        ${If} $3 = 0
            Return
        ${EndIf}
        ReadEnvStr $0 USERNAME
        nsExec::Exec '"$SYSDIR\taskkill.exe" /fi "USERNAME eq $0" /im ${EXE}'
        Pop $1
        StrCpy $1 0
        ${Do}
            Sleep 500
            Call ${prefix}IsRunning
            ${IfThen} $3 = 0 ${|} ${Break} ${|}
            IntOp $1 $1 + 1
            ${If} $1 = 30
                nsExec::Exec '"$SYSDIR\taskkill.exe" /f /fi "USERNAME eq $0" /im ${EXE}'
                Pop $2
            ${ElseIf} $1 > 40
                MessageBox MB_RETRYCANCEL|MB_ICONEXCLAMATION "${PRODUCT} is still running. Close it and try again." /SD IDCANCEL IDRETRY +2
                Abort
                StrCpy $1 0
            ${EndIf}
        ${Loop}
    FunctionEnd
!macroend
!insertmacro CLOSE_APP ""
!insertmacro CLOSE_APP "un."

Function .onInit
    ReadRegStr $InstalledVersion HKCU "${UNINSTKEY}" "DisplayVersion"
    ${If} $InstalledVersion == ""
        StrCpy $WelcomeText "This will install ${PRODUCT} ${VERSION}, a mock IMAP and SMTP server for testing email clients.$\r$\n$\r$\nNo administrator rights are needed, it is installed for your account only."
        Return
    ${EndIf}
    ; 0 the same version, 1 this one is newer, 2 the installed one is newer
    ${VersionCompare} "${VERSION}" "$InstalledVersion" $0
    ${If} $0 = 2
        MessageBox MB_YESNO|MB_ICONQUESTION "${PRODUCT} $InstalledVersion is installed, which is newer than ${VERSION}. Install the older version anyway?" /SD IDNO IDYES +2
        Abort
    ${EndIf}
    ${If} $0 = 0
        StrCpy $WelcomeText "${PRODUCT} ${VERSION} is installed already. This will install it again.$\r$\n$\r$\nYour mailboxes and settings are kept."
    ${Else}
        StrCpy $WelcomeText "This will update ${PRODUCT} $InstalledVersion to ${VERSION}.$\r$\n$\r$\nIf it is running it is closed first. Your mailboxes and settings are kept."
    ${EndIf}
FunctionEnd

Function SkipDirectoryOnUpgrade
    ${If} $InstalledVersion != ""
        Abort
    ${EndIf}
FunctionEnd

Section "Install"
    Call CloseApp
    SetOutPath "$INSTDIR"
    ; the bundled sidecar and Node.js are replaced as a whole, files that a newer version left out must not linger
    RMDir /r "$INSTDIR\sidecar"
    RMDir /r "$INSTDIR\node"
    File /r "${STAGING}/*"
    WriteUninstaller "$INSTDIR\uninstall.exe"

    CreateShortcut "$SMPROGRAMS\${PRODUCT}.lnk" "$INSTDIR\${EXE}"

    WriteRegStr HKCU "${REGKEY}" "InstallDir" "$INSTDIR"
    WriteRegStr HKCU "${REGKEY}" "Version" "${VERSION}"
    WriteRegStr HKCU "${UNINSTKEY}" "DisplayName" "${PRODUCT}"
    WriteRegStr HKCU "${UNINSTKEY}" "DisplayVersion" "${VERSION}"
    WriteRegStr HKCU "${UNINSTKEY}" "Publisher" "${PUBLISHER}"
    WriteRegStr HKCU "${UNINSTKEY}" "URLInfoAbout" "${HOMEPAGE}"
    WriteRegStr HKCU "${UNINSTKEY}" "DisplayIcon" "$INSTDIR\${EXE}"
    WriteRegStr HKCU "${UNINSTKEY}" "InstallLocation" "$INSTDIR"
    WriteRegStr HKCU "${UNINSTKEY}" "UninstallString" '"$INSTDIR\uninstall.exe"'
    WriteRegStr HKCU "${UNINSTKEY}" "QuietUninstallString" '"$INSTDIR\uninstall.exe" /S'
    WriteRegDWORD HKCU "${UNINSTKEY}" "NoModify" 1
    WriteRegDWORD HKCU "${UNINSTKEY}" "NoRepair" 1
    ${GetSize} "$INSTDIR" "/S=0K" $0 $1 $2
    WriteRegDWORD HKCU "${UNINSTKEY}" "EstimatedSize" $0
SectionEnd

Section "Uninstall"
    Call un.CloseApp
    ; only what the installer put there, the folder may hold files of the user
    Delete "$INSTDIR\${EXE}"
    Delete "$INSTDIR\uninstall.exe"
    RMDir /r "$INSTDIR\sidecar"
    RMDir /r "$INSTDIR\node"
    RMDir "$INSTDIR"

    Delete "$SMPROGRAMS\${PRODUCT}.lnk"
    DeleteRegKey HKCU "${UNINSTKEY}"
    DeleteRegKey HKCU "${REGKEY}"
    DeleteRegKey /ifempty HKCU "Software\Postal Systems"

    ; the cache of the message preview
    RMDir /r "$LOCALAPPDATA\ImapKitDesktop"
    MessageBox MB_YESNO|MB_ICONQUESTION "Also remove the mailboxes and settings of ${PRODUCT}?" /SD IDNO IDNO +2
    RMDir /r "$APPDATA\ImapKitDesktop"
SectionEnd
