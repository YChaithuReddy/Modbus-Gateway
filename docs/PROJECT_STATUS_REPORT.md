# FluxGen Modbus IoT Gateway - Project Status Report

**Prepared for**: Management Review Meeting
**Date**: March 26, 2026
**Project Lead**: Chaithu Reddy
**Current Version**: v1.3.8
**Platform**: ESP32-WROOM-32 (4MB Flash)

---

## 1. Executive Summary

The FluxGen Modbus IoT Gateway is a production-ready industrial IoT device that reads flow meters and water quality sensors via RS485 Modbus, and transmits data to Azure IoT Hub over WiFi or 4G cellular. The system is currently deployed in the field and has been operating reliably with 8+ days continuous uptime.

Version 1.3.8 brings significant memory optimizations, improved SD card reliability, new sensor support, and a production board testing tool.

---

## 2. System Architecture

```
                        ┌──────────────────────────┐
                        │      Azure IoT Hub       │
                        │   (Cloud Dashboard)      │
                        └────────────┬─────────────┘
                                     │ MQTT/TLS
                                     │
                        ┌────────────┴─────────────┐
                        │    ESP32 IoT Gateway      │
                        │    (FluxGen PCB Board)    │
                        ├───────────────────────────┤
                        │  WiFi 802.11 b/g/n       │
                        │  4G LTE (SIM A7670C)     │
                        │  RS485 Modbus RTU        │
                        │  SD Card (offline cache)  │
                        │  DS3231 RTC              │
                        │  Web Configuration UI    │
                        │  OTA Firmware Updates    │
                        └──┬─────────┬──────────┬──┘
                           │         │          │
                    ┌──────┴──┐ ┌────┴────┐ ┌───┴──────┐
                    │ Flow    │ │ Flow    │ │ Water    │
                    │ Meter 1 │ │ Meter 2 │ │ Quality  │
                    │ (RS485) │ │ (RS485) │ │ (RS485)  │
                    └─────────┘ └─────────┘ └──────────┘
```

### Key Components

| Component | Technology | Purpose |
|-----------|-----------|---------|
| MCU | ESP32-WROOM-32 (Dual Core, 240MHz) | Main controller |
| Communication | RS485 Modbus RTU (UART2) | Sensor data acquisition |
| Internet - WiFi | 802.11 b/g/n (STA + AP mode) | Primary connectivity |
| Internet - Cellular | SIM A7670C 4G LTE (PPP) | Backup connectivity |
| Cloud | Azure IoT Hub (MQTT/TLS) | Telemetry + remote management |
| Storage | SD Card (FAT32) + NVS Flash | Offline caching + config |
| Clock | DS3231 RTC (I2C) | Accurate timestamps offline |
| Configuration | Web UI on SoftAP (192.168.4.1) | Field setup without tools |
| Updates | OTA via GitHub Releases | Remote firmware updates |

---

## 3. Current Deployment Status

### Production Device: sssh_24

| Metric | Value |
|--------|-------|
| Location | Residential Overhead Tank (OHT) |
| Firmware | v1.3.7 |
| Network | 4G Cellular (SIM) |
| Uptime | 8+ days continuous |
| Messages Sent | 4,645 telemetry messages |
| Failures | 0 in current session |
| Free Memory | 86KB (healthy) |
| Restart Count | 35 (lifetime, across all versions) |

### Sensors Connected

| Sensor | Unit ID | Type | Location |
|--------|---------|------|----------|
| Residential OHT Inlet | FG24027F | Flow Meter (Slave 1) | Inlet pipe |
| Residential OHT Outlet | FG24028F | Flow Meter (Slave 2) | Outlet pipe |

### Azure IoT Hub Integration

- Device Twin for remote configuration (telemetry interval, Modbus settings)
- OTA firmware updates triggered via cloud
- Real-time telemetry with offline SD card caching
- Automatic data replay when connectivity restores

---

## 4. Features Delivered (v1.3.7 → v1.3.8)

### v1.3.7 (Deployed in Production)
- NVS partition expanded for more configuration storage
- Full Azure Device Twin support
- OTA updates from GitHub releases
- SD card offline caching with automatic replay
- Web-based configuration interface
- Dual connectivity: WiFi + 4G Cellular

### v1.3.8 (Ready for Deployment)

| Feature | Description | Impact |
|---------|-------------|--------|
| Memory Optimization | Saved ~20KB heap (max sensors 15→10, buffer reductions) | More headroom, fewer crashes |
| SD Card Error Recovery | 3-retry logic + RAM buffer fallback + auto-recovery | Zero data loss during SD errors |
| Telemetry Timeout Fix | Skip restart when SD card is caching data | No unnecessary reboots offline |
| SPI Bus Leak Fix | Free SPI bus on SD mount failure | SD works after restart |
| Aster pH/TDS Sensor | New water quality sensor type (pH + TDS) | Expanded sensor support |
| Auth System Removal | Removed unused login/WebSocket/provisioning | Smaller binary, within OTA limit |
| Stack Overflow Fix | Main task 3.5KB→8KB for web server start | Web toggle no longer crashes |
| Serial Test Command | Send "TEST" via USB for instant RS485 check | 3-second PCB board testing |

---

## 5. Supported Sensors

| Sensor Type | Model | Parameters | Protocol |
|-------------|-------|-----------|----------|
| Flow Meter | Dalian Ultrasonic | Cumulative flow, flow rate | Modbus RTU (0x03) |
| Water Quality | Aquadax | pH, TDS, Temp, Humidity, TSS, BOD, COD | Modbus RTU (0x03) |
| Water Quality | Opruss Ace | pH, Temp, Humidity | Modbus RTU (0x03) |
| Water Quality | Opruss Ace TDS | TDS, Temperature | Modbus RTU (0x04) |
| Water Quality | Aster | pH, TDS | Modbus RTU (0x03) |

Maximum 10 sensors per gateway (configurable mix of flow + quality).

---

## 6. Production QC Tool - Board Tester

Built an automated RS485 testing tool for new PCB boards:

### How It Works
1. Flash ONE ESP32 with firmware (one time)
2. Place ESP32 on new PCB, connect reference flow meter
3. Run `python board_tester.py batch` on laptop
4. Script sends TEST command → ESP32 reads sensor → PASS/FAIL in 3 seconds
5. Move ESP32 to next board, repeat

### Features
- 3 automatic retries per board
- Works during boot or after boot (auto-detects)
- Results logged to CSV for traceability
- No WiFi or web server needed — pure serial

### Test Output Example
```
TESTING: PCB-001
  [10.1s] [RS485_TEST] Serial test command received - reading sensors NOW
  [11.2s] [RECV] Received 9 bytes from RS485
  [11.2s] [DATA] Register[0]: 0x6804 (26628)
  [11.2s] [RS485_TEST] PASS: Read 1/1 sensors successfully

  ╔═════════════════════════════════════════╗
  ║   PASS        PCB-001   RS485 OK       ║
  ╚═════════════════════════════════════════╝
```

---

## 7. Hardware Design - SIM Module PCB Integration

Completed hardware analysis for integrating SIM A7670C 4G LTE module directly into the PCB:

### Pin Connections (ESP32 → A7670C)

| Function | ESP32 GPIO | A7670C Pin | Notes |
|----------|-----------|------------|-------|
| UART TX | GPIO 33 | RX | Data to modem |
| UART RX | GPIO 32 | TX | Data from modem |
| Reset | GPIO 2 | REST | Hardware reset |
| Power | GPIO 4 | POWER KEY | On/off control |
| Power Supply | — | VDD | 3.8V–4.2V, 2A peak |

### Critical Design Requirements
- 470uF bulk capacitor near VDD (2A peak during LTE TX)
- 1mm+ trace width for power lines
- Solid ground plane under module
- Antenna at board edge, 10mm clearance from copper
- KiCad footprint available in repository

---

## 8. Complete GPIO Allocation

| GPIO | Function | GPIO | Function |
|------|----------|------|----------|
| 0 | Boot button | 19 | SD Card MISO |
| 2 | SIM module reset | 21 | RTC SDA (I2C) |
| 4 | SIM module power | 22 | RTC SCL (I2C) |
| 5 | SD Card CLK | 23 | SD Card MOSI |
| 15 | SD Card CS | 25 | Web Server LED |
| 16 | RS485 RX | 26 | MQTT Status LED |
| 17 | RS485 TX | 27 | Sensor Status LED |
| 18 | RS485 RTS | 32 | SIM UART RX |
| 33 | SIM UART TX | 34 | Config button (input) |

All 19 GPIOs allocated with zero conflicts.

---

## 9. Technical Constraints

| Constraint | Detail | Mitigation |
|-----------|--------|------------|
| Partition table frozen | Cannot change via OTA (v1.3.7+) | Plan sizes generously upfront |
| OTA partition limit | 1.5MB per app slot | Binary size monitoring, removed unused features |
| Max 10 sensors | Reduced from 15 to save memory | Sufficient for current deployments |
| Heap budget | 80-90KB free (web off), 50-70KB (web on) | Continuous memory monitoring |
| 609KB web_config.c | Single large file, fragile to edit | Future: split into separate files |

---

## 10. OTA Update Strategy

```
Current device (v1.3.7) ──── OTA ────→ v1.3.8  ✓ Works (same partition)
Older devices (v1.3.6-)  ─── OTA ──X→ v1.3.7+  ✗ Manual flash required
```

- OTA triggered via Azure Device Twin (`ota_url` property)
- Automatic rollback protection on boot failure
- MQTT stopped during OTA to free ~30KB heap for TLS
- GitHub CDN certificate verification skipped (CDN certs problematic)

---

## 11. Upcoming Priorities

| Priority | Task | Timeline |
|----------|------|----------|
| High | Deploy v1.3.8 to production via OTA | This week |
| High | Test new PCB boards using batch tester | This week |
| High | Configure Azure credentials on dev board | Immediate |
| Medium | SIM module PCB integration (new board design) | Next sprint |
| Medium | Field test Aster water quality sensor | Next sprint |
| Low | Split web_config.c into smaller files | Backlog |
| Low | Investigate intermittent SD card heartbeat write failure | Backlog |

---

## 12. Key Metrics & KPIs

| Metric | Target | Actual |
|--------|--------|--------|
| Device uptime | >99% | 99.5% (8+ days, 35 restarts lifetime) |
| Telemetry delivery | >99% | 100% current session (4,645 messages) |
| Memory headroom | >30KB free | 86KB free (exceeds target) |
| OTA success rate | 100% | Tested, pending v1.3.8 rollout |
| Board QC test time | <1 min/board | ~10 seconds/board |
| Offline data retention | 100% | SD card + RAM fallback |

---

## 13. Risk Register

| Risk | Likelihood | Impact | Mitigation |
|------|-----------|--------|------------|
| SD card failure in field | Medium | Data loss during offline | 3-retry + RAM fallback + auto-recovery |
| Heap exhaustion crash | Low | Device restart | Memory optimizations, monitoring task |
| OTA bricking device | Low | Manual field visit | Rollback protection, dual OTA partitions |
| 4G coverage gaps | Medium | Temporary offline | SD card caching, auto-replay |
| Partition table change needed | Low | All devices need reflash | Table is frozen, plan around current layout |

---

## 14. Repository & Tools

| Resource | Location |
|----------|----------|
| Source Code | github.com/YChaithuReddy/Modbus-Gateway |
| Build System | ESP-IDF v5.5.1 |
| Cloud Platform | Azure IoT Hub (fluxgen-testhub) |
| Board Tester | `tests/board_tester.py` |
| Hardware Docs | `docs/` directory |
| SIM Module Ref | github.com (SIM-A7670C---4G-LTE-Modem) |

---

*Report prepared by FluxGen Technologies IoT Team*
*Firmware: v1.3.8 | Hardware: ESP32-WROOM-32 + A7670C LTE*
