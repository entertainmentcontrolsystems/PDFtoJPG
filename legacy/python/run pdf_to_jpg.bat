@echo off
cd /d "%~dp0"

call venv\Scripts\activate.bat
if errorlevel 1 (
    echo ERROR: Could not activate virtual environment.
    pause
    exit /b 1
)

echo Starting PDF_to_JPG...
python pdf_to_jpg.py
if errorlevel 1 (
    echo.
    echo PDF_to_JPG exited with an error. See above for details.
)
pause