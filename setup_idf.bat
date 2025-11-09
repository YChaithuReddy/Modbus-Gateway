@echo off
echo Setting up ESP-IDF v5.5.1 environment...
call C:\Users\chath\esp\v5.5.1\esp-idf\export.bat
echo.
echo ESP-IDF environment ready!
echo.
echo Available commands:
echo   idf.py build              - Build the project
echo   idf.py -p COM3 flash      - Flash to ESP32 (adjust COM port)
echo   idf.py monitor            - Monitor serial output
echo   idf.py flash monitor      - Flash and monitor
echo   idf.py menuconfig         - Configure project
echo   idf.py clean              - Clean build
echo   idf.py size               - Check binary size
echo   idf.py erase-flash        - Erase entire flash
echo.
cmd /k