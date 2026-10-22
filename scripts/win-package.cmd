@echo off
rem Builds the app and stages what the installer installs in dist\ImapKitDesktop: the exe, the sidecar with its
rem production dependencies and node.exe, so the target machine needs nothing else (WebView2 comes with Windows 11).
rem dist\ImapKitDesktop-win-x64.zip is the same as a portable zip
setlocal
call "%~dp0win-build.cmd" || exit /b 1
cd /d "%~dp0.."
set OUT=dist\ImapKitDesktop
if exist dist rmdir /s /q dist
mkdir %OUT%\sidecar %OUT%\node || exit /b 1
copy /y build\ImapKitDesktop.exe %OUT%\ >nul || exit /b 1
copy /y sidecar\package.json %OUT%\sidecar\ >nul || exit /b 1
xcopy /e /i /q sidecar\src %OUT%\sidecar\src >nul || exit /b 1
xcopy /e /i /q sidecar\node_modules %OUT%\sidecar\node_modules >nul || exit /b 1
rem a pinned Node.js LTS, checked against the checksums of nodejs.org
set NODE_VERSION=v24.22.1
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0fetch-node.ps1" -Version %NODE_VERSION% -Out %OUT%\node\node.exe || exit /b 1
powershell -NoProfile -Command "Compress-Archive -Path '%OUT%' -DestinationPath 'dist\ImapKitDesktop-win-x64.zip' -Force" || exit /b 1
echo dist\ImapKitDesktop-win-x64.zip
