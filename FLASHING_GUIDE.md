# ESP32 Firmware Flashing Guide (No ESP-IDF Required)

This guide explains how to flash the Modbus IoT Gateway firmware to ESP32 **without installing ESP-IDF**. Perfect for team members familiar with Arduino.

---

## Quick Start (5 minutes)

### What You Need
1. **ESP32 board** (any variant with 4MB+ flash)
2. **USB cable** (data cable, not charging-only)
3. **Flash Download Tool** (free from Espressif)

### Download Links
- **Flash Download Tool**: https://www.espressif.com/en/support/download/other-tools
- **Firmware Files**: Get from your team lead or GitHub Releases

---

## Method 1: ESP Flash Download Tool (Windows - Easiest)

### Step 1: Download the Tool
1. Go to: https://www.espressif.com/en/support/download/other-tools
2. Download "Flash Download Tools (Windows)"
3. Extract the ZIP file
4. Run `flash_download_tool_x.x.x.exe`

### Step 2: Configure the Tool
1. Select **ESP32** as chip type
2. Select **Develop** mode
3. Click **OK**

### Step 3: Add Firmware Files

Add these 4 files with their addresses:

| File | Address | Location |
|------|---------|----------|
| `bootloader.bin` | `0x1000` | `build/bootloader/` |
| `partition-table.bin` | `0x8000` | `build/partition_table/` |
| `ota_data_initial.bin` | `0xd000` | `build/` |
| `mqtt_azure_minimal.bin` | `0x10000` | `build/` |

**Check the checkbox next to each file!**

### Step 4: Configure Settings
```
SPI SPEED: 40MHz
SPI MODE: DIO
FLASH SIZE: 4MB (or 8MB/16MB if your board has more)
COM: Select your ESP32 port (e.g., COM3, COM5)
BAUD: 460800 (or 115200 if issues)
```

### Step 5: Flash
1. Hold **BOOT** button on ESP32
2. Click **START** in the tool
3. Release **BOOT** button after download starts
4. Wait for "FINISH" message

### Step 6: Access Web Interface
1. Power cycle the ESP32
2. Connect to WiFi: **FluxGen-Gateway** (password: `fluxgen123`)
3. Open browser: **http://192.168.4.1**

---

## Method 2: esptool.py (Cross-Platform - Command Line)

### Step 1: Install Python & esptool
```bash
# Install Python from python.org first, then:
pip install esptool
```

### Step 2: Flash with Single Command
```bash
# Windows (replace COM3 with your port)
esptool.py --chip esp32 --port COM3 --baud 460800 \
  --before default_reset --after hard_reset write_flash \
  -z --flash_mode dio --flash_freq 40m --flash_size 4MB \
  0x1000 build/bootloader/bootloader.bin \
  0x8000 build/partition_table/partition-table.bin \
  0xd000 build/ota_data_initial.bin \
  0x10000 build/mqtt_azure_minimal.bin

# Linux/Mac (replace /dev/ttyUSB0 with your port)
esptool.py --chip esp32 --port /dev/ttyUSB0 --baud 460800 \
  --before default_reset --after hard_reset write_flash \
  -z --flash_mode dio --flash_freq 40m --flash_size 4MB \
  0x1000 build/bootloader/bootloader.bin \
  0x8000 build/partition_table/partition-table.bin \
  0xd000 build/ota_data_initial.bin \
  0x10000 build/mqtt_azure_minimal.bin
```

### Find Your COM Port
- **Windows**: Device Manager → Ports (COM & LPT)
- **Linux**: `ls /dev/ttyUSB*` or `ls /dev/ttyACM*`
- **Mac**: `ls /dev/cu.*`

---

## Method 3: Arduino IDE (For Arduino Users)

### Step 1: Install ESP32 Board Support
1. Open Arduino IDE
2. Go to **File → Preferences**
3. Add to "Additional Board Manager URLs":
   ```
   https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
   ```
4. Go to **Tools → Board → Board Manager**
5. Search "ESP32" and install

### Step 2: Install esptool via Arduino
Arduino IDE installs esptool automatically. Find it at:
- **Windows**: `C:\Users\<username>\AppData\Local\Arduino15\packages\esp32\tools\esptool_py\`
- **Mac**: `~/Library/Arduino15/packages/esp32/tools/esptool_py/`

### Step 3: Flash Using esptool
Use the esptool command from Method 2 with the path to Arduino's esptool.

---

## Troubleshooting

### "Failed to connect to ESP32"
1. **Hold BOOT button** while connecting
2. Try different USB cable (some are charge-only)
3. Install USB drivers: https://www.silabs.com/developers/usb-to-uart-bridge-vcp-drivers
4. Lower baud rate to `115200`

### "A fatal error occurred: Timed out"
1. Press and hold **BOOT** button
2. Click **START** in flash tool
3. Wait 2 seconds, then release **BOOT**

### "No port detected"
1. Install CP210x or CH340 USB drivers
2. Try different USB port (preferably USB 2.0)
3. Check Device Manager for the port

### ESP32 not creating WiFi hotspot
1. Wait 30 seconds after boot
2. Power cycle the ESP32
3. Check serial monitor at 115200 baud for errors

---

## After Flashing - First Setup

1. **Connect to Gateway WiFi**
   - SSID: `FluxGen-Gateway`
   - Password: `fluxgen123`

2. **Open Web Interface**
   - URL: `http://192.168.4.1`

3. **Configure Your Network**
   - Enter your WiFi SSID/password, OR
   - Enable SIM mode with APN settings

4. **Configure Azure IoT Hub**
   - Enter Hub FQDN (e.g., `your-hub.azure-devices.net`)
   - Enter Device ID and Device Key

5. **Add Sensors**
   - Go to Modbus Sensors section
   - Add your RS485 sensors
   - Test each sensor using Test RS485 button

---

## File Locations

After building the project, find these files:

```
modbus_iot_gateway/
└── build/
    ├── bootloader/
    │   └── bootloader.bin        ← Flash at 0x1000
    ├── partition_table/
    │   └── partition-table.bin   ← Flash at 0x8000
    ├── ota_data_initial.bin      ← Flash at 0xd000
    └── mqtt_azure_minimal.bin    ← Flash at 0x10000 (main firmware)
```

---

## Creating a Merged Binary (Optional)

For easier distribution, create a single merged binary:

```bash
# Using esptool
esptool.py --chip esp32 merge_bin -o merged_firmware.bin \
  --flash_mode dio --flash_freq 40m --flash_size 4MB \
  0x1000 build/bootloader/bootloader.bin \
  0x8000 build/partition_table/partition-table.bin \
  0xd000 build/ota_data_initial.bin \
  0x10000 build/mqtt_azure_minimal.bin
```

Then flash the merged binary at address `0x0`:
```bash
esptool.py --chip esp32 --port COM3 --baud 460800 write_flash 0x0 merged_firmware.bin
```

---

## Quick Reference Card

| Step | Action |
|------|--------|
| 1 | Download Flash Download Tool |
| 2 | Connect ESP32 via USB |
| 3 | Add 4 bin files with addresses |
| 4 | Select COM port, click START |
| 5 | Hold BOOT if needed |
| 6 | Connect to FluxGen-Gateway WiFi |
| 7 | Open http://192.168.4.1 |
| 8 | Configure and deploy! |

---

**Version**: 1.3.6
**Last Updated**: December 2024
