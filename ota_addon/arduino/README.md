# Azure OTA for Arduino ESP32

**Single-header library to add OTA updates via Azure IoT Hub to any Arduino ESP32 sketch.**

---

## Quick Start (5 Minutes)

### Step 1: Install Required Libraries

In Arduino IDE, go to **Sketch → Include Library → Manage Libraries** and install:
- `PubSubClient` by Nick O'Leary
- `ArduinoJson` by Benoit Blanchon

### Step 2: Select Partition Scheme

In Arduino IDE, go to **Tools → Partition Scheme** and select:
- `Minimal SPIFFS (Large APP with OTA)` ← **Recommended**
- OR `Default 4MB with OTA`

### Step 3: Copy AzureOTA.h

Copy `AzureOTA.h` to your Arduino sketch folder (same folder as your .ino file).

### Step 4: Add to Your Sketch

```cpp
#include <WiFi.h>
#include "AzureOTA.h"

// WiFi credentials
const char* WIFI_SSID = "YOUR_WIFI";
const char* WIFI_PASSWORD = "YOUR_PASSWORD";

// Azure credentials
const char* AZURE_HUB = "your-hub.azure-devices.net";
const char* DEVICE_ID = "your-device-id";
const char* DEVICE_KEY = "your-device-key";

void setup() {
    Serial.begin(115200);

    // Connect WiFi
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
    }

    // Initialize Azure OTA - THAT'S IT!
    AzureOTA.begin(AZURE_HUB, DEVICE_ID, DEVICE_KEY);
}

void loop() {
    // IMPORTANT: Call this every loop!
    AzureOTA.loop();

    // Your code here...
}
```

### Step 5: Upload & Test

1. Upload sketch to ESP32
2. Open Serial Monitor (115200 baud)
3. You should see "Azure Connected!"

---

## Triggering OTA Update

### From Azure Portal:

1. Go to **Azure Portal** → **IoT Hub** → **Devices**
2. Click your device → **Device Twin**
3. Add to desired properties:

```json
{
  "properties": {
    "desired": {
      "ota_url": "https://github.com/user/repo/releases/download/v1.0/firmware.bin"
    }
  }
}
```

4. Click **Save**
5. Device will download and install the new firmware automatically!

### From Azure CLI:

```bash
az iot hub device-twin update \
  --hub-name your-hub \
  --device-id your-device \
  --desired '{"ota_url": "https://example.com/firmware.bin"}'
```

---

## Important: Keep OTA in Your Firmware!

**WARNING:** If you upload firmware without AzureOTA, you lose remote update capability!

Always include in your sketches:
```cpp
#include "AzureOTA.h"

void setup() {
    // ... WiFi connection ...
    AzureOTA.begin(AZURE_HUB, DEVICE_ID, DEVICE_KEY);  // Keep this!
}

void loop() {
    AzureOTA.loop();  // Keep this!
    // Your code...
}
```

---

## Files

| File | Description |
|------|-------------|
| `AzureOTA.h` | Library file - copy to your sketch folder |
| `AzureOTA_Example.ino` | Complete working example |

---

## Troubleshooting

### "WiFi connected but Azure not connecting"
- Check Azure credentials are correct
- Ensure device is registered in IoT Hub
- Use PRIMARY key from device (not connection string)

### "OTA starts but fails"
- Check URL is accessible (try in browser)
- Ensure firmware .bin is for ESP32
- Check partition scheme supports OTA

### "Not enough space"
- Change partition scheme to include OTA
- Tools → Partition Scheme → "Minimal SPIFFS (Large APP with OTA)"

### "Time sync failed"
- Device needs internet access for NTP
- Wait a few seconds after WiFi connects

---

## Minimum Sketch Size

The AzureOTA library adds approximately:
- **Flash:** ~50KB
- **RAM:** ~20KB

Your sketch must leave room for OTA partition (typically 50% of app space).

---

## License

MIT License - Free to use in any project.
