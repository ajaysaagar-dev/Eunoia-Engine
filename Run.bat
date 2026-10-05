@echo off
setlocal EnableDelayedExpansion
cd /d "%~dp0"

REM Search for existing engine binary in Main Build (BUILD) directory
set "EXE="
if exist "..\Build (BUILD)\Eunoia-Editor.exe" set "EXE=..\Build (BUILD)\Eunoia-Editor.exe"
if not defined EXE if exist "BUILD\x64\Release\Editor_Windows\Eunoia-Editor.exe" set "EXE=BUILD\x64\Release\Editor_Windows\Eunoia-Editor.exe"
if not defined EXE if exist "BUILD\x64\Release\Eunoia-Editor.exe" set "EXE=BUILD\x64\Release\Eunoia-Editor.exe"
if not defined EXE if exist "..\Build (BUILD)\Editor_Windows.exe" set "EXE=..\Build (BUILD)\Editor_Windows.exe"

if not defined EXE (
    echo [ERROR] Editor binary not found in Build (BUILD) directory.
    echo Run Build.bat to compile the binary first.
    exit /b 1
)

echo Launching Eunoia Editor directly from: !EXE!
start "" "!EXE!" %*
exit /b 0

