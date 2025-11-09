@echo off
cd /d "C:\Users\chath\OneDrive\Documents\Python code\modbus_iot_gateway_stable final"

REM Set up ESP-IDF environment
call C:\Espressif\frameworks\esp-idf-v5.5.1\export.bat

REM Build the project
idf.py build

if %ERRORLEVEL% NEQ 0 (
    echo Build failed with error code %ERRORLEVEL%
    exit /b %ERRORLEVEL%
) else (
    echo Build completed successfully!
)