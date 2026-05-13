@echo off
REM ============================================
REM FluxGen Gateway v1.4.0 - Web Flasher Launcher
REM Opens browser-based firmware flasher
REM ============================================

echo.
echo ========================================
echo   FluxGen Gateway v1.4.0
echo   Starting Web Flasher...
echo ========================================
echo.

REM Auto-package firmware if not already done
if not exist "%~dp0firmware\merged_firmware.bin" (
    echo [INFO] Merged firmware not found, packaging now...
    echo.
    call "%~dp0package_firmware.bat"
    if not exist "%~dp0firmware\merged_firmware.bin" (
        echo [ERROR] Packaging failed. Run idf.py build first.
        echo.
        pause
        exit /b 1
    )
)

REM Find Python
set PYTHON_CMD=
python --version >nul 2>nul
if %ERRORLEVEL% EQU 0 (
    set PYTHON_CMD=python
    goto :found_python
)
py --version >nul 2>nul
if %ERRORLEVEL% EQU 0 (
    set PYTHON_CMD=py
    goto :found_python
)
python3 --version >nul 2>nul
if %ERRORLEVEL% EQU 0 (
    set PYTHON_CMD=python3
    goto :found_python
)

echo [ERROR] Python not found!
echo Install Python from https://www.python.org/downloads/
echo.
pause
exit /b 1

:found_python
echo [OK] Using: %PYTHON_CMD%
echo.
echo ========================================
echo   Opening browser at localhost:5000
echo   Press Ctrl+C to stop the server
echo ========================================
echo.

cd /d "%~dp0"
start "" "http://localhost:5000"
%PYTHON_CMD% -m http.server 5000
