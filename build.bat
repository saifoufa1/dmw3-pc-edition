@echo off
rem Builds the game and tools with MSVC from Visual Studio 2022/2026 (CMake + Ninja ship with VS).
rem Usage: build.bat [Release|Debug]
setlocal
set "CONFIG=%~1"
if "%CONFIG%"=="" set "CONFIG=Release"

if defined VCToolsInstallDir goto build
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
  echo vswhere.exe not found - install Visual Studio with "Desktop development with C++".
  exit /b 1
)
"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath > "%TEMP%\dmw3_vsdir.txt"
set /p VSDIR=<"%TEMP%\dmw3_vsdir.txt"
if not defined VSDIR (
  echo Visual Studio with C++ tools not found.
  exit /b 1
)
call "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1

:build
cmake -S "%~dp0." -B "%~dp0build" -G Ninja -DCMAKE_BUILD_TYPE=%CONFIG% || exit /b 1
cmake --build "%~dp0build" || exit /b 1
rem NPC and door tables come from the player's own game data (never in git): extract them once.
if not exist "%~dp0data\npcs\S210.npc" "%~dp0build\dw3check.exe" --extract-npcs "%~dp0data" >nul
if not exist "%~dp0data\npcs\S210.npc" echo NOTE: data\npcs not extracted - run: build\dw3check.exe --extract-npcs data
echo.
echo Built: %~dp0build\dmw3.exe  and  %~dp0build\dw3check.exe
