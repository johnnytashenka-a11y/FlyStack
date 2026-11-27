@echo off
setlocal
cd /d "%~dp0"

rem Find a Visual Studio 2022+ installation with the native C++ toolset.
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
set "VSINSTALL="
if exist "%VSWHERE%" (
  for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSINSTALL=%%i"
)

if not defined VSINSTALL if exist "%ProgramFiles(x86)%\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" (
  set "VSINSTALL=%ProgramFiles(x86)%\Microsoft Visual Studio\2022\BuildTools"
)
if not defined VSINSTALL if exist "%ProgramFiles%\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" (
  set "VSINSTALL=%ProgramFiles%\Microsoft Visual Studio\2022\Community"
)
if not defined VSINSTALL if exist "%ProgramFiles(x86)%\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" (
  set "VSINSTALL=%ProgramFiles(x86)%\Microsoft Visual Studio\2022\Community"
)

if defined VSINSTALL (
  call "%VSINSTALL%\VC\Auxiliary\Build\vcvars64.bat"
  if errorlevel 1 exit /b 1
) else (
  where cl >nul 2>nul
  if errorlevel 1 (
    echo [ERROR] Visual Studio C++ build tools ^(v143, x64^) not found.
    echo Install "Desktop development with C++" in Visual Studio 2022 or Build Tools.
    exit /b 1
  )
)

cl /O2 /EHsc /std:c++17 /utf-8 /I . /I vendor\include main.cpp taskbar_modes.cpp tray_host.cpp preview_host.cpp /link /LIBPATH:vendor\lib\x64 WebView2Loader.dll.lib User32.lib Gdi32.lib Ole32.lib Shell32.lib Gdiplus.lib comctl32.lib dwmapi.lib /OUT:..\FlyStackTaskbar.exe
if errorlevel 1 exit /b 1

copy /Y vendor\bin\x64\WebView2Loader.dll ..\WebView2Loader.dll >nul
if errorlevel 1 exit /b 1

echo Build finished.
endlocal
