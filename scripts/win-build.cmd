@echo off
rem Builds the sidecar dependencies and the native host, run on Windows (scripts/win-build.sh calls it over SSH)
setlocal
rem the C++ tools of Visual Studio 2022, Build Tools or a full edition
for /f "usebackq delims=" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set VSDIR=%%i
if not defined VSDIR (
    echo Visual Studio with the C++ tools was not found
    exit /b 1
)
call "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
cd /d "%~dp0.."
rem a running copy holds the exe open, the link would fail
taskkill /im ImapKitDesktop.exe /f >nul 2>&1
pushd sidecar
call npm install --omit=dev --no-audit --no-fund --loglevel=error || exit /b 1
popd
cmake -S host-win -B build -G Ninja -DCMAKE_BUILD_TYPE=Release || exit /b 1
cmake --build build || exit /b 1
