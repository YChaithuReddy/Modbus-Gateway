# Azure IoT Hub - Device Twin & C2D Commands Reference

ESP32 Modbus IoT Gateway v1.0.0

## Table of Contents
- [Device Twin Properties](#device-twin-properties)
  - [Desired Properties (Cloud to Device)](#desired-properties-cloud-to-device)
  - [Reported Properties (Device to Cloud)](#reported-properties-device-to-cloud)
- [C2D Commands](#c2d-commands-cloud-to-device-messages)
  - [System Commands](#system-commands)
  - [Configuration Commands](#configuration-commands)
  - [Sensor Management](#sensor-management)
  - [OTA Update Commands](#ota-update-commands)
  - [Diagnostic Commands](#diagnostic-commands)

---

## Device Twin Properties

### Desired Properties (Cloud to Device)

Set these properties in Azure IoT Hub Device Twin to configure the device remotely.

| Property | Type | Range/Values | Description |
|----------|------|--------------|-------------|
| `telemetry_interval` | number | 30-3600 | Telemetry sending interval in seconds |
| `modbus_retry_count` | number | 0-3 | Number of Modbus read retries on failure |
| `modbus_retry_delay` | number | 10-500 | Delay between retries in milliseconds |
| `batch_telemetry` | boolean | true/false | Send all sensor data in single message |
| `web_server_enabled` | boolean | true/false | Enable/disable web configuration server |
| `maintenance_mode` | boolean | true/false | Pause telemetry when true |
| `reboot_device` | boolean | true | Trigger remote reboot (one-shot) |
| `ota_enabled` | boolean | true/false | Allow/block OTA firmware updates |
| `ota_url` | string | URL | Firmware download URL (triggers OTA) |

#### Example: Set Desired Properties via Azure Portal/CLI

```json
{
  "properties": {
    "desired": {
      "telemetry_interval": 60,
      "modbus_retry_count": 2,
      "modbus_retry_delay": 100,
      "batch_telemetry": true,
      "web_server_enabled": false,
      "maintenance_mode": false,
      "ota_enabled": true
    }
  }
}
```

#### Trigger OTA Update via Device Twin

```json
{
  "properties": {
    "desired": {
      "ota_enabled": true,
      "ota_url": "https://github.com/user/repo/releases/download/v1.0.1/firmware.bin"
    }
  }
}
```

---

### Reported Properties (Device to Cloud)

These properties are reported by the device automatically.

| Property | Type | Description |
|----------|------|-------------|
| `telemetry_interval` | number | Current telemetry interval |
| `modbus_retry_count` | number | Current retry count setting |
| `modbus_retry_delay` | number | Current retry delay setting |
| `batch_telemetry` | boolean | Batch mode status |
| `sensor_count` | number | Number of configured sensors |
| `firmware_version` | string | Current firmware version |
| `device_id` | string | Azure IoT Hub device ID |
| `network_mode` | string | "WiFi" or "SIM" |
| `web_server_enabled` | boolean | Web server running status |
| `maintenance_mode` | boolean | Maintenance mode status |
| `ota_enabled` | boolean | OTA updates allowed |
| `ota_url` | string | Last OTA URL used |
| `ota_status` | string | "idle", "downloading", "success", "failed" |
| `free_heap` | number | Available heap memory in bytes |
| `uptime_sec` | number | Device uptime in seconds |
| `last_boot_time` | number | Time since boot in seconds |

---

## C2D Commands (Cloud-to-Device Messages)

Send JSON commands to the device via Azure IoT Hub C2D messaging.

**Base format:**
```json
{"command": "<command_name>", ...parameters}
```
or
```json
{"cmd": "<command_name>", ...parameters}
```

---

### System Commands

#### `ping`
Health check - device responds with "PING received - device is alive!"
```json
{"command": "ping"}
```

#### `restart`
Restart the device after 3 seconds
```json
{"command": "restart"}
```

#### `get_status`
Trigger immediate telemetry send
```json
{"command": "get_status"}
```

#### `toggle_webserver`
Toggle web server on/off
```json
{"command": "toggle_webserver"}
```

#### `led_test`
Flash all LEDs for 2 seconds
```json
{"command": "led_test"}
```

#### `factory_reset`
Reset device to factory defaults (requires confirmation)
```json
{"command": "factory_reset", "confirm": "CONFIRM_RESET"}
```

#### `sync_time`
Trigger NTP time synchronization
```json
{"command": "sync_time"}
```

#### `help`
List all available commands (output to device logs)
```json
{"command": "help"}
```

---

### Configuration Commands

#### `set_telemetry_interval`
Set telemetry sending interval
```json
{"command": "set_telemetry_interval", "interval": 60}
```
- `interval`: 30-3600 seconds

#### `set_modbus_retry`
Configure Modbus retry settings
```json
{"command": "set_modbus_retry", "count": 2, "delay": 100}
```
- `count`: 0-3 retries
- `delay`: 10-500 ms

#### `set_batch_mode`
Enable/disable batch telemetry mode
```json
{"command": "set_batch_mode", "enabled": true}
```

#### `get_config`
Get current device configuration
```json
{"command": "get_config"}
```

---

### Sensor Management

#### `add_sensor`
Add a new sensor (max 10 sensors)
```json
{
  "command": "add_sensor",
  "sensor": {
    "name": "Flow Meter 1",
    "unit_id": "FM001",
    "slave_id": 1,
    "register_address": 0,
    "quantity": 2,
    "data_type": "FLOAT32",
    "register_type": "HOLDING",
    "byte_order": "ABCD",
    "parity": "none",
    "sensor_type": "Flow-Meter",
    "scale_factor": 1.0,
    "baud_rate": 9600,
    "description": "Main flow meter",
    "enabled": true
  }
}
```

**Sensor Parameters:**
| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `name` | string | - | Sensor display name (max 31 chars) |
| `unit_id` | string | - | Unit identifier (max 15 chars) |
| `slave_id` | number | 1 | Modbus slave address (1-247) |
| `register_address` | number | 0 | Starting register address |
| `quantity` | number | 2 | Number of registers to read |
| `data_type` | string | "FLOAT32" | Data type (see below) |
| `register_type` | string | "HOLDING" | "HOLDING" or "INPUT" |
| `byte_order` | string | "ABCD" | Byte order (ABCD, DCBA, BADC, CDAB) |
| `parity` | string | "none" | Serial parity (none, even, odd) |
| `sensor_type` | string | "Flow-Meter" | Sensor category |
| `scale_factor` | number | 1.0 | Value multiplier |
| `baud_rate` | number | 9600 | Serial baud rate |
| `description` | string | - | Description (max 63 chars) |
| `enabled` | boolean | true | Enable/disable sensor |

**Supported Data Types:**
- `INT16`, `UINT16`
- `INT32`, `UINT32`
- `FLOAT32`
- `FLOAT64`
- `ZEST_FIXED` (special format)

#### `update_sensor`
Update an existing sensor's configuration
```json
{
  "command": "update_sensor",
  "index": 0,
  "updates": {
    "enabled": false,
    "scale_factor": 0.5,
    "name": "Updated Name"
  }
}
```

#### `delete_sensor`
Delete a sensor by index
```json
{"command": "delete_sensor", "index": 0}
```

#### `list_sensors`
List all configured sensors (output to device logs)
```json
{"command": "list_sensors"}
```

#### `get_sensors`
Get summary of enabled sensors
```json
{"command": "get_sensors"}
```

#### `read_sensor`
Read a specific sensor immediately
```json
{"command": "read_sensor", "index": 0}
```

---

### OTA Update Commands

#### `ota_update`
Start OTA firmware update from URL
```json
{
  "command": "ota_update",
  "url": "https://github.com/user/repo/releases/download/v1.0.1/firmware.bin",
  "version": "1.0.1"
}
```
- Supports GitHub releases with redirects
- Device will auto-reboot after successful download
- Web server is stopped during OTA to free memory

#### `ota_status`
Check OTA update status
```json
{"command": "ota_status"}
```
Returns: status, progress percentage, bytes downloaded, error message if any

#### `ota_cancel`
Cancel ongoing OTA update
```json
{"command": "ota_cancel"}
```

#### `ota_confirm`
Mark current firmware as valid (prevents rollback)
```json
{"command": "ota_confirm"}
```

#### `ota_reboot`
Apply pending OTA update (manual reboot)
```json
{"command": "ota_reboot"}
```

---

### Diagnostic Commands

#### `get_heap`
Get memory status
```json
{"command": "get_heap"}
```
Returns: free heap, minimum free heap, largest free block

#### `get_network`
Get network status
```json
{"command": "get_network"}
```
Returns: network mode, MQTT connection status, telemetry count, reconnect count

#### `reset_stats`
Reset telemetry and reconnection statistics to zero
```json
{"command": "reset_stats"}
```

---

## Azure CLI Examples

### Send C2D Command
```bash
az iot device c2d-message send \
  --hub-name YOUR_IOT_HUB \
  --device-id YOUR_DEVICE_ID \
  --data '{"command": "ping"}'
```

### Update Device Twin Desired Properties
```bash
az iot hub device-twin update \
  --hub-name YOUR_IOT_HUB \
  --device-id YOUR_DEVICE_ID \
  --desired '{"telemetry_interval": 60, "batch_telemetry": true}'
```

### Trigger OTA Update
```bash
az iot hub device-twin update \
  --hub-name YOUR_IOT_HUB \
  --device-id YOUR_DEVICE_ID \
  --desired '{"ota_enabled": true, "ota_url": "https://github.com/user/repo/releases/download/v1.0.1/firmware.bin"}'
```

### Get Device Twin
```bash
az iot hub device-twin show \
  --hub-name YOUR_IOT_HUB \
  --device-id YOUR_DEVICE_ID
```

---

## Notes

1. **OTA Updates**: The device stops the web server before OTA to free memory (~40KB required)
2. **Rollback**: If new firmware fails to boot 3 times, device automatically rolls back
3. **GitHub OTA**: Supports GitHub release URLs with redirect handling
4. **Memory**: Monitor `free_heap` in reported properties - should stay above 30KB
5. **Sensor Limit**: Maximum 10 sensors can be configured
6. **Telemetry Interval**: Minimum 30 seconds to prevent MQTT flooding

---

## SIM Mode OTA Limitations

**Important**: OTA updates over cellular (SIM mode) may not work with GitHub releases due to mobile carrier restrictions.

### The Issue
Some mobile carriers (including Airtel IoT) block or interfere with HTTPS connections to certain CDN providers (like GitHub's objects.githubusercontent.com). This causes TLS handshake failures with error `-0x0050` (connection reset).

**Symptoms:**
- OTA works perfectly in WiFi mode
- OTA fails in SIM mode during TLS handshake
- Azure IoT Hub MQTT (port 8883) works fine over the same connection
- Error: `mbedtls_ssl_handshake returned -0x0050`

### Recommended Solution: Use Azure Blob Storage

Since Azure connections work reliably over mobile networks (same infrastructure as IoT Hub), host your firmware on Azure Blob Storage instead of GitHub:

1. **Create Azure Storage Account** with a container for firmware
2. **Upload firmware.bin** to the container
3. **Generate SAS URL** or use public access
4. **Use Azure Blob URL for OTA**:
```json
{
  "command": "ota_update",
  "url": "https://yourstorage.blob.core.windows.net/firmware/firmware.bin?sv=2021-06-08&se=...",
  "version": "1.0.1"
}
```

### Azure CLI Example
```bash
# Upload firmware to blob storage
az storage blob upload \
  --account-name yourstorage \
  --container-name firmware \
  --name firmware.bin \
  --file ./build/firmware.bin

# Generate SAS URL (valid for 7 days)
az storage blob generate-sas \
  --account-name yourstorage \
  --container-name firmware \
  --name firmware.bin \
  --permissions r \
  --expiry $(date -u -d "7 days" '+%Y-%m-%dT%H:%MZ') \
  --full-uri
```

### Alternative: WiFi-only OTA
If SIM mode OTA is not critical, continue using GitHub releases - they work perfectly in WiFi mode.

---

*Last Updated: December 2024*
