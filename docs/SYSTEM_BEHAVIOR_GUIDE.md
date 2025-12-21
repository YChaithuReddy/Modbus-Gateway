# ESP32 Modbus IoT Gateway - System Behavior Guide

**Version:** 1.2.0
**Last Updated:** December 20, 2024
**Author:** Development Team

---

## Table of Contents

1. [Executive Summary](#1-executive-summary)
2. [System Architecture](#2-system-architecture)
3. [Network Behavior](#3-network-behavior)
4. [MQTT Connection Handling](#4-mqtt-connection-handling)
5. [Offline Data Caching](#5-offline-data-caching)
6. [Failure Scenarios & Recovery](#6-failure-scenarios--recovery)
7. [Remote Configuration (Device Twin)](#7-remote-configuration-device-twin)
8. [OTA Firmware Updates](#8-ota-firmware-updates)
9. [LED Status Indicators](#9-led-status-indicators)
10. [Configuration Parameters](#10-configuration-parameters)
11. [Recent Changes](#11-recent-changes)

---

## 1. Executive Summary

The ESP32 Modbus IoT Gateway is a production-ready industrial IoT device that:

- Reads data from Modbus RS485 sensors (flow meters, level sensors, energy meters)
- Transmits telemetry to Azure IoT Hub via MQTT over TLS
- Supports both WiFi and 4G/LTE (SIM) connectivity
- Caches data to SD card when offline
- Supports remote configuration via Azure Device Twin
- Provides OTA (Over-The-Air) firmware updates

**Key Design Principle:** The system is designed for **zero data loss** - if the network is unavailable, data is cached locally and automatically replayed when connectivity is restored.

---

## 2. System Architecture

### 2.1 Hardware Components

| Component | GPIO Pins | Purpose |
|-----------|-----------|---------|
| Modbus RS485 | TX=17, RX=16, RTS=18 | Sensor communication |
| SD Card (SPI) | CS=15, MOSI=23, MISO=19, CLK=5 | Offline data storage |
| DS3231 RTC | SDA=21, SCL=22 | Real-time clock |
| A7670C Modem | Configurable | 4G/LTE connectivity |
| Status LEDs | Web=25, MQTT=26, Sensor=27 | Visual status |
| Web Toggle | GPIO 34 or BOOT button | Enable/disable web server |

### 2.2 Dual-Core Task Distribution

| Core | Task | Priority | Purpose |
|------|------|----------|---------|
| Core 0 | Modbus Monitor | 5 | RS485 communication monitoring |
| Core 1 | MQTT Client | 4 | Azure IoT Hub connection |
| Core 1 | Telemetry Sender | 3 | Data transmission & caching |

### 2.3 Data Flow

```
[Modbus Sensors] --> [ESP32 Gateway] --> [Azure IoT Hub]
                          |
                          v
                     [SD Card Cache]
                     (when offline)
```

---

## 3. Network Behavior

### 3.1 WiFi Mode

| Event | System Response |
|-------|-----------------|
| WiFi connected | Proceeds to MQTT connection |
| WiFi disconnected | Attempts reconnection (15s timeout) |
| WiFi connected, no internet | Forces reconnection cycle |
| Reconnection failed | Caches data to SD card |

### 3.2 SIM/4G Mode

| Event | System Response |
|-------|-----------------|
| PPP connected | Proceeds to MQTT connection |
| PPP disconnected | Attempts reconnection (60s timeout) |
| Modem unresponsive | Triggers modem reset (power cycle) |
| Reset cooldown | 5-minute minimum between reset attempts |

### 3.3 Network Priority

1. Check network connection (WiFi or PPP)
2. If disconnected → Cache to SD card
3. If connected → Check MQTT status
4. If MQTT disconnected → Cache to SD card
5. If MQTT connected → Send telemetry

---

## 4. MQTT Connection Handling

### 4.1 Connection Details

| Parameter | Value |
|-----------|-------|
| Broker | Azure IoT Hub (port 8883, TLS) |
| Protocol | MQTT 3.1.1 |
| Keep-alive | 30 seconds |
| Authentication | SAS Token (1-hour validity) |
| Auto-reconnect | Enabled |

### 4.2 Connection States

```
┌─────────────┐     Connected      ┌─────────────┐
│ Disconnected│ ─────────────────> │  Connected  │
│             │ <───────────────── │             │
└─────────────┘    Disconnected    └─────────────┘
      │                                   │
      │ Cache to SD                       │ Send telemetry
      v                                   v
┌─────────────┐                    ┌─────────────┐
│  SD Card    │                    │ Azure IoT   │
│  Storage    │                    │    Hub      │
└─────────────┘                    └─────────────┘
```

### 4.3 Reconnection Behavior

| Scenario | Action |
|----------|--------|
| Normal disconnect | Auto-reconnect immediately |
| Network unavailable | Wait for network, then reconnect |
| SAS token expiring | Refresh token 5 minutes before expiry |
| 20+ reconnect failures | Log error (no restart by default) |

### 4.4 What Happens When MQTT Disconnects

1. **Immediate:** `mqtt_connected` flag set to `false`
2. **Data handling:** New telemetry cached to SD card
3. **Reconnection:** Automatic via MQTT library
4. **If network issue:** Modem reset triggered (if enabled)
5. **On reconnect:** Cached messages replayed before live data

---

## 5. Offline Data Caching

### 5.1 When Data is Cached

Data is cached to SD card when:

- WiFi/PPP network is disconnected
- MQTT client is not connected
- Azure IoT Hub is unreachable

### 5.2 Cache File Format

**Location:** `/sdcard/msgs.txt`

**Format:** `ID|TIMESTAMP|TOPIC|PAYLOAD`

**Example:**
```
1|2025-12-20T06:30:00Z|devices/test_100/messages/events/|{"unit_id":"ZEST001","type":"FLOW","consumption":"50.000","created_on":"2025-12-20T06:30:00Z"}
```

### 5.3 Replay Mechanism

When MQTT reconnects:

1. System detects cached messages
2. **Replays ALL cached messages BEFORE sending live data**
3. Maintains chronological order (oldest first)
4. Rate limiting prevents Azure throttling:
   - 500ms delay between messages
   - 10 messages per batch
   - 2 seconds between batches
5. Messages deleted from SD only after successful send

### 5.4 Corruption Protection

The system automatically detects and removes:

- Binary/garbage data in messages
- Invalid timestamps (year < 2024)
- Malformed message IDs
- Invalid UTF-8 sequences

---

## 6. Failure Scenarios & Recovery

### 6.1 Failure Response Matrix

| Failure Scenario | Immediate Response | Recovery Action | Data Impact |
|------------------|-------------------|-----------------|-------------|
| **Network goes offline** | Cache to SD card | Auto-reconnect | No data loss |
| **MQTT disconnects** | Cache to SD card | Auto-reconnect | No data loss |
| **MQTT disconnect during replay** | Stop replay, keep remaining on SD | Resume on reconnect | No data loss |
| **SD card full** | Delete 10 oldest messages | Retry save | Oldest data lost |
| **SD card error** | Unmount/remount attempt | Continue without caching | New data may be lost |
| **Modbus timeout** | Mark sensor as invalid | Retry next cycle | Sensor skipped |
| **SAS token expiry** | Refresh token | New MQTT connection | Brief interruption |
| **Power loss** | N/A | RTC preserves time, SD has FSYNC | Minimal data loss |
| **OTA failure** | Report to Azure | Manual retry via Twin | No data impact |

### 6.2 Network Offline Scenario

```
Timeline:
─────────────────────────────────────────────────────────────>

T=0:    Network disconnects
        └─> Cache enabled, mqtt_connected = false

T=5min: Telemetry interval
        └─> Data cached to SD card (not sent)

T=10min: Telemetry interval
         └─> Data cached to SD card

T=12min: Network reconnects
         └─> MQTT connects
         └─> Replay starts: Message 1, 2 sent with 500ms delays
         └─> Live telemetry resumes
```

### 6.3 SD Card Replay During MQTT Disconnect

**Problem Solved:** Previously, rapid message sending caused Azure IoT Hub to disconnect.

**Solution Implemented:**
- Increased delay between messages (100ms → 500ms)
- Reduced batch size (20 → 10 messages)
- Added connection checks before each message
- Changed from QoS 1 to QoS 0 for replay
- Wait 1 second after publish before removing from SD
- Stop replay immediately if MQTT disconnects

---

## 7. Remote Configuration (Device Twin)

### 7.1 Configurable Parameters

| Property | Type | Range | Description |
|----------|------|-------|-------------|
| `telemetry_interval` | int | 30-3600 | Seconds between telemetry |
| `modbus_retry_count` | int | 0-3 | Retry attempts for Modbus |
| `modbus_retry_delay` | int | 10-500 | Ms between retries |
| `batch_telemetry` | bool | - | Use flat JSON format |
| `web_server_enabled` | bool | - | Enable/disable web server |
| `maintenance_mode` | bool | - | Pause telemetry |
| `ota_enabled` | bool | - | Allow OTA updates |
| `ota_url` | string | - | Firmware URL (triggers OTA) |
| `sensors` | array | 0-10 | Remote sensor configuration |

### 7.2 Remote Sensor Configuration Example

```json
{
  "sensors": [
    {
      "name": "Main Flow",
      "unit_id": "ZEST001",
      "slave_id": 1,
      "sensor_type": "ZEST"
    }
  ]
}
```

**Supported Sensor Types with Auto-Presets:**

| Type | Register | Quantity | Description |
|------|----------|----------|-------------|
| ZEST | 4121 | 4 | AquaGen flow meter |
| Panda_EMF | 4114 | 4 | Electromagnetic flow meter |
| Panda_USM | 8 | 4 | Ultrasonic flow meter |
| Panda_Level | 1 | 1 | Water level sensor |
| Dailian_EMF | 2006 | 2 | Dailian flow meter |

### 7.3 Device Status Reporting

The device reports these properties to Azure every 5 minutes:

```json
{
  "telemetry_interval": 300,
  "firmware_version": "1.2.0",
  "device_id": "test_100",
  "network_mode": "WiFi",
  "web_server_enabled": true,
  "maintenance_mode": false,
  "free_heap": 72000,
  "uptime_sec": 3600,
  "sensor_count": 1
}
```

---

## 8. OTA Firmware Updates

### 8.1 OTA Process

1. Set `ota_url` in Device Twin desired properties
2. Device downloads firmware via HTTP/HTTPS
3. Writes to OTA partition
4. Reboots with new firmware
5. After first successful telemetry → firmware marked as valid
6. If validation fails → automatic rollback to previous version

### 8.2 OTA Safety Features

- **Rollback protection:** 5-minute confirmation window
- **Memory check:** Requires 40KB minimum free heap
- **Progress reporting:** Status updates to Device Twin
- **Retry logic:** 3 attempts per download

---

## 9. LED Status Indicators

| LED | GPIO | ON State | OFF State |
|-----|------|----------|-----------|
| Web Server | 25 | Web server running | Web server stopped |
| MQTT | 26 | Connected to Azure | Disconnected |
| Sensor | 27 | Sensors responding | No sensor response |

**Note:** LEDs are active-low (GPIO LOW = LED ON)

---

## 10. Configuration Parameters

### 10.1 Timing Parameters (iot_configs.h)

| Parameter | Value | Description |
|-----------|-------|-------------|
| `TELEMETRY_FREQUENCY_MILLISECS` | 300000 | 5 minutes default |
| `MAX_MQTT_RECONNECT_ATTEMPTS` | 20 | Before giving up |
| `WATCHDOG_TIMEOUT_SEC` | 120 | System restart if stuck |
| `TELEMETRY_TIMEOUT_SEC` | 1800 | 30 min without telemetry |
| `SD_REPLAY_DELAY_BETWEEN_MESSAGES_MS` | 500 | Rate limiting |
| `SD_REPLAY_DELAY_BETWEEN_BATCHES_MS` | 2000 | Batch delay |
| `SD_REPLAY_MAX_MESSAGES_PER_BATCH` | 10 | Messages per batch |

### 10.2 MQTT Parameters

| Parameter | Value |
|-----------|-------|
| Keep-alive | 30 seconds |
| SAS Token validity | 3600 seconds (1 hour) |
| Token refresh margin | 300 seconds (5 min before expiry) |

---

## 11. Recent Changes

### Version 1.2.0 (December 20, 2024)

#### Bug Fixes

1. **MQTT Disconnect During SD Replay**
   - **Problem:** Azure IoT Hub disconnected after 1-2 messages during SD card replay
   - **Cause:** Messages sent too fast (100ms delay), QoS 1 causing ACK buildup
   - **Solution:**
     - Increased delay to 500ms between messages
     - Reduced batch size to 10 messages
     - Changed to QoS 0 for replay
     - Added connection checks before each message
     - Wait for ACK before removing from SD

2. **Device Twin Sensor Presets Not Applied**
   - **Problem:** ZEST sensors configured via Device Twin used register 0 instead of 4121
   - **Cause:** `apply_sensor_type_presets()` not called in Device Twin parsing
   - **Solution:** Added preset application after parsing sensor configuration

3. **Telemetry JSON Format**
   - **Problem:** Complex nested JSON format (`{"body":[...],"properties":{...}}`)
   - **Solution:** Changed to simple flat format:
     ```json
     {"unit_id":"ZEST001","type":"FLOW","consumption":"50.000","created_on":"2025-12-20T07:11:16Z"}
     ```

#### New Features

- Configurable SD replay rate limiting via `iot_configs.h`
- ZEST and all flow meter types now mapped to `"type":"FLOW"`
- Improved logging for SD card replay progress

---

## Appendix A: Telemetry JSON Formats

### Flow Meter (ZEST, Panda_EMF, etc.)
```json
{"unit_id":"ZEST001","type":"FLOW","consumption":"50.000","created_on":"2025-12-20T07:11:16Z"}
```

### Level Sensor
```json
{"unit_id":"LEVEL01","type":"LEVEL","level_filled":"75","created_on":"2025-12-20T07:11:16Z"}
```

### Energy Meter
```json
{"ene_con_hex":"00004351","type":"ENERGY","created_on_epoch":1702213256,"slave_id":1,"meter":"meter1"}
```

---

## Appendix B: Troubleshooting

| Symptom | Possible Cause | Solution |
|---------|---------------|----------|
| No telemetry received | Network offline | Check WiFi/SIM connection |
| Telemetry delayed | SD replay in progress | Wait for replay to complete |
| MQTT keeps disconnecting | Rate limiting by Azure | Check SD replay settings |
| Sensor shows 0 value | Wrong register address | Verify sensor_type preset |
| OTA fails | Insufficient memory | Stop web server first |
| SD card errors | Card corruption | Reformat as FAT32 |

---

*Document generated for internal use. Contact development team for questions.*
