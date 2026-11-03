@echo off
setlocal enabledelayedexpansion
set "PROJECT_DIR=%~dp0"
if "%PROJECT_DIR:~-1%"=="\" set "PROJECT_DIR=%PROJECT_DIR:~0,-1%"

:: Prepend pinned w64devkit toolchain to PATH if present
if exist "%PROJECT_DIR%\tools\w64devkit\bin" (
    set "PATH=%PROJECT_DIR%\tools\w64devkit\bin;!PATH!"
)

set "PRESET=dev"
if not "%~1"=="" (
    set "PRESET=%~1"
)

echo [BUILD] Configuring CMake preset '%PRESET%'...
cmake --preset %PRESET%
if !ERRORLEVEL! neq 0 (
    echo [ERROR] CMake configuration failed with error !ERRORLEVEL!.
    exit /b !ERRORLEVEL!
)

echo [BUILD] Building CMake preset '%PRESET%'...
cmake --build --preset %PRESET%
if !ERRORLEVEL! neq 0 (
    echo [ERROR] CMake build failed with error !ERRORLEVEL!.
    exit /b !ERRORLEVEL!
)

echo [BUILD] Build finished successfully.
exit /b 0
