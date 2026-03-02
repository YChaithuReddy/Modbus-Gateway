@echo off
REM ============================================
REM Creates merged firmware binary for web flasher
REM Run this after building with idf.py build
REM ============================================

set BUILD_DIR=%~dp0..\build
set FIRMWARE_DIR=%~dp0firmware

echo.
echo ========================================
echo   Packaging Firmware for Web Flasher
echo ========================================
echo.

REM Check build folder exists
if not exist "%BUILD_DIR%\mqtt_azure_minimal.bin" (
    echo [ERROR] Build files not found!
    echo Run "idf.py build" first.
    echo.
    pause
    exit /b 1
)

REM Create firmware folder if needed
if not exist "%FIRMWARE_DIR%" mkdir "%FIRMWARE_DIR%"

REM Create merged binary using esptool
echo Creating merged firmware binary...
python -m esptool --chip esp32 merge-bin --output "%FIRMWARE_DIR%\merged_firmware.bin" ^
  --flash-mode dio --flash-freq 40m --flash-size 4MB ^
  0x1000 "%BUILD_DIR%\bootloader\bootloader.bin" ^
  0x8000 "%BUILD_DIR%\partition_table\partition-table.bin" ^
  0x14000 "%BUILD_DIR%\ota_data_initial.bin" ^
  0x20000 "%BUILD_DIR%\mqtt_azure_minimal.bin"

if %ERRORLEVEL% NEQ 0 (
    echo [ERROR] Failed to create merged binary!
    echo Make sure esptool is installed: pip install esptool
    pause
    exit /b 1
)

echo.
echo ========================================
echo   Done! Merged firmware created.
echo ========================================
echo.
echo File: %FIRMWARE_DIR%\merged_firmware.bin
for %%A in ("%FIRMWARE_DIR%\merged_firmware.bin") do echo Size: %%~zA bytes
echo.
echo Next: Run start_flasher.bat to open the web flasher.
echo Or share the entire flash_web folder with your team.
echo.
pause
