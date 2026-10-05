@echo off
setlocal EnableDelayedExpansion
cd /d "%~dp0"

REM Search for existing engine binary across configurations
set "EXE="
if exist "BUILD\x64\Release\Editor_Windows\Editor_Windows.exe" set "EXE=BUILD\x64\Release\Editor_Windows\Editor_Windows.exe"
if not defined EXE if exist "BUILD\x64\Release\Editor_Windows.exe" set "EXE=BUILD\x64\Release\Editor_Windows.exe"
if not defined EXE if exist "BUILD\x64\Debug\Editor_Windows\Editor_Windows.exe" set "EXE=BUILD\x64\Debug\Editor_Windows\Editor_Windows.exe"
if not defined EXE if exist "BUILD\x64\Debug\Editor_Windows.exe" set "EXE=BUILD\x64\Debug\Editor_Windows.exe"
if not defined EXE if exist "Editor\Editor_Windows\x64\Release\Editor_Windows.exe" set "EXE=Editor\Editor_Windows\x64\Release\Editor_Windows.exe"

if not defined EXE (
    echo [ERROR] Editor binary not found in BUILD directories.
    echo Run Build.bat to compile the binary first.
    exit /b 1
)

echo Launching Eunoia Editor directly using workspace source assets: !EXE!
start "" "!EXE!" %*
exit /b 0
