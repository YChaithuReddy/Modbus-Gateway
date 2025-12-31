@echo off
REM ============================================
REM Fluxgen Modbus IoT Gateway - Flash Script
REM Version 1.3.7
REM ============================================

echo.
echo ========================================
echo   Fluxgen Modbus IoT Gateway Flasher
echo   Version 1.3.7
echo ========================================
echo.

REM Try different ways to find esptool
set ESPTOOL_CMD=

REM Method 1: Try python -m esptool
python -m esptool version >nul 2>nul
if %ERRORLEVEL% EQU 0 (
    set ESPTOOL_CMD=python -m esptool
    goto :found_esptool
)

REM Method 2: Try py -m esptool
py -m esptool version >nul 2>nul
if %ERRORLEVEL% EQU 0 (
    set ESPTOOL_CMD=py -m esptool
    goto :found_esptool
)

REM Method 3: Try esptool.py directly
where esptool.py >nul 2>nul
if %ERRORLEVEL% EQU 0 (
    set ESPTOOL_CMD=esptool.py
    goto :found_esptool
)

REM Method 4: Try esptool (without .py)
where esptool >nul 2>nul
if %ERRORLEVEL% EQU 0 (
    set ESPTOOL_CMD=esptool
    goto :found_esptool
)

REM Not found
echo [ERROR] esptool not found!
echo.
echo Please install esptool using one of these:
echo   python -m pip install esptool
echo   py -m pip install esptool
echo   pip install esptool
echo.
pause
exit /b 1

:found_esptool
echo [OK] Found esptool: %ESPTOOL_CMD%

REM Check if build files exist
if not exist "build\bootloader\bootloader.bin" (
    echo [ERROR] Firmware files not found!
    echo Please ensure you have the build folder with:
    echo   - build\bootloader\bootloader.bin
    echo   - build\partition_table\partition-table.bin
    echo   - build\ota_data_initial.bin
    echo   - build\mqtt_azure_minimal.bin
    echo.
    pause
    exit /b 1
)

REM Ask for COM port
echo Available COM ports:
mode | findstr "COM"
echo.
set /p COMPORT="Enter COM port (e.g., COM3): "

echo.
echo ========================================
echo   Flashing to %COMPORT%...
echo ========================================
echo.
echo TIP: Hold the BOOT button on ESP32 if flashing fails
echo.

%ESPTOOL_CMD% --chip esp32 --port %COMPORT% --baud 460800 ^
  --before default_reset --after hard_reset write_flash ^
  -z --flash_mode dio --flash_freq 40m --flash_size 4MB ^
  0x1000 build\bootloader\bootloader.bin ^
  0x8000 build\partition_table\partition-table.bin ^
  0xd000 build\ota_data_initial.bin ^
  0x10000 build\mqtt_azure_minimal.bin

if %ERRORLEVEL% EQU 0 (
    echo.
    echo ========================================
    echo   SUCCESS! Firmware flashed.
    echo ========================================
    echo.
    echo Next steps:
    echo   1. Wait 10 seconds for ESP32 to boot
    echo   2. Connect to WiFi: FluxGen-Gateway
    echo      Password: fluxgen123
    echo   3. Open browser: http://192.168.4.1
    echo.
) else (
    echo.
    echo ========================================
    echo   FAILED! Try these solutions:
    echo ========================================
    echo   1. Hold BOOT button while flashing
    echo   2. Try lower baud rate: 115200
    echo   3. Check USB cable (use data cable)
    echo   4. Install USB drivers
    echo.
)

pause
