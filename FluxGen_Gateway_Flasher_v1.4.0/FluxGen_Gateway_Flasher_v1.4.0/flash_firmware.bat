@echo off
REM ============================================
REM FluxGen Gateway - Command-Line Flasher
REM Reliable fallback using esptool
REM Version 1.4.0
REM ============================================

echo.
echo ========================================
echo   FluxGen Gateway - Command-Line Flasher
echo   Version 1.4.0
echo ========================================
echo.

REM Check firmware exists
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

REM Not found — offer to install
echo [INFO] esptool not found. Installing automatically...
python -m pip install esptool >nul 2>nul
if %ERRORLEVEL% EQU 0 (
    set ESPTOOL_CMD=python -m esptool
    goto :found_esptool
)
py -m pip install esptool >nul 2>nul
if %ERRORLEVEL% EQU 0 (
    set ESPTOOL_CMD=py -m esptool
    goto :found_esptool
)
echo.
echo [ERROR] Could not install esptool automatically.
echo Please install manually: pip install esptool
echo You also need Python from https://python.org
echo.
pause
exit /b 1

:found_esptool
echo [OK] Found esptool: %ESPTOOL_CMD%
echo.

REM List available COM ports
echo Available COM ports:
echo -------------------
mode | findstr "COM"
echo -------------------
echo.
set /p COMPORT="Enter COM port (e.g., COM3): "

echo.
echo ========================================
echo   Flashing to %COMPORT%...
echo ========================================
echo.
echo TIP: Hold the BOOT button on ESP32 if flashing fails.
echo.

%ESPTOOL_CMD% --chip esp32 --port %COMPORT% --baud 460800 ^
  --before default_reset --after hard_reset write_flash ^
  -z --flash_mode dio --flash_freq 40m --flash_size 4MB ^
  0x0 "%~dp0firmware\merged_firmware.bin"

if %ERRORLEVEL% EQU 0 (
    echo.
    echo ========================================
    echo   SUCCESS! Firmware v1.4.0 flashed.
    echo ========================================
    echo.
    echo Next steps:
    echo   1. Wait 10 seconds for ESP32 to boot
    echo   2. Connect to WiFi: ModbusIoT-Config
    echo      Password: config123
    echo   3. Open browser: http://192.168.4.1
    echo   4. Configure WiFi/SIM, Azure IoT Hub, and sensors
    echo.
    echo NEW in v1.4.0:
    echo   - WireGuard VPN auto-connects on boot
    echo   - Remote web access via ping from VPN
    echo.
) else (
    echo.
    echo ========================================
    echo   FLASH FAILED - Try these fixes:
    echo ========================================
    echo.
    echo   1. Hold BOOT button while flashing
    echo   2. Try lower baud rate:
    echo      %ESPTOOL_CMD% --chip esp32 --port %COMPORT% --baud 115200 ^
    echo        write_flash -z 0x0 firmware\merged_firmware.bin
    echo   3. Use a different USB cable (must be data cable)
    echo   4. Install USB driver: CP2102 or CH340
    echo   5. Close any serial monitors (Arduino IDE, PuTTY, etc.)
    echo.
)

pause
