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
if [ ! -f "firmware/merged_firmware.bin" ]; then
    echo "[ERROR] Firmware file not found!"
    echo "Expected: firmware/merged_firmware.bin"
    echo ""
    echo "Run package_firmware first to create the merged firmware."
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
python3 -m http.server 5000
