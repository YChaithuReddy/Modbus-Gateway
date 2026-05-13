#!/bin/bash
# ============================================
# FluxGen Gateway - Command-Line Flasher
# Reliable fallback using esptool
# Version 1.3.8
# ============================================

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

echo ""
echo "========================================"
echo "  FluxGen Gateway - Command-Line Flasher"
echo "  Version 1.3.8"
echo "========================================"
echo ""

# Check firmware exists
if [ ! -f "$SCRIPT_DIR/firmware/merged_firmware.bin" ]; then
    echo "[ERROR] Firmware file not found!"
    echo "Expected: firmware/merged_firmware.bin"
    echo ""
    echo "Run package_firmware first to create the merged firmware."
    exit 1
fi

# Find esptool
ESPTOOL_CMD=""
if command -v esptool.py &>/dev/null; then
    ESPTOOL_CMD="esptool.py"
elif python3 -m esptool version &>/dev/null 2>&1; then
    ESPTOOL_CMD="python3 -m esptool"
elif python -m esptool version &>/dev/null 2>&1; then
    ESPTOOL_CMD="python -m esptool"
elif command -v esptool &>/dev/null; then
    ESPTOOL_CMD="esptool"
fi

if [ -z "$ESPTOOL_CMD" ]; then
    echo "[ERROR] esptool not found!"
    echo ""
    echo "Installing esptool..."
    pip3 install esptool 2>/dev/null || pip install esptool 2>/dev/null
    if python3 -m esptool version &>/dev/null 2>&1; then
        ESPTOOL_CMD="python3 -m esptool"
    else
        echo ""
        echo "Could not install esptool. Please install manually:"
        echo "  pip install esptool"
        exit 1
    fi
fi

echo "[OK] Found esptool: $ESPTOOL_CMD"
echo ""

# Detect serial port
echo "Available serial ports:"
echo "-------------------"
if [ -d /dev/serial/by-id ]; then
    ls -la /dev/serial/by-id/ 2>/dev/null
elif ls /dev/ttyUSB* 2>/dev/null || ls /dev/ttyACM* 2>/dev/null; then
    true
elif ls /dev/cu.* 2>/dev/null; then
    true
else
    echo "(none detected)"
fi
echo "-------------------"
echo ""

read -p "Enter serial port (e.g., /dev/ttyUSB0): " PORT

echo ""
echo "========================================"
echo "  Flashing to $PORT..."
echo "========================================"
echo ""
echo "TIP: Hold the BOOT button on ESP32 if flashing fails."
echo ""

$ESPTOOL_CMD --chip esp32 --port "$PORT" --baud 460800 \
  --before default_reset --after hard_reset write_flash \
  -z --flash_mode dio --flash_freq 40m --flash_size 4MB \
  0x0 "$SCRIPT_DIR/firmware/merged_firmware.bin"

if [ $? -eq 0 ]; then
    echo ""
    echo "========================================"
    echo "  SUCCESS! Firmware flashed."
    echo "========================================"
    echo ""
    echo "Next steps:"
    echo "  1. Wait 10 seconds for ESP32 to boot"
    echo "  2. Connect to WiFi: ModbusIoT-Config"
    echo "     Password: config123"
    echo "  3. Open browser: http://192.168.4.1"
else
    echo ""
    echo "========================================"
    echo "  FLASH FAILED - Try these fixes:"
    echo "========================================"
    echo "  1. Hold BOOT button while flashing"
    echo "  2. Try lower baud: $ESPTOOL_CMD --chip esp32 --port $PORT --baud 115200 write_flash -z 0x0 firmware/merged_firmware.bin"
    echo "  3. Use a different USB cable (must be data cable)"
    echo "  4. Install USB driver for your board"
fi
