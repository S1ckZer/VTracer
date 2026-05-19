@echo off
setlocal enableextensions

REM Single-click builder for VTracer (Release x64, standalone).
REM Output: build-release\Release\VTracer.exe and VTracer-0.1.2-win-x64.zip
REM Requirements: Visual Studio 2022 Build Tools (C++), CMake, Node.js, WebView2 SDK.

set "SCRIPT_DIR=%~dp0"
pushd "%SCRIPT_DIR%"

if "%WEBVIEW2_SDK%"=="" (
    if exist "%SCRIPT_DIR%third_party\webview2\build\native\include\WebView2.h" (
        set "WEBVIEW2_SDK=%SCRIPT_DIR%third_party\webview2"
    )
)

if "%WEBVIEW2_SDK%"=="" (
    echo.
    echo [error] WEBVIEW2_SDK is not set and third_party\webview2 is not present.
    echo.
    echo Either:
    echo   1. Set the env var:  set WEBVIEW2_SDK=C:\path\to\unpacked\webview2
    echo   2. Or put the unpacked NuGet at:  %SCRIPT_DIR%third_party\webview2
    echo.
    echo Get it from https://www.nuget.org/packages/Microsoft.Web.WebView2
    echo ^(rename .nupkg to .zip and extract^).
    echo.
    popd
    exit /b 1
)

REM Pick the newest installed Windows 10 SDK. VS otherwise sometimes falls back
REM to the legacy 8.1 SDK, which does not parse modern WRL headers.
for /f "usebackq delims=" %%V in (`powershell -NoProfile -Command "(Get-ChildItem 'C:\Program Files (x86)\Windows Kits\10\Include' -Directory | Sort-Object Name | Select-Object -Last 1).Name"`) do set "WIN_SDK_VERSION=%%V"
if "%WIN_SDK_VERSION%"=="" (
    echo [error] No Windows 10 SDK found under C:\Program Files ^(x86^)\Windows Kits\10\Include.
    popd
    exit /b 1
)
echo Using Windows SDK: %WIN_SDK_VERSION%

set "BUILD_DIR=%SCRIPT_DIR%build-release"

echo Configuring (Release x64, static MSVC runtime) ...
cmake -B "%BUILD_DIR%" -G "Visual Studio 17 2022" -A x64 ^
    -DCMAKE_SYSTEM_VERSION=%WIN_SDK_VERSION% ^
    -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded ^
    -DWEBVIEW2_SDK="%WEBVIEW2_SDK%" || goto :error

echo Building ...
cmake --build "%BUILD_DIR%" --config Release || goto :error

echo Packaging ...
set "STAGE=%BUILD_DIR%\package\VTracer-0.1.2-win-x64"
set "ZIP=%SCRIPT_DIR%VTracer-0.1.2-win-x64.zip"
if exist "%BUILD_DIR%\package" rmdir /s /q "%BUILD_DIR%\package"
mkdir "%STAGE%" || goto :error
copy /y "%BUILD_DIR%\Release\VTracer.exe" "%STAGE%\" >nul || goto :error
xcopy /e /i /y /q "%BUILD_DIR%\Release\catalogs" "%STAGE%\catalogs" >nul || goto :error
if exist "%SCRIPT_DIR%README.md" copy /y "%SCRIPT_DIR%README.md" "%STAGE%\" >nul
if exist "%ZIP%" del /q "%ZIP%"
powershell -NoProfile -Command "Compress-Archive -Path '%STAGE%\*' -DestinationPath '%ZIP%'" || goto :error

echo.
echo Done.
echo   Exe:  %BUILD_DIR%\Release\VTracer.exe
echo   Zip:  %ZIP%
echo.
echo The exe is statically linked (no VC++ Redistributable required).
echo Target machine still needs the Microsoft Edge WebView2 Runtime,
echo which is pre-installed on Windows 10 (recent updates) and Windows 11.
popd
exit /b 0

:error
echo.
echo Build failed.
popd
exit /b 1
