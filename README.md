# Fluxgen ESP32 Modbus IoT Gateway - Production Release

[![Production Ready](https://img.shields.io/badge/Status-PRODUCTION-brightgreen.svg)](PRODUCTION_GUIDE.md)
[![Version](https://img.shields.io/badge/Version-1.3.6-blue.svg)](VERSION.md)
[![ESP-IDF](https://img.shields.io/badge/ESP--IDF-v5.5.1-orange.svg)](https://docs.espressif.com/projects/esp-idf/en/v5.5.1/)
[![64-bit Support](https://img.shields.io/badge/64--bit-Complete-purple.svg)](#)
[![License](https://img.shields.io/badge/License-Industrial-yellow.svg)](#)

## Professional Industrial IoT Gateway

A production-ready ESP32-based Modbus IoT gateway designed for **unattended remote deployment**. Features real-time RS485 Modbus communication, comprehensive ScadaCore data format support, SD card offline caching, autonomous recovery, and seamless Azure IoT Hub integration.

## Key Features

### Industrial Grade Communication
- **Real-time RS485 Modbus RTU** with professional error handling
- **20+ comprehensive data formats** including all ScadaCore interpretations
- **Robust diagnostics** with detailed troubleshooting guides
- **Support for up to 8 sensors** per gateway
- **Multiple flow meter types** with specialized data decoding

### Professional Web Interface
- **Responsive industrial design** optimized for field use
- **Individual sensor management** - Add, Edit, Delete, Test independently
- **Real-time testing** with comprehensive format display
- **Professional branding** with customizable company logos
- **Dark gradient theme** for better visibility

### Enterprise Cloud Integration
- **Azure IoT Hub connectivity** with secure MQTT communication
- **Configurable telemetry intervals** (30-3600 seconds)
- **Automatic reconnection** with exponential backoff (up to 20 retries)
- **Persistent configuration** in flash memory
- **SD Card offline caching** with chronological replay

### Dual Connectivity Modes
- **WiFi Mode** - Connect to existing WiFi networks (with USB dongle support)
- **SIM/4G Mode** - A7670C cellular modem with PPP support
- **Automatic failover** and recovery

### Autonomous Recovery (v1.2.0)
- **No system restarts** on network errors - caches data instead
- **WiFi dongle power cycling** with 5-minute cooldown
- **Automatic MQTT reconnection** with exponential backoff
- **SD card caching** during network outages
- **First telemetry** sent immediately after boot (~10-20 seconds)
- **Watchdog protection** - 2-minute timeout if main loop stuck
- **Telemetry timeout** - Force restart if no successful send for 30 minutes

## Supported Sensor Types

### Flow Meters (Totalizer/Cumulative)

| Sensor Type | Data Format | Registers | Description |
|-------------|-------------|-----------|-------------|
| **Flow-Meter** | UINT32_BADC + FLOAT32_BADC | 4 | Generic flow meter |
| **Panda_USM** | FLOAT64_BE (Double) | 4 | Panda Ultrasonic meter |
| **Clampon** | UINT32_BADC + FLOAT32_BADC | 4 | Clampon flow meter |
| **ZEST** | UINT16 + FLOAT32_BE | 4 | AquaGen ZEST meter |
| **Dailian** | UINT32_3412 (CDAB) | 2 | Dailian Ultrasonic |
| **Dailian_EMF** | UINT32 word-swapped | 2 | Dailian EMF meter |
| **Panda_EMF** | INT32_BE + FLOAT32_BE | 4 | Panda EMF meter |

### Level Sensors

| Sensor Type | Data Format | Calculation |
|-------------|-------------|-------------|
| **Level** | User-selectable | Level % = ((Sensor Height - Raw) / Tank Height) x 100 |
| **Radar Level** | User-selectable | Level % = (Raw / Max Water Level) x 100 |
| **Panda_Level** | UINT16 | Level % = ((Sensor Height - Raw) / Tank Height) x 100 |
| **Piezometer** | UINT16_HI (fixed) | Raw value x scale factor |

### Other Sensors

| Sensor Type | Description |
|-------------|-------------|
| **ENERGY** | Energy meter readings |
| **RAINGAUGE** | Rain gauge measurements |
| **BOREWELL** | Borewell monitoring |
| **QUALITY** | Water quality (pH, turbidity, DO, conductivity) |

## Supported Data Formats

### 8-bit Formats
- **INT8** - 8-bit Signed (-128 to 127)
- **UINT8** - 8-bit Unsigned (0 to 255)

### 16-bit Formats (1 register)
- **INT16_HI/LO** - 16-bit signed, high/low byte first
- **UINT16_HI/LO** - 16-bit unsigned, high/low byte first

### 32-bit Integer Formats (2 registers)
- **INT32_ABCD** - Big Endian
- **INT32_DCBA** - Little Endian
- **INT32_BADC** - Mid-Big Endian (Word swap)
- **INT32_CDAB** - Mid-Little Endian
- **UINT32_ABCD/DCBA/BADC/CDAB** - Unsigned variants

### 32-bit Float Formats (2 registers)
- **FLOAT32_ABCD/DCBA/BADC/CDAB** - IEEE 754 with all byte orders

### 64-bit Formats (4 registers)
- **INT64_12345678/87654321/21436587/78563412** - All byte orders
- **UINT64_12345678/87654321/21436587/78563412** - Unsigned variants
- **FLOAT64_12345678/87654321/21436587/78563412** - Double precision

### Special Formats
- **ASCII** - ASCII String
- **HEX** - Hexadecimal
- **BOOL** - Boolean
- **PDU** - Protocol Data Unit

## Hardware Connections

### ESP32 Pin Configuration
```
ESP32 Connections:
├── RS485 Module
│   ├── GPIO 16 (RX2)  → RS485 RO (Receive)
│   ├── GPIO 17 (TX2)  → RS485 DI (Transmit)
│   └── GPIO 18 (RTS)  → RS485 DE/RE (Direction)
│
├── SD Card Module (SPI)
│   ├── GPIO 15 (CS)   → SD Card CS
│   ├── GPIO 13 (MOSI) → SD Card MOSI
│   ├── GPIO 12 (MISO) → SD Card MISO
│   └── GPIO 14 (CLK)  → SD Card CLK
│
├── A7670C SIM Module (Optional)
│   ├── GPIO 26 (TX)   → SIM Module RX
│   ├── GPIO 27 (RX)   → SIM Module TX
│   └── GPIO 4  (PWR)  → SIM Module Power Control
│
├── WiFi USB Dongle (Optional)
│   └── GPIO 2  (PWR)  → Dongle Power Control (via relay/MOSFET)
│
└── GND                → Common Ground
```

### Important GPIO Notes
- **GPIO 15** is used by SD Card CS - Do NOT use for SIM reset pin
- **GPIO 2** controls WiFi dongle power cycling (if enabled)
- **GPIO 4** controls SIM module power cycling

## Quick Start

### 1. Build and Flash
```bash
git clone https://github.com/YChaithuReddy/Modbus-Gateway.git
cd modbus_iot_gateway
idf.py build
idf.py -p COM3 flash monitor      # Windows
idf.py -p /dev/ttyUSB0 flash monitor  # Linux
```

### 2. Configure via Web Interface
1. Connect to WiFi: `FluxGen-Gateway` (password: `fluxgen123`)
2. Open browser: `http://192.168.4.1`
3. Configure WiFi or SIM settings
4. Configure Azure IoT Hub credentials
5. Add sensors with appropriate type selection
6. Test each sensor using Test RS485 feature

### 3. Deploy to Field
1. Power on the gateway
2. First telemetry sends within ~20 seconds
3. System automatically recovers from network issues
4. Data cached to SD card during outages

## Production Configuration

### Configuration Constants (iot_configs.h)

| Parameter | Value | Description |
|-----------|-------|-------------|
| `TELEMETRY_FREQUENCY_MILLISECS` | 300000 | 5 minutes between telemetry |
| `MAX_MQTT_RECONNECT_ATTEMPTS` | 20 | Retries before giving up |
| `SYSTEM_RESTART_ON_CRITICAL_ERROR` | false | Cache to SD instead of restart |
| `WATCHDOG_TIMEOUT_SEC` | 120 | 2 minutes - restart if stuck |
| `TELEMETRY_TIMEOUT_SEC` | 1800 | 30 minutes - force restart if no telemetry |
| `HEARTBEAT_LOG_INTERVAL_SEC` | 300 | 5 minutes - log to SD card |
| `MODEM_RESET_COOLDOWN_SEC` | 300 | 5 minutes between dongle resets |

### Autonomous Recovery Behavior

| Scenario | System Behavior |
|----------|-----------------|
| WiFi disconnects | Retries 20 times with backoff, caches to SD |
| MQTT fails | Reconnects with exponential backoff |
| Network down >5 min | Power cycles WiFi dongle (once per 5 min) |
| Network restored | Sends cached data FIRST, then live data |
| Sensor read fails | Logs error, continues with other sensors |
| SD card missing | Warning logged, telemetry still sent when online |
| Main loop stuck | Watchdog restarts system after 2 minutes |

## SD Card Offline Caching

### Features
- **Automatic caching** when network is unavailable
- **Chronological replay** - Cached data sent FIRST when network resumes
- **Message validation** - Skips invalid timestamps and placeholder data
- **Batch processing** - 20 messages per replay cycle
- **Auto-recovery** - Remounts SD card on filesystem errors

### Why Offline Data First?
For flow meters and totalizers, data must be sent in chronological order:
1. Cloud uses first received data as reference point
2. If live data sent first, cached data would be "out of order"
3. Historical data would be interpolated incorrectly or wasted

### Sequence When Network Resumes
```
1. Check SD card for pending messages
2. Send ALL cached messages first (oldest to newest)
3. Wait 500ms for processing
4. Send current live telemetry data
```

## Web Interface Features

### Overview Dashboard
- System status and uptime
- Azure IoT Hub connection status
- Modbus communication statistics
- Network signal strength

### Sensor Configuration
- Add/Edit/Delete sensors
- Test RS485 communication
- View all ScadaCore format interpretations
- Real-time value display

### Settings
- WiFi configuration with network scanning
- SIM/4G modem settings (Reset pin defaults to -1, disabled)
- Azure IoT Hub credentials
- Telemetry interval configuration
- SD Card enable/disable
- Modem reset GPIO configuration

## Project Structure

```
modbus_iot_gateway/
├── main/
│   ├── main.c              # Main application and task management
│   ├── web_config.c        # Web interface (687KB - handle with care!)
│   ├── web_config.h        # Configuration structures
│   ├── sensor_manager.c    # Sensor data processing
│   ├── modbus.c            # RS485 Modbus RTU implementation
│   ├── sd_card_logger.c    # SD card offline caching
│   ├── a7670c_ppp.c        # SIM module PPP implementation
│   ├── json_templates.c    # Telemetry JSON formatting
│   ├── iot_configs.h       # IoT configuration constants
│   └── gpio_map.h          # GPIO pin definitions
├── CLAUDE.md               # Development guidelines
├── README.md               # This file
├── HARDWARE_CONNECTIONS.md # Detailed wiring guide
└── sdkconfig               # ESP-IDF configuration
```

## Version History

### v1.3.6 (Current) - OTA & Device Twin Fixes
**OTA Updates from GitHub:**
- OTA firmware updates from GitHub releases now fully working
- Fixed certificate verification issues on GitHub CDN
- Fixed heap exhaustion during OTA (stop MQTT first)
- Fixed long redirect URL crashes (923-byte Location headers)
- Fixed Device Twin firmware version reporting

**Continuous Sensor Reading:**
- Sensors continue reading during SD card replay
- No data loss during 4+ hour replay scenarios
- Cached sensor data maintains chronological order

### v1.2.0 - Production Autonomous Release
**Autonomous Recovery Features:**
- System no longer restarts on network errors (caches to SD instead)
- Added 5-minute cooldown between WiFi dongle resets
- First telemetry sends immediately after boot (~10-20 seconds)
- Fixed GPIO 15 conflict between SD card CS and SIM reset pin
- SIM reset pin now defaults to -1 (disabled)
- Increased MQTT reconnect attempts from 5 to 20
- Split large web_config.c sections to prevent buffer overflow

**Bug Fixes:**
- Fixed modem reset cycling continuously
- Fixed first telemetry waiting full interval instead of sending after boot
- Fixed SD card initialization failure (GPIO conflict)

### v1.1.0 - Stable Release
**New Sensor Types:**
- Panda EMF - INT32_BE + FLOAT32_BE totalizer format
- Panda Level - UINT16 with percentage calculation
- Clampon - UINT32_BADC + FLOAT32_BADC format
- Dailian EMF - UINT32 word-swapped totalizer

**Bug Fixes:**
- Fixed Test RS485 display for all flow meter types
- Fixed ScadaCore table header visibility
- Fixed success page styling
- Fixed buffer overflow in success page

**Improvements:**
- Offline data priority - Cached data sent BEFORE live data
- Memory optimization - Skip MQTT/telemetry tasks in setup mode
- PPP recovery - Better handling of modem reset on reboot

### v1.0.0 - Initial Release
- RS485 Modbus RTU communication
- Azure IoT Hub integration
- WiFi and SIM connectivity
- Web configuration interface
- SD card offline caching

## Future Roadmap

### Stage 2: Remote Management (Planned)
- OTA firmware updates via Azure
- Device Twin for remote configuration
- Cloud-to-Device (C2D) commands

### Stage 3: Monitoring & Alerts (Planned)
- Threshold-based alerts
- Health monitoring dashboard
- Remote diagnostic logs

### Stage 4: Advanced Features (Planned)
- Multi-device fleet management
- Power BI integration
- Edge processing/local rules
- Secure boot

## Troubleshooting

### RS485 Communication Issues
1. Verify baud rate matches sensor (default: 9600)
2. Check GPIO pins: RX=16, TX=17, RTS=18
3. Ensure proper RS485 termination
4. Verify slave ID and register addresses

### SD Card Issues
1. Format as FAT32 (not exFAT)
2. Use 2GB-16GB card (Class 4 or 10)
3. Check wiring: CS=15, MOSI=13, MISO=12, CLK=14
4. Ensure SIM reset pin is set to -1 (GPIO 15 conflict)

### MQTT Connection Issues
1. Verify Azure IoT Hub credentials
2. Check network connectivity
3. Verify DNS resolution for azure-devices.net
4. Check SAS token expiration
5. System will retry 20 times before caching to SD

### WiFi Dongle Not Recovering
1. Check GPIO 2 is connected to dongle power
2. Verify modem reset is enabled in web interface
3. Wait 5 minutes (cooldown between resets)
4. Check logs for "Modem reset cooldown active"

## Deployment Checklist

- [ ] Hardware properly wired and tested
- [ ] RS485 network with proper termination
- [ ] SD card formatted as FAT32 and inserted
- [ ] WiFi or SIM credentials configured
- [ ] Azure IoT Hub device provisioned
- [ ] All sensors configured and tested
- [ ] Offline caching verified
- [ ] First telemetry confirmed (within 20 seconds of boot)
- [ ] Network recovery tested (disconnect/reconnect WiFi)

## Support

### Technical Support Levels
- **Level 1**: Basic configuration and WiFi setup
- **Level 2**: RS485 communication and sensor integration
- **Level 3**: Custom firmware and enterprise integration

### Resources
- **Web Interface** - Built-in help and diagnostics
- **Serial Monitor** - Detailed debug logging
- **CLAUDE.md** - Development guidelines and known issues
- **GitHub Issues** - Bug reports and feature requests

## About Fluxgen

**Fluxgen Industrial IoT Solutions** - Professional industrial automation and IoT connectivity solutions for modern manufacturing and process control environments.

---

**Status: PRODUCTION READY v1.3.6**

*Designed for unattended remote deployment with autonomous recovery capabilities and OTA updates via GitHub.*
