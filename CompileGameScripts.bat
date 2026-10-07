@echo off
setlocal enabledelayedexpansion

:: ============================================================================
:: CompileGameScripts.bat
:: Compiles user behaviour scripts and packages standalone game executable.
:: Usage: CompileGameScripts.bat [ProjectPath]
:: ============================================================================

set "ENGINE_ROOT=%~dp0"
if "%ENGINE_ROOT:~-1%"=="\" set "ENGINE_ROOT=%ENGINE_ROOT:~0,-1%"

set "PROJ_PATH=%~1"
if "%PROJ_PATH%"=="" (
    set "PROJ_PATH=%ENGINE_ROOT%\..\Projects\Unknown-13-Test"
)

:: Canonicalize project path
for %%i in ("%PROJ_PATH%") do set "PROJ_PATH=%%~fi"

:: Run through .NET Build Tool
if exist "%ENGINE_ROOT%\Build.bat" (
    call "%ENGINE_ROOT%\Build.bat" --game-only --project "%PROJ_PATH%"
    exit /b !ERRORLEVEL!
)
