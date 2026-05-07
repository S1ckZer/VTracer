@echo off
setlocal enableextensions

REM Single-click builder for VTracer.
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
    echo (rename .nupkg to .zip and extract).
    echo.
    popd
    exit /b 1
)

echo Configuring (Release x64) ...
cmake -B build -A x64 -DWEBVIEW2_SDK="%WEBVIEW2_SDK%" || goto :error

echo Building ...
cmake --build build --config Release || goto :error

echo.
echo Done. Run:  build\Release\VTracer.exe
popd
exit /b 0

:error
echo.
echo Build failed.
popd
exit /b 1
