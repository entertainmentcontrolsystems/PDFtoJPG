@echo off
:: build_windows.bat — Build ECS PDF Converter on Windows
:: Produces: build\Release\ECS PDF Converter.exe (with Qt DLLs via windeployqt)
::
:: Prerequisites:
::   - Qt 6.6+ (install via Qt Online Installer, select MSVC build)
::   - Visual Studio 2022+ with C++ build tools
::   - CMake 3.21+
::
:: For vector output (optional):
::   - Poppler for Windows (pdftocairo.exe)
::   - Potrace for Windows (potrace.exe)
::   Bundle these in a "tools\" subfolder next to the .exe

setlocal enabledelayedexpansion

echo.
echo ============================================================
echo  ECS PDF Converter -- Windows Build
echo ============================================================
echo.

:: ── 1. Check prerequisites ──────────────────────────────────────
echo [1/5] Checking prerequisites...

where cmake >nul 2>&1
if %errorlevel% neq 0 (
    echo ERROR: CMake not found. Install from https://cmake.org
    exit /b 1
)

where cl >nul 2>&1
if %errorlevel% neq 0 (
    echo ERROR: MSVC compiler not found.
    echo Run from "x64 Native Tools Command Prompt for VS 2022"
    exit /b 1
)

for /f "tokens=*" %%v in ('cmake --version 2^>^&1') do set CMVER=%%v
echo Found: %CMVER%
echo.

:: ── 2. Find Qt6 ────────────────────────────────────────────────
echo [2/5] Locating Qt6...

set "QT_DIR="
for /f "delims=" %%i in ('dir /b /ad "C:\Qt\6.*" 2^>nul') do (
    if exist "C:\Qt\%%i\msvc*\lib\Qt6Core.lib" (
        set "QT_DIR=C:\Qt\%%i\msvc*"
        set "QT_VERSION=%%i"
    )
)

if not defined QT_VERSION (
    for /f "delims=" %%i in ('dir /b /ad "C:\Qt\6.*" 2^>nul') do (
        for /f "delims=" %%j in ('dir /b /ad "C:\Qt\%%i" 2^>nul') do (
            if exist "C:\Qt\%%i\%%j\lib\Qt6Core.lib" (
                set "QT_DIR=C:\Qt\%%i\%%j"
                set "QT_VERSION=%%i"
            )
        )
    )
)

if not defined QT_VERSION (
    echo ERROR: Qt6 not found in C:\Qt\
    echo Install Qt6 via the Qt Online Installer.
    exit /b 1
)

:: Resolve wildcard in QT_DIR
for /f "delims=" %%d in ('dir /b /ad "%QT_DIR%" 2^>nul') do (
    if exist "%QT_DIR%\%%d\lib\Qt6Core.lib" set "QT_DIR=%QT_DIR%\%%d"
)

echo   Qt: %QT_VERSION% at %QT_DIR%
echo.

:: ── 3. Clean build ──────────────────────────────────────────────
echo [3/5] Cleaning previous build...
if exist build rmdir /s /q build
mkdir build
echo   Done.
echo.

:: ── 4. CMake configure + build ──────────────────────────────────
echo [4/5] Building...
cd build

set "CMAKE_PREFIX_PATH=%QT_DIR%"

cmake -G "Ninja" -DCMAKE_BUILD_TYPE=Release .. 2>&1
if %errorlevel% neq 0 (
    echo ERROR: CMake configuration failed.
    exit /b 1
)

cmake --build . --config Release 2>&1
if %errorlevel% neq 0 (
    echo ERROR: Build failed.
    exit /b 1
)

echo.

:: ── 5. Deploy ──────────────────────────────────────────────────
echo [5/5] Deploying Qt dependencies...

set "WINDEPLOYQT=%QT_DIR%\bin\windeployqt.exe"
if not exist "%WINDEPLOYQT%" (
    echo WARNING: windeployqt not found at %WINDEPLOYQT%
    echo The .exe is at build\Release\ECS PDF Converter.exe
    echo You'll need to manually run windeployqt or distribute Qt DLLs.
) else (
    "%WINDEPLOYQT%" --release --no-translations --no-system-d3d-compiler --no-opengl-sw "Release\ECS PDF Converter.exe"
    echo   Deployed.
)

echo.
echo ============================================================
echo  BUILD SUCCEEDED
echo.
echo  Executable: build\Release\ECS PDF Converter.exe
echo.
echo  For vector output, bundle these in build\Release\tools\:
echo    - pdftocairo.exe (from Poppler for Windows)
echo    - potrace.exe (from https://potrace.sourceforge.net/)
echo ============================================================
echo.