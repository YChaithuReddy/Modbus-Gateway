@echo off
REM ============================================
REM Creates merged firmware binary for web flasher
REM Run this after "idf.py build" in the project root
REM FluxGen Gateway v1.4.0
REM ============================================

set BUILD_DIR=%~dp0..\build
set FIRMWARE_DIR=%~dp0firmware

echo.
echo ========================================
echo   FluxGen Gateway v1.4.0
echo   Packaging Firmware for Web Flasher
echo ========================================
echo.

REM Check build folder exists
if not exist "%BUILD_DIR%\mqtt_azure_minimal.bin" (
    echo [ERROR] Build files not found!
    echo Expected: %BUILD_DIR%\mqtt_azure_minimal.bin
    echo.
    echo Run "idf.py build" in the project root first.
    echo.
    pause
    exit /b 1
)

REM Create firmware folder if needed
if not exist "%FIRMWARE_DIR%" mkdir "%FIRMWARE_DIR%"

REM Create merged binary using esptool
echo Creating merged firmware binary...
echo Offsets: bootloader=0x1000  ptable=0x8000  otadata=0x14000  app=0x20000
echo.

python -m esptool --chip esp32 merge-bin --output "%FIRMWARE_DIR%\merged_firmware.bin" ^
  --flash-mode dio --flash-freq 40m --flash-size 4MB ^
  0x1000  "%BUILD_DIR%\bootloader\bootloader.bin" ^
  0x8000  "%BUILD_DIR%\partition_table\partition-table.bin" ^
  0x14000 "%BUILD_DIR%\ota_data_initial.bin" ^
  0x20000 "%BUILD_DIR%\mqtt_azure_minimal.bin"

if %ERRORLEVEL% NEQ 0 (
    echo.
    echo [ERROR] Failed to create merged binary!
    echo Make sure esptool is installed: pip install esptool
    echo.
    pause
    exit /b 1
)

echo.
echo ========================================
echo   Done! Firmware packaged.
echo ========================================
echo.
echo File: %FIRMWARE_DIR%\merged_firmware.bin
for %%A in ("%FIRMWARE_DIR%\merged_firmware.bin") do echo Size: %%~zA bytes
echo.
echo Next: Run start_flasher.bat to open the web flasher,
echo       or share the FluxGen_Gateway_Flasher_v1.4.0 folder.
echo.
pause
