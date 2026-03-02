#!/bin/bash
# ============================================
# FluxGen Gateway - Web Flasher Launcher
# Opens browser-based firmware flasher
# ============================================

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR"

echo ""
echo "========================================"
echo "  FluxGen Web Flasher"
echo "  Starting local server..."
echo "========================================"
echo ""

# Check firmware files exist
if [ ! -f "firmware/mqtt_azure_minimal.bin" ]; then
    echo "[ERROR] Firmware files not found in firmware/ folder!"
    echo ""
    echo "Run package_firmware first, or manually copy these files:"
    echo "  - bootloader.bin"
    echo "  - partition-table.bin"
    echo "  - ota_data_initial.bin"
    echo "  - mqtt_azure_minimal.bin"
    exit 1
fi

echo "[OK] Firmware files found"
echo ""
echo "========================================"
echo "  Opening browser at localhost:5000"
echo "  Press Ctrl+C to stop the server"
echo "========================================"
echo ""

# Open browser (works on Linux and macOS)
if command -v xdg-open &>/dev/null; then
    xdg-open "http://localhost:5000" &
elif command -v open &>/dev/null; then
    open "http://localhost:5000" &
fi

# Start HTTP server
python3 -m http.server 8080
