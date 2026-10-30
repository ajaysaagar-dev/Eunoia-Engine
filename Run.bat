@echo off
setlocal enabledelayedexpansion
cd /d "%~dp0"

set "TARGET_EXE=..\Build\Engine\Eunoia-Editor.exe"
if not exist "!TARGET_EXE!" set "TARGET_EXE=Build\Engine\Eunoia-Editor.exe"

if not exist "!TARGET_EXE!" (
    echo [INFO] Eunoia-Editor.exe not found. Building first...
    call Build.bat
    if %ERRORLEVEL% neq 0 exit /b %ERRORLEVEL%
)

if exist "..\Build\Engine\Eunoia-Editor.exe" (
    echo [INFO] Starting Eunoia-Editor from ..\Build\Engine...
    cd /d "%~dp0..\Build\Engine"
    start "" "Eunoia-Editor.exe" %*
    exit /b 0
)

if exist "Build\Engine\Eunoia-Editor.exe" (
    echo [INFO] Starting Eunoia-Editor from Build\Engine...
    cd /d "%~dp0Build\Engine"
    start "" "Eunoia-Editor.exe" %*
    exit /b 0
)

echo [ERROR] Could not find or build Eunoia-Editor.exe.
exit /b 1