@echo off
:: build_windows.bat
:: -----------------
:: Builds pdf_to_jpg.exe on Windows using PyInstaller inside an
:: isolated virtual environment so system packages never pollute the build.
::
:: Prerequisites (one-time):
::   - Python 3.9+ installed and on PATH
::   - Poppler for Windows installed and on PATH
::       Download: https://github.com/oschwartz10612/poppler-windows/releases
::       Extract and add the \Library\bin folder to your system PATH
::
:: Usage:
::   Double-click this file, or run from the project folder:
::     build_windows.bat
::
:: Output:
::   dist\pdf_to_jpg\pdf_to_jpg.exe   (and supporting files)
::
:: Next step (optional):
::   Compile build_installer.iss with Inno Setup to produce a
::   single Setup.exe installer for distribution.

setlocal enabledelayedexpansion

echo.
echo ============================================================
echo  PDF to JPG Converter -- Windows Build Script
echo ============================================================
echo.

:: ------------------------------------------------------------------
:: 1. Verify Python is available
:: ------------------------------------------------------------------
where python >nul 2>&1
if %errorlevel% neq 0 (
    echo ERROR: Python not found on PATH.
    echo Install Python 3.9+ from https://python.org and try again.
    pause
    exit /b 1
)

for /f "tokens=*" %%v in ('python --version 2^>^&1') do set PYVER=%%v
echo Found: %PYVER%
echo.

:: ------------------------------------------------------------------
:: 2. Create a clean virtual environment for the build
:: ------------------------------------------------------------------
echo [1/6] Creating isolated build environment...
if exist build_venv (
    echo       Removing old build_venv...
    rmdir /s /q build_venv
)
python -m venv build_venv
if %errorlevel% neq 0 (
    echo ERROR: Failed to create virtual environment.
    pause
    exit /b 1
)
echo       Done.
echo.

:: ------------------------------------------------------------------
:: 3. Activate venv and upgrade pip
:: ------------------------------------------------------------------
echo [2/6] Activating venv and upgrading pip...
call build_venv\Scripts\activate.bat
python -m pip install --upgrade pip --quiet
echo       Done.
echo.

:: ------------------------------------------------------------------
:: 4. Install runtime dependencies
:: ------------------------------------------------------------------
echo [3/6] Installing runtime dependencies...
pip install pdf2image Pillow --quiet
if %errorlevel% neq 0 (
    echo ERROR: Failed to install dependencies.
    pause
    exit /b 1
)
echo       Done.
echo.

:: ------------------------------------------------------------------
:: 5. Install PyInstaller
:: ------------------------------------------------------------------
echo [4/6] Installing PyInstaller...
pip install pyinstaller --quiet
if %errorlevel% neq 0 (
    echo ERROR: Failed to install PyInstaller.
    pause
    exit /b 1
)
echo       Done.
echo.

:: ------------------------------------------------------------------
:: 6. Clean previous build artifacts
:: ------------------------------------------------------------------
echo [5/6] Cleaning previous build output...
if exist build rmdir /s /q build
if exist dist  rmdir /s /q dist
echo       Done.
echo.

:: ------------------------------------------------------------------
:: 7. Run PyInstaller
:: ------------------------------------------------------------------
echo [6/6] Running PyInstaller...
echo.
pyinstaller pdf_to_jpg.spec
if %errorlevel% neq 0 (
    echo.
    echo ERROR: PyInstaller failed. See output above for details.
    call build_venv\Scripts\deactivate.bat 2>nul
    pause
    exit /b 1
)
echo.

:: ------------------------------------------------------------------
:: 8. Deactivate venv
:: ------------------------------------------------------------------
call build_venv\Scripts\deactivate.bat 2>nul

:: ------------------------------------------------------------------
:: 9. Report success
:: ------------------------------------------------------------------
echo.
echo ============================================================
echo  BUILD SUCCEEDED
echo.
echo  Executable:  dist\pdf_to_jpg\pdf_to_jpg.exe
echo  Full bundle: dist\pdf_to_jpg\
echo.
echo  IMPORTANT: Poppler must be installed on the user's machine
echo  and on their system PATH for the converter to work.
echo  Direct users to:
echo    https://github.com/oschwartz10612/poppler-windows/releases
echo.
echo  Optional next step:
echo    Compile build_installer.iss with Inno Setup to produce a
echo    single-click Setup.exe that also reminds users about Poppler.
echo    (Free: https://jrsoftware.org/isinfo.php)
echo ============================================================
echo.
pause
