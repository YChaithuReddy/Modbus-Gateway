# Azure IoT Hub Device Twin Guide

This guide explains how to use Azure IoT Hub Device Twin to remotely configure and control your ESP32 Modbus IoT Gateway.

## Table of Contents

1. [Overview](#overview)
2. [Initial Device Setup](#initial-device-setup)
3. [Accessing Device Twin](#accessing-device-twin)
4. [Remote Sensor Configuration](#remote-sensor-configuration)
5. [Other Device Twin Properties](#other-device-twin-properties)
6. [OTA Firmware Updates](#ota-firmware-updates)
7. [Reported Properties](#reported-properties)
8. [Troubleshooting](#troubleshooting)

---

## Overview

Device Twin is a JSON document in Azure IoT Hub that stores device state information:

- **Desired Properties**: Set by the cloud to configure the device
- **Reported Properties**: Sent by the device to report its current state

When you update desired properties in Azure, the device automatically receives the changes and applies them.

### What You Can Control Remotely

| Feature | Property | Description |
|---------|----------|-------------|
| Sensor Configuration | `sensors` | Add/modify/remove Modbus sensors |
| Telemetry Interval | `telemetry_interval` | Data reporting frequency (30-3600 sec) |
| OTA Updates | `ota_url`, `ota_enabled` | Trigger firmware updates |
| Web Server | `web_server_enabled` | Start/stop the configuration web server |
| Maintenance Mode | `maintenance_mode` | Pause all telemetry |
| Remote Reboot | `reboot_device` | Restart the device |
| Modbus Retry | `modbus_retry_count` | Number of read retries (0-3) |
| Modbus Delay | `modbus_retry_delay` | Delay between retries (10-500 ms) |
| Batch Telemetry | `batch_telemetry` | Send all sensors in one message |

---

## Initial Device Setup

Before using Device Twin, you must configure the device once via the local web interface:

### Step 1: Connect to Device AP

1. Power on the ESP32 device
2. Connect to WiFi: `ModbusGateway-XXXX` (password: `modbus123`)
3. Open browser: `http://192.168.4.1`

### Step 2: Configure Azure Credentials

In the web interface, enter:

| Field | Example | Description |
|-------|---------|-------------|
| Azure Hub FQDN | `your-hub.azure-devices.net` | Your IoT Hub hostname |
| Device ID | `device-001` | The device ID registered in IoT Hub |
| Device Key | `abc123...` | Primary or secondary key from IoT Hub |

### Step 3: Configure Network

Choose either:

**WiFi Mode:**
- Enter your WiFi SSID and password

**SIM Mode:**
- Enable SIM module
- Enter APN (e.g., `airteliot` for Airtel, `jionet` for Jio)

### Step 4: Save and Reboot

Click "Save Configuration" and the device will reboot and connect to Azure.

After this one-time setup, you can configure everything else via Device Twin.

---

## Accessing Device Twin

### Azure Portal

1. Go to [Azure Portal](https://portal.azure.com)
2. Navigate to: **IoT Hub** → **Devices** → **[Your Device]**
3. Click **Device Twin** in the top menu

### Azure CLI

```bash
# View Device Twin
az iot hub device-twin show --hub-name YourHubName --device-id device-001

# Update desired properties
az iot hub device-twin update --hub-name YourHubName --device-id device-001 \
  --desired '{"telemetry_interval": 60}'
```

### VS Code (Azure IoT Tools Extension)

1. Install "Azure IoT Tools" extension
2. Connect to your IoT Hub
3. Right-click device → "Edit Device Twin"

---

## Remote Sensor Configuration

The most powerful feature is configuring sensors remotely via the `sensors` array.

### Basic Sensor Configuration

Add this to Device Twin desired properties:

```json
{
  "sensors": [
    {
      "enabled": true,
      "name": "Flow Meter 1",
      "unit_id": "FM001",
      "slave_id": 1,
      "baud_rate": 9600,
      "parity": "none",
      "register_address": 0,
      "quantity": 2,
      "data_type": "FLOAT32",
      "register_type": "HOLDING",
      "scale_factor": 1.0,
      "byte_order": "BIG_ENDIAN",
      "sensor_type": "Flow-Meter",
      "description": "Main inlet flow meter"
    }
  ]
}
```

### Sensor Field Reference

| Field | Type | Required | Default | Description |
|-------|------|----------|---------|-------------|
| `enabled` | boolean | No | `true` | Enable/disable sensor |
| `name` | string | Yes | - | Display name (max 31 chars) |
| `unit_id` | string | No | "" | Unique identifier for telemetry |
| `slave_id` | number | Yes | 1 | Modbus slave address (1-247) |
| `baud_rate` | number | No | 9600 | Serial baud rate |
| `parity` | string | No | "none" | "none", "even", "odd" |
| `register_address` | number | Yes | 0 | Starting register address |
| `quantity` | number | No | 1 | Number of registers to read |
| `data_type` | string | No | "UINT16" | See data types below |
| `register_type` | string | No | "HOLDING" | "HOLDING" or "INPUT" |
| `scale_factor` | number | No | 1.0 | Multiply raw value by this |
| `byte_order` | string | No | "BIG_ENDIAN" | Byte ordering |
| `sensor_type` | string | No | "Flow-Meter" | Sensor category |
| `description` | string | No | "" | Optional description |

### Supported Data Types

#### Basic Data Types (16-bit)

| Data Type | Registers | Description |
|-----------|-----------|-------------|
| `UINT16` | 1 | Unsigned 16-bit integer (Big Endian) |
| `UINT16_BE` | 1 | Unsigned 16-bit integer (Big Endian) |
| `UINT16_LE` | 1 | Unsigned 16-bit integer (Little Endian) |
| `UINT16_HI` | 1 | Unsigned 16-bit, high byte first |
| `UINT16_LO` | 1 | Unsigned 16-bit, low byte first |
| `INT16` | 1 | Signed 16-bit integer (Big Endian) |
| `INT16_BE` | 1 | Signed 16-bit integer (Big Endian) |
| `INT16_LE` | 1 | Signed 16-bit integer (Little Endian) |
| `INT16_HI` | 1 | Signed 16-bit, high byte first |
| `INT16_LO` | 1 | Signed 16-bit, low byte first |
| `UINT8` | 1 | Unsigned 8-bit integer |
| `INT8` | 1 | Signed 8-bit integer |
| `BOOL` | 1 | Boolean (0 or 1) |

#### 32-bit Integer Types

| Data Type | Registers | Byte Order | Description |
|-----------|-----------|------------|-------------|
| `UINT32` | 2 | ABCD | Unsigned 32-bit (Big Endian, default) |
| `UINT32_1234` | 2 | ABCD | Unsigned 32-bit (Big Endian) |
| `UINT32_ABCD` | 2 | ABCD | Unsigned 32-bit (Big Endian) |
| `UINT32_4321` | 2 | DCBA | Unsigned 32-bit (Little Endian) |
| `UINT32_DCBA` | 2 | DCBA | Unsigned 32-bit (Little Endian) |
| `UINT32_SWAP` | 2 | DCBA | Unsigned 32-bit (Byte Swapped) |
| `UINT32_2143` | 2 | BADC | Unsigned 32-bit (Word Swapped) |
| `UINT32_BADC` | 2 | BADC | Unsigned 32-bit (Word Swapped) |
| `UINT32_3412` | 2 | CDAB | Unsigned 32-bit (Word Swapped LE) |
| `UINT32_CDAB` | 2 | CDAB | Unsigned 32-bit (Word Swapped LE) |
| `INT32` | 2 | ABCD | Signed 32-bit (Big Endian, default) |
| `INT32_1234` | 2 | ABCD | Signed 32-bit (Big Endian) |
| `INT32_ABCD` | 2 | ABCD | Signed 32-bit (Big Endian) |
| `INT32_4321` | 2 | DCBA | Signed 32-bit (Little Endian) |
| `INT32_DCBA` | 2 | DCBA | Signed 32-bit (Little Endian) |
| `INT32_SWAP` | 2 | DCBA | Signed 32-bit (Byte Swapped) |
| `INT32_2143` | 2 | BADC | Signed 32-bit (Word Swapped) |
| `INT32_BADC` | 2 | BADC | Signed 32-bit (Word Swapped) |
| `INT32_3412` | 2 | CDAB | Signed 32-bit (Word Swapped LE) |
| `INT32_CDAB` | 2 | CDAB | Signed 32-bit (Word Swapped LE) |

#### 32-bit Floating Point Types

| Data Type | Registers | Byte Order | Description |
|-----------|-----------|------------|-------------|
| `FLOAT32` | 2 | ABCD | IEEE 754 Float (Big Endian, default) |
| `FLOAT32_1234` | 2 | ABCD | IEEE 754 Float (Big Endian) |
| `FLOAT32_ABCD` | 2 | ABCD | IEEE 754 Float (Big Endian) |
| `FLOAT32_4321` | 2 | DCBA | IEEE 754 Float (Little Endian) |
| `FLOAT32_DCBA` | 2 | DCBA | IEEE 754 Float (Little Endian) |
| `FLOAT32_SWAP` | 2 | DCBA | IEEE 754 Float (Byte Swapped) |
| `FLOAT32_2143` | 2 | BADC | IEEE 754 Float (Word Swapped) |
| `FLOAT32_BADC` | 2 | BADC | IEEE 754 Float (Word Swapped) |
| `FLOAT32_3412` | 2 | CDAB | IEEE 754 Float (Word Swapped LE) |
| `FLOAT32_CDAB` | 2 | CDAB | IEEE 754 Float (Word Swapped LE) |

#### 64-bit Types

| Data Type | Registers | Byte Order | Description |
|-----------|-----------|------------|-------------|
| `FLOAT64` | 4 | 12345678 | 64-bit Double (Big Endian) |
| `FLOAT64_12345678` | 4 | ABCDEFGH | 64-bit Double (Big Endian) |
| `FLOAT64_87654321` | 4 | HGFEDCBA | 64-bit Double (Little Endian) |
| `FLOAT64_78563412` | 4 | GHEFCDAB | 64-bit Double (Mixed byte order) |
| `INT64_12345678` | 4 | ABCDEFGH | 64-bit Signed Integer (Big Endian) |
| `INT64_87654321` | 4 | HGFEDCBA | 64-bit Signed Integer (Little Endian) |
| `INT64_78563412` | 4 | GHEFCDAB | 64-bit Signed Integer (Mixed) |
| `UINT64_12345678` | 4 | ABCDEFGH | 64-bit Unsigned Integer (Big Endian) |
| `UINT64_87654321` | 4 | HGFEDCBA | 64-bit Unsigned Integer (Little Endian) |
| `UINT64_78563412` | 4 | GHEFCDAB | 64-bit Unsigned Integer (Mixed) |

#### Special Data Types

| Data Type | Registers | Description |
|-----------|-----------|-------------|
| `HEX` | Variable | Raw hexadecimal data |
| `ASCII` | Variable | ASCII string data |
| `PDU` | Variable | Raw PDU data |

### Supported Sensor Types (Flow Meters)

Use `sensor_type` field to enable special handling for specific meter brands:

| Sensor Type | Registers | Description | Register Format |
|-------------|-----------|-------------|-----------------|
| `Flow-Meter` | 4 | Generic flow meter | UINT32_BADC + FLOAT32_BADC (integer + decimal) |
| `Panda_EMF` | 4 | Panda Electromagnetic | INT32_BE + FLOAT32_BE at reg 4114 (0x1012) |
| `Panda_USM` | 4 | Panda Ultrasonic | FLOAT64 (64-bit double) |
| `Panda_Level` | 1 | Panda Level Sensor | UINT16 at reg 1 (0x0001) |
| `ZEST` | 4 | ZEST/AquaGen Flow | UINT16 + FLOAT32 at reg 4121 (0x1019) |
| `Clampon` | 4 | Clampon Flow Meter | UINT32_BADC + FLOAT32_BADC |
| `Dailian_EMF` | 2 | Dailian Electromagnetic | UINT32 word-swapped at reg 2006 (0x07D6) |
| `Level` | Variable | Generic level sensor | Uses sensor_height & max_water_level |
| `Radar Level` | Variable | Radar level sensor | Similar to Level |
| `QUALITY` | Variable | Water quality multi-sensor | Uses sub_sensors array |
| `ENERGY` | Variable | Energy meter | Uses meter_type field |

### Flow Meter Configuration Examples

#### Panda EMF (Electromagnetic Flow Meter)

```json
{
  "name": "Panda EMF Meter",
  "unit_id": "PANDA001",
  "slave_id": 1,
  "baud_rate": 9600,
  "register_address": 4114,
  "quantity": 4,
  "sensor_type": "Panda_EMF",
  "description": "Panda electromagnetic flow meter"
}
```

#### ZEST/AquaGen Flow Meter

```json
{
  "name": "ZEST Flow Meter",
  "unit_id": "ZEST001",
  "slave_id": 1,
  "baud_rate": 9600,
  "register_address": 4121,
  "quantity": 4,
  "sensor_type": "ZEST",
  "description": "AquaGen ZEST flow meter"
}
```

#### Panda USM (Ultrasonic Flow Meter)

```json
{
  "name": "Panda USM Meter",
  "unit_id": "USM001",
  "slave_id": 1,
  "baud_rate": 9600,
  "register_address": 4,
  "quantity": 4,
  "sensor_type": "Panda_USM",
  "description": "Panda ultrasonic flow meter (64-bit double)"
}
```

#### Dailian EMF (Electromagnetic Flow Meter)

```json
{
  "name": "Dailian EMF",
  "unit_id": "DAILIAN001",
  "slave_id": 1,
  "baud_rate": 9600,
  "register_address": 2006,
  "quantity": 2,
  "sensor_type": "Dailian_EMF",
  "scale_factor": 0.001,
  "description": "Dailian electromagnetic flow meter"
}
```

#### Clampon Flow Meter

```json
{
  "name": "Clampon Meter",
  "unit_id": "CLAMP001",
  "slave_id": 1,
  "baud_rate": 9600,
  "register_address": 0,
  "quantity": 4,
  "sensor_type": "Clampon",
  "description": "Clampon clamp-on flow meter"
}
```

#### Generic Flow Meter (UINT32 + FLOAT32)

```json
{
  "name": "Generic Flow",
  "unit_id": "FLOW001",
  "slave_id": 1,
  "baud_rate": 9600,
  "register_address": 0,
  "quantity": 4,
  "sensor_type": "Flow-Meter",
  "description": "Generic flow meter with BADC byte order"
}
```

#### Panda Level Sensor

```json
{
  "name": "Tank Level",
  "unit_id": "LEVEL001",
  "slave_id": 1,
  "baud_rate": 9600,
  "register_address": 1,
  "quantity": 1,
  "sensor_type": "Panda_Level",
  "sensor_height": 5.0,
  "max_water_level": 4.5,
  "description": "Panda ultrasonic level sensor"
}
```

### Byte Order Options

| Value | Description | Example |
|-------|-------------|---------|
| `BIG_ENDIAN` | Most significant byte first (ABCD) | 0x12345678 → [0x12, 0x34, 0x56, 0x78] |
| `LITTLE_ENDIAN` | Least significant byte first (DCBA) | 0x12345678 → [0x78, 0x56, 0x34, 0x12] |
| `MID_BIG_ENDIAN` | Word-swapped big endian (BADC) | 0x12345678 → [0x34, 0x12, 0x78, 0x56] |
| `MID_LITTLE_ENDIAN` | Word-swapped little endian (CDAB) | 0x12345678 → [0x56, 0x78, 0x12, 0x34] |

### Multiple Sensors Example

```json
{
  "sensors": [
    {
      "name": "Inlet Flow",
      "unit_id": "INLET01",
      "slave_id": 1,
      "register_address": 4114,
      "quantity": 4,
      "data_type": "Panda_EMF",
      "sensor_type": "Flow-Meter"
    },
    {
      "name": "Outlet Flow",
      "unit_id": "OUTLET01",
      "slave_id": 2,
      "register_address": 4114,
      "quantity": 4,
      "data_type": "Panda_EMF",
      "sensor_type": "Flow-Meter"
    },
    {
      "name": "Tank Level",
      "unit_id": "TANK01",
      "slave_id": 3,
      "register_address": 0,
      "quantity": 2,
      "data_type": "FLOAT32",
      "sensor_type": "Level",
      "sensor_height": 5.0,
      "max_water_level": 4.5
    }
  ]
}
```

### Water Quality Sensor with Sub-Sensors

For multi-parameter water quality sensors:

```json
{
  "sensors": [
    {
      "name": "Water Quality Monitor",
      "unit_id": "WQ001",
      "slave_id": 1,
      "sensor_type": "QUALITY",
      "sub_sensors": [
        {
          "enabled": true,
          "parameter_name": "pH",
          "json_key": "pH",
          "register_address": 100,
          "quantity": 2,
          "data_type": "FLOAT32",
          "register_type": "HOLDING",
          "scale_factor": 1.0,
          "byte_order": "BIG_ENDIAN",
          "units": "pH"
        },
        {
          "enabled": true,
          "parameter_name": "Temperature",
          "json_key": "temp",
          "register_address": 102,
          "quantity": 2,
          "data_type": "FLOAT32",
          "units": "degC"
        },
        {
          "enabled": true,
          "parameter_name": "TDS",
          "json_key": "tds",
          "register_address": 104,
          "quantity": 2,
          "data_type": "FLOAT32",
          "units": "ppm"
        },
        {
          "enabled": true,
          "parameter_name": "Turbidity",
          "json_key": "turb",
          "register_address": 106,
          "quantity": 2,
          "data_type": "FLOAT32",
          "units": "NTU"
        }
      ]
    }
  ]
}
```

### Sensor with Calculation Engine

Apply calculations to raw sensor values:

```json
{
  "sensors": [
    {
      "name": "Tank Volume",
      "unit_id": "TANKVOL01",
      "slave_id": 1,
      "register_address": 0,
      "quantity": 2,
      "data_type": "FLOAT32",
      "sensor_type": "Level",
      "calculation": {
        "calc_type": 4,
        "tank_diameter": 2.5,
        "tank_height": 3.0,
        "volume_unit": 0,
        "output_unit": "L",
        "decimal_places": 0
      }
    }
  ]
}
```

#### Calculation Types

| calc_type | Name | Description |
|-----------|------|-------------|
| 0 | NONE | No calculation |
| 1 | COMBINE_REGISTERS | Combine HIGH×multiplier + LOW |
| 2 | SCALE_OFFSET | value × scale + offset |
| 3 | LEVEL_PERCENTAGE | Convert level to percentage |
| 4 | CYLINDER_VOLUME | Calculate cylinder tank volume |
| 5 | RECTANGLE_VOLUME | Calculate rectangular tank volume |
| 6 | DIFFERENCE | value1 - value2 |
| 7 | FLOW_RATE_PULSE | Pulse counting for flow |
| 8 | LINEAR_INTERPOLATION | Map input range to output range |
| 9 | POLYNOMIAL | ax² + bx + c |
| 10 | FLOW_INT_DECIMAL | Integer + decimal from two registers |

---

## Other Device Twin Properties

### Telemetry Interval

Change how often data is sent (in seconds):

```json
{
  "telemetry_interval": 60
}
```

Valid range: 30 to 3600 seconds (30 sec to 1 hour)

### Modbus Settings

Adjust retry behavior for unreliable connections:

```json
{
  "modbus_retry_count": 2,
  "modbus_retry_delay": 100
}
```

- `modbus_retry_count`: 0-3 retries
- `modbus_retry_delay`: 10-500 ms between retries

### Batch Telemetry

Send all sensor data in a single message:

```json
{
  "batch_telemetry": true
}
```

### Web Server Control

Enable/disable the configuration web server remotely:

```json
{
  "web_server_enabled": true
}
```

Note: Web server is automatically disabled during OTA updates to free memory.

### Maintenance Mode

Pause all telemetry (useful during sensor maintenance):

```json
{
  "maintenance_mode": true
}
```

Set to `false` to resume normal operation.

### Remote Reboot

Trigger a device restart:

```json
{
  "reboot_device": true
}
```

The device will reboot after 3 seconds. This property auto-resets to `false`.

---

## OTA Firmware Updates

Update device firmware remotely:

### Step 1: Host Firmware File

Upload your `.bin` file to a publicly accessible HTTPS URL (e.g., GitHub Releases).

### Step 2: Trigger Update

```json
{
  "ota_enabled": true,
  "ota_url": "https://github.com/YourRepo/releases/download/v1.1.0/firmware.bin"
}
```

### OTA Process

1. Device receives new `ota_url`
2. Stops web server to free memory (if running)
3. Downloads firmware over HTTPS
4. Validates and installs to OTA partition
5. Reboots to new firmware
6. If boot fails, automatically rolls back

### OTA Status in Reported Properties

Monitor progress via reported properties:

| ota_status | Description |
|------------|-------------|
| `idle` | No OTA in progress |
| `downloading` | Downloading firmware |
| `verifying` | Validating firmware |
| `installing` | Writing to flash |
| `success` | Update complete, rebooting |
| `failed` | Update failed (see ota_error) |

### Disable OTA

To prevent accidental updates:

```json
{
  "ota_enabled": false
}
```

---

## Reported Properties

The device automatically reports its state. You can view these in Device Twin:

```json
{
  "reported": {
    "telemetry_interval": 300,
    "modbus_retry_count": 1,
    "modbus_retry_delay": 50,
    "batch_telemetry": false,
    "sensor_count": 2,
    "firmware_version": "1.0.0",
    "device_id": "device-001",
    "network_mode": "WiFi",
    "web_server_enabled": false,
    "maintenance_mode": false,
    "ota_enabled": true,
    "ota_status": "idle",
    "free_heap": 125000,
    "uptime_sec": 3600.5,
    "sensors": [
      {
        "name": "Inlet Flow",
        "unit_id": "INLET01",
        "slave_id": 1,
        "type": "Flow-Meter",
        "enabled": true
      },
      {
        "name": "Tank Level",
        "unit_id": "TANK01",
        "slave_id": 3,
        "type": "Level",
        "enabled": true
      }
    ]
  }
}
```

---

## Troubleshooting

### Sensors Not Updating

1. **Check JSON syntax**: Use a JSON validator
2. **Check $version**: Azure increments this automatically; device ignores duplicate versions
3. **Check logs**: Monitor device serial output for errors

### OTA Fails

1. **URL accessible?**: Test URL in browser
2. **HTTPS required**: HTTP URLs are rejected
3. **Memory**: Device needs ~50KB free heap
4. **File size**: Must fit in OTA partition (~1.5MB)

### Device Not Receiving Twin Updates

1. **MQTT connected?**: Check reported `mqtt_connected` status
2. **Credentials correct?**: Verify Device Key in web config
3. **Network stable?**: Check WiFi/SIM signal strength

### Configuration Not Persisting

1. **NVS space**: Device has limited NVS storage
2. **Save error**: Check logs for NVS write errors
3. **Reboot required**: Some changes need restart

### Getting Device Logs

Connect via USB serial at 115200 baud to see real-time logs:

```bash
# Windows
idf.py -p COM3 monitor

# Linux
idf.py -p /dev/ttyUSB0 monitor
```

Look for `[TWIN]` tagged messages for Device Twin operations.

---

## Complete Example

Here's a complete desired properties configuration:

```json
{
  "desired": {
    "telemetry_interval": 300,
    "modbus_retry_count": 1,
    "modbus_retry_delay": 50,
    "batch_telemetry": true,
    "web_server_enabled": false,
    "maintenance_mode": false,
    "ota_enabled": true,
    "sensors": [
      {
        "enabled": true,
        "name": "BWSSB Inlet Meter",
        "unit_id": "BWSSB001",
        "slave_id": 1,
        "baud_rate": 9600,
        "parity": "none",
        "register_address": 4114,
        "quantity": 4,
        "data_type": "Panda_EMF",
        "register_type": "HOLDING",
        "scale_factor": 1.0,
        "byte_order": "BIG_ENDIAN",
        "sensor_type": "Flow-Meter",
        "description": "Main inlet electromagnetic flow meter"
      },
      {
        "enabled": true,
        "name": "Storage Tank Level",
        "unit_id": "TANK001",
        "slave_id": 2,
        "baud_rate": 9600,
        "register_address": 0,
        "quantity": 2,
        "data_type": "FLOAT32",
        "sensor_type": "Level",
        "sensor_height": 5.0,
        "max_water_level": 4.5,
        "calculation": {
          "calc_type": 4,
          "tank_diameter": 3.0,
          "tank_height": 5.0,
          "volume_unit": 0,
          "output_unit": "L",
          "decimal_places": 0
        }
      }
    ]
  }
}
```

---

## Support

For issues or questions:
- GitHub Issues: [https://github.com/YChaithuReddy/Modbus-Gateway/issues](https://github.com/YChaithuReddy/Modbus-Gateway/issues)
- Check device logs via serial monitor for detailed error messages
