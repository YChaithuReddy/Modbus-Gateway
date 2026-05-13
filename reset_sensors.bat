@echo off
REM Sensor Configuration Reset Script for ESP32 Modbus IoT Gateway
REM No CORS issues - runs directly from command line

echo ==========================================
echo ESP32 Sensor Configuration Reset
echo ==========================================
echo.

set DEVICE_IP=192.168.4.1

echo Checking device connection...
ping -n 1 %DEVICE_IP% >nul 2>&1
if errorlevel 1 (
    echo [ERROR] Cannot reach device at %DEVICE_IP%
    echo.
    echo Please check:
    echo   - Device is powered on
    echo   - Web server is running
    echo   - You are connected to device WiFi: ModbusIoT-Config
    echo.
    pause
    exit /b 1
)

echo [OK] Device is reachable
echo.

echo ==========================================
echo Step 1: Resetting Sensor Configurations
echo ==========================================
echo.
echo Sending reset command...
curl -X POST http://%DEVICE_IP%/reset_sensors -H "Content-Type: application/json"
echo.
echo.

echo ==========================================
echo Step 2: Rebooting Device
echo ==========================================
echo.
choice /C YN /M "Do you want to reboot the device now"
if errorlevel 2 goto skip_reboot

echo.
echo Sending reboot command...
curl -X POST http://%DEVICE_IP%/reboot -H "Content-Type: application/json"
echo.
echo.
echo [INFO] Device is rebooting...
echo [INFO] Please wait 30 seconds before reconnecting
echo.
pause
exit /b 0

:skip_reboot
echo.
echo [INFO] Reboot skipped. Please reboot manually to apply changes.
echo [INFO] You can run this command to reboot later:
echo        curl -X POST http://%DEVICE_IP%/reboot
echo.
pause
exit /b 0
