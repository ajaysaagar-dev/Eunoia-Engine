@echo off
setlocal enabledelayedexpansion
set "PROJECT_DIR=%~dp0"
if "%PROJECT_DIR:~-1%"=="\" set "PROJECT_DIR=%PROJECT_DIR:~0,-1%"

:: Prepend pinned w64devkit toolchain to PATH if present
if exist "%PROJECT_DIR%\tools\w64devkit\bin" (
    set "PATH=%PROJECT_DIR%\tools\w64devkit\bin;!PATH!"
)

set "TARGET_EXE=%PROJECT_DIR%\Build\dev\Editor\Editor\Eunoia-Editor.exe"
if not exist "!TARGET_EXE!" set "TARGET_EXE=%PROJECT_DIR%\Build\dev\bin\Eunoia-Editor.exe"
if not exist "!TARGET_EXE!" set "TARGET_EXE=%PROJECT_DIR%\Build\Engine\Eunoia-Editor.exe"

if not exist "!TARGET_EXE!" (
    echo [INFO] Eunoia-Editor.exe not found. Building first...
    call "%PROJECT_DIR%\Build.bat" dev
    if !ERRORLEVEL! neq 0 (
        echo [ERROR] Build step failed.
        exit /b !ERRORLEVEL!
    )
)

if not exist "!TARGET_EXE!" set "TARGET_EXE=%PROJECT_DIR%\Build\dev\Editor\Editor\Eunoia-Editor.exe"
if not exist "!TARGET_EXE!" set "TARGET_EXE=%PROJECT_DIR%\Build\dev\bin\Eunoia-Editor.exe"

if exist "!TARGET_EXE!" (
    echo [INFO] Starting Eunoia-Editor from !TARGET_EXE!...
    cd /d "%PROJECT_DIR%"
    start "" "!TARGET_EXE!" %*
    exit /b 0
)

echo [ERROR] Could not find or build Eunoia-Editor.exe.
exit /b 1
