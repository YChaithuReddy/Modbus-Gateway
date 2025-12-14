# Azure IoT Hub Remote Control Guide
## Control Your ESP32 Gateway from Anywhere in the World

---

## 🌍 Overview

Your ESP32 Modbus Gateway now supports **Cloud-to-Device (C2D) commands** from Azure IoT Hub. This means you can:

✅ **Control your gateway from anywhere** - Send commands from Azure portal, mobile app, or your own web application
✅ **No physical access needed** - Remotely restart, configure, or troubleshoot
✅ **Real-time control** - Commands are delivered instantly over MQTT
✅ **Secure** - All communication encrypted with TLS 1.2

---

## 🎯 What You Can Control Remotely

### Available Commands:

1. **Restart Gateway** - Reboot the ESP32
2. **Change Telemetry Interval** - Update how often data is sent
3. **Get Status** - Force immediate status update
4. **Toggle Web Server** - Start/stop the configuration web interface

---

## 🚀 How to Send Commands

### Method 1: Azure Portal (Web Browser)

**Step 1: Login to Azure Portal**
```
1. Go to https://portal.azure.com
2. Navigate to your IoT Hub
3. Click "Devices" in left menu
4. Click on your device (e.g., "gateway-01")
```

**Step 2: Send Command**
```
1. Click "Message to Device" button at top
2. In "Message Body", enter JSON command
3. Click "Send Message"
```

**Example Commands:**

```json
{
  "command": "restart"
}
```

```json
{
  "command": "set_telemetry_interval",
  "interval": 120
}
```

```json
{
  "command": "get_status"
}
```

```json
{
  "command": "toggle_webserver"
}
```

---

### Method 2: Azure CLI (Command Line)

**Install Azure CLI:**
```bash
# Windows
Download from: https://aka.ms/installazurecliwindows

# Linux
curl -sL https://aka.ms/InstallAzureCLIDeb | sudo bash

# Mac
brew install azure-cli
```

**Login:**
```bash
az login
```

**Send Commands:**

**1. Restart Gateway:**
```bash
az iot device c2d-message send \
  --hub-name fluxgen-modbus-hub \
  --device-id gateway-01 \
  --data '{"command":"restart"}'
```

**2. Change Telemetry Interval to 2 minutes:**
```bash
az iot device c2d-message send \
  --hub-name fluxgen-modbus-hub \
  --device-id gateway-01 \
  --data '{"command":"set_telemetry_interval","interval":120}'
```

**3. Get Immediate Status:**
```bash
az iot device c2d-message send \
  --hub-name fluxgen-modbus-hub \
  --device-id gateway-01 \
  --data '{"command":"get_status"}'
```

**4. Toggle Web Server:**
```bash
az iot device c2d-message send \
  --hub-name fluxgen-modbus-hub \
  --device-id gateway-01 \
  --data '{"command":"toggle_webserver"}'
```

---

### Method 3: Python Script (Automation)

**Install SDK:**
```bash
pip install azure-iot-hub
```

**Create Script** (`send_command.py`):

```python
from azure.iot.hub import IoTHubRegistryManager
import sys

# Your IoT Hub connection string
CONNECTION_STRING = "HostName=fluxgen-modbus-hub.azure-devices.net;SharedAccessKeyName=iothubowner;SharedAccessKey=YOUR_KEY_HERE"
DEVICE_ID = "gateway-01"

def send_command(command_json):
    try:
        registry_manager = IoTHubRegistryManager(CONNECTION_STRING)
        registry_manager.send_c2d_message(DEVICE_ID, command_json)
        print(f"✅ Command sent successfully: {command_json}")
    except Exception as e:
        print(f"❌ Error: {e}")

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage: python send_command.py <command>")
        print("Examples:")
        print('  python send_command.py \'{"command":"restart"}\'')
        print('  python send_command.py \'{"command":"set_telemetry_interval","interval":90}\'')
        sys.exit(1)

    send_command(sys.argv[1])
```

**Usage:**
```bash
# Restart gateway
python send_command.py '{"command":"restart"}'

# Change interval
python send_command.py '{"command":"set_telemetry_interval","interval":90}'

# Get status
python send_command.py '{"command":"get_status"}'
```

---

### Method 4: Node.js Script

**Install SDK:**
```bash
npm install azure-iothub
```

**Create Script** (`sendCommand.js`):

```javascript
const Client = require('azure-iothub').Client;

const connectionString = 'HostName=fluxgen-modbus-hub.azure-devices.net;SharedAccessKeyName=iothubowner;SharedAccessKey=YOUR_KEY_HERE';
const deviceId = 'gateway-01';

function sendCommand(commandJson) {
    const client = Client.fromConnectionString(connectionString);

    client.open((err) => {
        if (err) {
            console.error('❌ Could not connect: ' + err.message);
        } else {
            console.log('✅ Connected to IoT Hub');

            const message = new Message(JSON.stringify(commandJson));

            client.send(deviceId, message, (err) => {
                if (err) {
                    console.error('❌ Send error: ' + err.toString());
                } else {
                    console.log('✅ Command sent:', commandJson);
                }
                client.close();
            });
        }
    });
}

// Example usage
sendCommand({ command: "restart" });
// sendCommand({ command: "set_telemetry_interval", interval: 120 });
// sendCommand({ command: "get_status" });
```

**Run:**
```bash
node sendCommand.js
```

---

## 📋 Command Reference

### 1. Restart Gateway

**Command:**
```json
{
  "command": "restart"
}
```

**What it does:**
- Reboots the ESP32 gateway
- Waits 3 seconds before restarting
- All configuration is preserved (saved in flash)
- Gateway reconnects automatically after boot

**Use cases:**
- Apply configuration changes
- Recover from errors
- Clear temporary glitches

**ESP32 Log Output:**
```
I (12345) AZURE_IOT: [C2D] Command: restart
W (12346) AZURE_IOT: [C2D] Restart command received - restarting in 3 seconds...
```

---

### 2. Set Telemetry Interval

**Command:**
```json
{
  "command": "set_telemetry_interval",
  "interval": 120
}
```

**Parameters:**
- `interval`: Seconds between telemetry sends (30-3600)

**What it does:**
- Changes how often sensor data is sent to Azure
- Saves configuration to flash (persists across reboots)
- Takes effect immediately

**Valid ranges:**
- Minimum: 30 seconds
- Maximum: 3600 seconds (1 hour)
- Recommended: 60-300 seconds

**Use cases:**
- **Reduce data costs**: Increase interval during low-activity periods
- **Faster monitoring**: Decrease interval during critical operations
- **Compliance**: Match regulatory reporting requirements

**ESP32 Log Output:**
```
I (12345) AZURE_IOT: [C2D] Command: set_telemetry_interval
I (12346) AZURE_IOT: [C2D] Telemetry interval updated to 120 seconds
```

**Examples:**

```bash
# Every minute (high frequency)
'{"command":"set_telemetry_interval","interval":60}'

# Every 5 minutes (balanced)
'{"command":"set_telemetry_interval","interval":300}'

# Every 15 minutes (low frequency, save data)
'{"command":"set_telemetry_interval","interval":900}'
```

---

### 3. Get Status

**Command:**
```json
{
  "command": "get_status"
}
```

**What it does:**
- Forces immediate telemetry send
- Does not change scheduled interval
- Useful for on-demand status checks

**Use cases:**
- Verify gateway is responding
- Get latest sensor readings now
- Troubleshoot connectivity
- Test after configuration changes

**ESP32 Log Output:**
```
I (12345) AZURE_IOT: [C2D] Command: get_status
I (12346) AZURE_IOT: [C2D] Status request - sending telemetry now
I (12347) AZURE_IOT: [SEND] Published to Azure IoT Hub
```

**Result:**
- Gateway sends full sensor telemetry immediately
- You'll see data in Azure within 1-2 seconds

---

### 4. Toggle Web Server

**Command:**
```json
{
  "command": "toggle_webserver"
}
```

**What it does:**
- Starts or stops the web configuration interface
- If OFF → turns ON (starts AP mode or HTTP server)
- If ON → turns OFF (stops HTTP server)

**Use cases:**
- **Enable remote config**: Start web server from cloud
- **Security**: Disable web server when not needed
- **Troubleshooting**: Restart web server if not responding
- **Maintenance**: Enable for configuration, disable after

**ESP32 Log Output:**
```
I (12345) AZURE_IOT: [C2D] Command: toggle_webserver
I (12346) AZURE_IOT: [C2D] Toggling web server
I (12347) WEB_CONFIG: Starting web configuration server...
```

---

## 🔧 Advanced Use Cases

### Scenario 1: Scheduled Interval Changes

**Problem**: You want high-frequency monitoring during business hours, low-frequency at night.

**Solution**: Use Azure Logic Apps or Functions with timer trigger

```python
import datetime
import azure_iot_hub_script

current_hour = datetime.datetime.now().hour

if 8 <= current_hour <= 18:  # Business hours
    send_command('{"command":"set_telemetry_interval","interval":60}')
else:  # Night time
    send_command('{"command":"set_telemetry_interval","interval":300}')
```

**Schedule**: Run every hour

---

### Scenario 2: Alert-Based Restart

**Problem**: Gateway occasionally hangs, need automatic recovery

**Solution**: Azure Stream Analytics + Logic App

```sql
-- Stream Analytics Query
SELECT
    System.Timestamp AS EventTime,
    DeviceId
INTO
    AlertOutput
FROM
    IoTHubInput
GROUP BY
    DeviceId,
    TumblingWindow(minute, 10)
HAVING
    COUNT(*) < 1  -- No messages in 10 minutes
```

**Logic App**: When alert triggers → Send restart command

---

### Scenario 3: Remote Troubleshooting

**Problem**: Customer reports issue, you need to check gateway

**Steps:**
```bash
# 1. Get current status
az iot device c2d-message send \
  --hub-name fluxgen-modbus-hub \
  --device-id gateway-01 \
  --data '{"command":"get_status"}'

# 2. Enable web server for configuration check
az iot device c2d-message send \
  --hub-name fluxgen-modbus-hub \
  --device-id gateway-01 \
  --data '{"command":"toggle_webserver"}'

# 3. Connect to web interface (if customer can share IP)

# 4. If needed, restart to apply fixes
az iot device c2d-message send \
  --hub-name fluxgen-modbus-hub \
  --device-id gateway-01 \
  --data '{"command":"restart"}'

# 5. Disable web server for security
az iot device c2d-message send \
  --hub-name fluxgen-modbus-hub \
  --device-id gateway-01 \
  --data '{"command":"toggle_webserver"}'
```

---

## 📊 Monitoring Command Execution

### Check ESP32 Logs:

**Connect serial monitor:**
```bash
idf.py monitor
```

**Look for:**
```
I (12345) AZURE_IOT: [MSG] CLOUD-TO-DEVICE MESSAGE RECEIVED:
I (12346) AZURE_IOT: [C2D] Processing command: {"command":"restart"}
I (12347) AZURE_IOT: [C2D] Command: restart
W (12348) AZURE_IOT: [C2D] Restart command received - restarting in 3 seconds...
```

---

### Monitor in Azure Portal:

**Azure Portal → IoT Hub → Metrics**

Add metric:
- **Metric**: C2D messages
- **Aggregation**: Count
- **Time range**: Last hour

Shows how many commands were sent.

---

## 🔒 Security Considerations

### Best Practices:

1. **Limit Access**
   - Only give IoT Hub connection strings to trusted users
   - Use Azure RBAC (Role-Based Access Control)
   - Create separate service principals for automation

2. **Command Validation**
   - ESP32 validates interval ranges (30-3600)
   - Unknown commands are logged but ignored
   - Consider adding command authentication tokens

3. **Audit Logging**
   - All C2D commands are logged in ESP32
   - Enable Azure Monitor diagnostics for audit trail
   - Review logs regularly

4. **Rate Limiting**
   - Avoid sending too many commands rapidly
   - Azure has rate limits (varies by tier)
   - Implement cooldown periods in automation scripts

---

## 🛠️ Troubleshooting

### Issue 1: Command Not Received

**Symptoms:**
- No log output on ESP32
- Command sent successfully from Azure

**Check:**
1. ✅ Gateway is connected to Azure (check MQTT_EVENT_CONNECTED log)
2. ✅ C2D subscription successful (check "Subscribed to C2D messages" log)
3. ✅ Device ID matches exactly
4. ✅ Network connectivity stable

**Solution:**
```bash
# Check device connection status
az iot hub device-identity show \
  --hub-name fluxgen-modbus-hub \
  --device-id gateway-01 \
  --query connectionState
```

---

### Issue 2: JSON Parse Error

**Symptoms:**
```
W (12345) AZURE_IOT: [C2D] Failed to parse JSON command
```

**Cause:** Malformed JSON

**Fix:**
- Ensure JSON is valid (use jsonlint.com to validate)
- Use double quotes, not single quotes
- No trailing commas

**Wrong:**
```json
{ 'command': 'restart', }
```

**Correct:**
```json
{ "command": "restart" }
```

---

### Issue 3: Unknown Command

**Symptoms:**
```
W (12345) AZURE_IOT: [C2D] Unknown command: reboot
```

**Fix:**
- Check spelling (commands are case-sensitive)
- Supported commands: `restart`, `set_telemetry_interval`, `get_status`, `toggle_webserver`

---

## 📱 Build Your Own Mobile App

Want to control from your phone? Use Azure IoT SDKs:

**Android (Java/Kotlin):**
```gradle
implementation 'com.microsoft.azure.sdk.iot:iot-service-client:1.30.0'
```

**iOS (Swift):**
```swift
// Use Azure IoT Hub REST API
// https://docs.microsoft.com/rest/api/iothub/
```

**React Native / Flutter:**
- Use REST API or WebSocket
- Call Azure Functions as backend

---

## 🎓 Quick Reference Card

| Command | JSON | Effect |
|---------|------|--------|
| Restart | `{"command":"restart"}` | Reboots ESP32 |
| Set Interval | `{"command":"set_telemetry_interval","interval":120}` | Change send frequency |
| Get Status | `{"command":"get_status"}` | Force immediate telemetry |
| Toggle Web | `{"command":"toggle_webserver"}` | Start/stop web interface |

**Common Intervals:**
- 30s = Real-time monitoring
- 60s = Standard (default)
- 300s = Balanced (5 min)
- 900s = Low-cost (15 min)

---

## 📞 Need More Commands?

Want to add custom commands? Edit `main/main.c` around line 510-545:

```c
else if (strcmp(cmd, "your_command") == 0) {
    // Your custom code here
    ESP_LOGI(TAG, "[C2D] Custom command executed");
}
```

**Ideas for custom commands:**
- Change WiFi network remotely
- Update sensor configurations
- Enable/disable specific sensors
- Change log levels
- Trigger manual Modbus scan
- Reset statistics/counters

---

## 🎯 Next Steps

1. ✅ Gateway is already configured for C2D
2. ✅ Try sending "get_status" command from Azure portal
3. ✅ Monitor ESP32 logs to see command execution
4. ✅ Create automation scripts for common tasks
5. ✅ Build your own monitoring dashboard

---

**Last Updated**: January 2025
**Gateway Version**: v1.1.0
**Supported Commands**: 4 (restart, set_telemetry_interval, get_status, toggle_webserver)
