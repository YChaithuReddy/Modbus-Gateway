# ESP-IDF Build Script for Windows PowerShell
Set-Location "C:\Users\chath\OneDrive\Documents\Python code\modbus_iot_gateway_stable final"

# Set ESP-IDF environment variables
$env:IDF_PATH = "C:\Espressif\frameworks\esp-idf-v5.5.1"
$env:IDF_TOOLS_PATH = "C:\Espressif"
$env:IDF_PYTHON_ENV_PATH = "C:\Espressif\python_env\idf5.5_py3.13_env"

# Add ESP-IDF tools to PATH
$env:PATH = "$env:IDF_PYTHON_ENV_PATH\Scripts;$env:IDF_PATH\tools;$env:PATH"

# Run the build using Python directly
& "$env:IDF_PYTHON_ENV_PATH\Scripts\python.exe" "$env:IDF_PATH\tools\idf.py" build

if ($LASTEXITCODE -ne 0) {
    Write-Host "Build failed with error code $LASTEXITCODE" -ForegroundColor Red
    exit $LASTEXITCODE
} else {
    Write-Host "Build completed successfully!" -ForegroundColor Green
}