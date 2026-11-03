@echo off
setlocal
cd /d "%~dp0"
if not defined VSCMD_VER (
  for /f "usebackq tokens=*" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSROOT=%%i"
  if not defined VSROOT (echo Install Visual Studio C++ Build Tools and Windows SDK. & exit /b 1)
)
if not defined VSCMD_VER call "%VSROOT%\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
if not exist build mkdir build
pushd src
rc /nologo /fo ..\build\app.res app.rc
set "RC_RESULT=%ERRORLEVEL%"
popd
if not "%RC_RESULT%"=="0" exit /b 1
cl /nologo /std:c++17 /EHsc /O2 /MT /W4 /utf-8 /DUNICODE /D_UNICODE src\tray.cpp src\bluetooth.cpp build\app.res /Fo:build\ /Fe:build\S-XMTray.exe /link /SUBSYSTEM:WINDOWS /MANIFEST:EMBED /MANIFESTINPUT:src\app.manifest Ws2_32.lib Bthprops.lib User32.lib Shell32.lib Advapi32.lib Ole32.lib Comctl32.lib Gdi32.lib Uxtheme.lib Dwmapi.lib Gdiplus.lib
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /EHsc /O2 /MT /W4 /utf-8 /DUNICODE /D_UNICODE src\cli.cpp src\bluetooth.cpp /Fo:build\ /Fe:build\S-XMTray-cli.exe /link Ws2_32.lib Bthprops.lib
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /EHsc /O2 /MT /W4 /utf-8 tests\protocol_tests.cpp /Fo:build\ /Fe:build\protocol-tests.exe
if errorlevel 1 exit /b 1
build\protocol-tests.exe
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /EHsc /O2 /MT /W4 /utf-8 tests\hotkey_tests.cpp /Fo:build\ /Fe:build\hotkey-tests.exe /link User32.lib
if errorlevel 1 exit /b 1
build\hotkey-tests.exe
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /EHsc /O2 /MT /W4 /utf-8 tests\feature_tests.cpp /Fo:build\ /Fe:build\feature-tests.exe
if errorlevel 1 exit /b 1
build\feature-tests.exe
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /EHsc /O2 /MT /W4 /utf-8 /DUNICODE /D_UNICODE tests\ui_tests.cpp src\bluetooth.cpp build\app.res /Fo:build\ /Fe:build\ui-tests.exe /link /MANIFEST:EMBED /MANIFESTINPUT:src\app.manifest Ws2_32.lib Bthprops.lib User32.lib Shell32.lib Advapi32.lib Ole32.lib Comctl32.lib Gdi32.lib Uxtheme.lib Dwmapi.lib Gdiplus.lib
if errorlevel 1 exit /b 1
build\ui-tests.exe
if errorlevel 1 exit /b 1