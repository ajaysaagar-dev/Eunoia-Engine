@echo off
setlocal enabledelayedexpansion
set "PROJECT_DIR=%~dp0"
if "%PROJECT_DIR:~-1%"=="\" set "PROJECT_DIR=%PROJECT_DIR:~0,-1%"

:: ============================================================================
:: Eunoia Engine .NET Build Driver
:: Automatically compiles and launches the .NET EunoiaBuildTool
:: ============================================================================

set "BUILD_TOOL_DIR=%PROJECT_DIR%\tools\EunoiaBuildTool"
set "BUILD_TOOL_EXE=%BUILD_TOOL_DIR%\bin\EunoiaBuilder.exe"
set "BUILD_TOOL_DLL=%BUILD_TOOL_DIR%\bin\Release\netcoreapp3.0\EunoiaBuilder.dll"
set "BUILD_TOOL_SRC=%BUILD_TOOL_DIR%\Program.cs"

:: Ensure output directory for tool exists
if not exist "%BUILD_TOOL_DIR%\bin" mkdir "%BUILD_TOOL_DIR%\bin"

:: 1. Check if dotnet is available
set "DOTNET_CMD="
where dotnet >nul 2>&1
if !ERRORLEVEL! equ 0 set "DOTNET_CMD=dotnet"

:: 2. Check for .NET Framework CSC compiler
set "CSC_CMD="
if exist "C:\Windows\Microsoft.NET\Framework64\v4.0.30319\csc.exe" (
    set "CSC_CMD=C:\Windows\Microsoft.NET\Framework64\v4.0.30319\csc.exe"
) else if exist "C:\Windows\Microsoft.NET\Framework\v4.0.30319\csc.exe" (
    set "CSC_CMD=C:\Windows\Microsoft.NET\Framework\v4.0.30319\csc.exe"
) else (
    where csc >nul 2>&1
    if !ERRORLEVEL! equ 0 set "CSC_CMD=csc"
)

:: Always ensure clean fresh build by deleting previous intermediates and binaries
if exist "%PROJECT_DIR%\Build\Intermediates" (
    rmdir /s /q "%PROJECT_DIR%\Build\Intermediates" >nul 2>&1
)
if exist "%PROJECT_DIR%\Build\Engine\Eunoia-Editor.exe" (
    del /f /q "%PROJECT_DIR%\Build\Engine\Eunoia-Editor.exe" >nul 2>&1
)

:: Compile .NET build tool if binary is missing or source is newer
set "NEED_COMPILE=0"
if not exist "%BUILD_TOOL_EXE%" (
    if not exist "%BUILD_TOOL_DLL%" set "NEED_COMPILE=1"
)
if exist "%BUILD_TOOL_EXE%" (
    for /f %%A in ('powershell -NoProfile -Command "(Get-Item '%BUILD_TOOL_SRC%').LastWriteTime -gt (Get-Item '%BUILD_TOOL_EXE%').LastWriteTime"') do (
        if "%%A"=="True" (
            del /f /q "%BUILD_TOOL_EXE%" >nul 2>&1
            set "NEED_COMPILE=1"
        )
    )
)

if "!NEED_COMPILE!"=="1" (
    if defined CSC_CMD (
        "%CSC_CMD%" /nologo /o /t:exe /out:"%BUILD_TOOL_EXE%" "%BUILD_TOOL_SRC%" >nul 2>&1
    ) else if defined DOTNET_CMD (
        "%DOTNET_CMD%" build "%BUILD_TOOL_DIR%\EunoiaBuildTool.csproj" -c Release >nul 2>&1
    )
)

:: Run .NET Build Tool
if exist "%BUILD_TOOL_EXE%" (
    "%BUILD_TOOL_EXE%" %*
    exit /b !ERRORLEVEL!
) else if exist "%BUILD_TOOL_DLL%" (
    "%DOTNET_CMD%" "%BUILD_TOOL_DLL%" %*
    exit /b !ERRORLEVEL!
) else if defined DOTNET_CMD (
    "%DOTNET_CMD%" run --project "%BUILD_TOOL_DIR%\EunoiaBuildTool.csproj" -c Release -- %*
    exit /b !ERRORLEVEL!
)

:: Fallback if no .NET runtime is installed on system
echo [WARNING] .NET not found. Falling back to native batch compilation...
set "COMPILER=%PROJECT_DIR%\tools\w64devkit\bin\g++.exe"
if not exist "%COMPILER%" set "COMPILER=g++"

"%COMPILER%" -std=c++20 -O2 ^
    "-I%PROJECT_DIR%\Engine\EngineCore\Include" ^
    "-I%PROJECT_DIR%\Editor\Editor\Include" ^
    -o "%PROJECT_DIR%\Build\Engine\Eunoia-Editor.exe"
exit /b %ERRORLEVEL%
