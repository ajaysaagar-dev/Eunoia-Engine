@echo off
setlocal
cd /d "%~dp0"

set "EXE=BUILD\x64\Release\Editor_Windows\Editor_Windows.exe"
if not exist "%EXE%" (
    set "EXE=BUILD\x64\Release\Editor_Windows.exe"
)

if not exist "%EXE%" (
    echo Executable not found. Running build...
    call Build.bat
    if %ERRORLEVEL% neq 0 exit /b %ERRORLEVEL%
)

if not exist "%EXE%" (
    set "EXE=BUILD\x64\Release\Editor_Windows\Editor_Windows.exe"
)

echo Launching WickedEngine Editor from: %EXE%
start "" "%EXE%"
exit /b 0
