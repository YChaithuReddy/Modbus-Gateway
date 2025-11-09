# ESP-IDF Setup Script for PowerShell
Write-Host "Setting up ESP-IDF v5.5.1 environment..." -ForegroundColor Green

# Set ESP-IDF path
$env:IDF_PATH = "C:\Users\chath\esp\v5.5.1\esp-idf"

# Run the export script
& "$env:IDF_PATH\export.ps1"

Write-Host "`nESP-IDF environment ready!" -ForegroundColor Green
Write-Host "`nAvailable commands:" -ForegroundColor Yellow
Write-Host "  idf.py build              - Build the project"
Write-Host "  idf.py -p COM3 flash      - Flash to ESP32 (adjust COM port)"
Write-Host "  idf.py monitor            - Monitor serial output"
Write-Host "  idf.py flash monitor      - Flash and monitor"
Write-Host "  idf.py menuconfig         - Configure project"
Write-Host "  idf.py clean              - Clean build"
Write-Host "  idf.py size               - Check binary size"
Write-Host "  idf.py erase-flash        - Erase entire flash"
Write-Host ""