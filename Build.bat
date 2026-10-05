@echo off
setlocal

REM Terminate lingering processes that hold file locks on output binaries
taskkill /f /im Editor_Windows.exe >nul 2>&1
taskkill /f /im cl.exe >nul 2>&1
taskkill /f /im link.exe >nul 2>&1
taskkill /f /im mspdbsrv.exe >nul 2>&1

set "MSBUILD="
if exist "C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\amd64\MSBuild.exe" set "MSBUILD=C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\amd64\MSBuild.exe"
if exist "C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\amd64\MSBuild.exe" set "MSBUILD=C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\amd64\MSBuild.exe"

if not defined MSBUILD (
    for /f "usebackq tokens=*" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -requires Microsoft.Component.MSBuild -find MSBuild\**\Bin\MSBuild.exe 2^>nul`) do (
        set "MSBUILD=%%i"
    )
)

if not defined MSBUILD (
    echo [ERROR] MSBuild.exe not found!
    exit /b 1
)

set "SLN_FILE=%~dp0EunoiaEngine.sln"
if not exist "%SLN_FILE%" set "SLN_FILE=%~dp0WickedEngine.sln"

if /i "%~1"=="clean" goto do_clean
if /i "%~1"=="run" goto do_fast_run
goto do_fast

:do_clean
echo Cleaning Editor_Windows build artifacts...
"%MSBUILD%" "%SLN_FILE%" /t:Editor_Windows:Clean /p:Configuration=Release /p:Platform=x64 /m /nr:false /v:m
goto end

:do_fast_run
echo Building modified Editor files...
"%MSBUILD%" "%SLN_FILE%" /t:Editor_Windows /p:Configuration=Release /p:Platform=x64 /m /p:BuildInParallel=true /nr:false /v:m
if %ERRORLEVEL% neq 0 (
    echo [FAILED] Build failed with error code %ERRORLEVEL%
    exit /b %ERRORLEVEL%
)
echo [SUCCESS] Build succeeded!
call "%~dp0Run.bat"
goto end

:do_fast
echo Building modified Editor files...
"%MSBUILD%" "%SLN_FILE%" /t:Editor_Windows /p:Configuration=Release /p:Platform=x64 /m /p:BuildInParallel=true /nr:false /v:m
if %ERRORLEVEL% neq 0 (
    echo [FAILED] Build failed with error code %ERRORLEVEL%
    exit /b %ERRORLEVEL%
)
echo [SUCCESS] Build succeeded!
goto end

:end
exit /b 0
