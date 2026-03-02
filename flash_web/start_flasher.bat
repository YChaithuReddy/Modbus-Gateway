@echo off
REM ============================================
REM FluxGen Gateway - Web Flasher Launcher
REM Opens browser-based firmware flasher
REM ============================================

echo.
echo ========================================
echo   FluxGen Web Flasher
echo   Starting local server...
echo ========================================
echo.

REM Check firmware files exist
if not exist "%~dp0firmware\mqtt_azure_minimal.bin" (
    echo [ERROR] Firmware files not found in firmware\ folder!
    echo.
    echo Run package_firmware.bat first to copy build files,
    echo or manually copy these files to the firmware\ folder:
    echo   - bootloader.bin
    echo   - partition-table.bin
    echo   - ota_data_initial.bin
    echo   - mqtt_azure_minimal.bin
    echo.
    pause
    exit /b 1
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

REM Start HTTP server from the flash_web directory
cd /d "%~dp0"

REM Open browser after a short delay
start "" "http://localhost:5000"

%PYTHON_CMD% -m http.server 5000
