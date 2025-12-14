# Azure IoT Hub OTA Addon

**Single-file solution to add OTA update capability to ANY ESP32 project.**

## Quick Start

### Step 1: Copy Files
Copy these files to your project's `main/` folder:
- `azure_ota_addon.c`
- `azure_ota_addon.h`

### Step 2: Update CMakeLists.txt
Add `azure_ota_addon.c` to your `main/CMakeLists.txt`:
```cmake
idf_component_register(
    SRCS "main.c" "azure_ota_addon.c"
    INCLUDE_DIRS "."
)
```

### Step 3: Configure Credentials
Edit `azure_ota_addon.c` and set your Azure credentials:
```c
#define AZURE_IOT_HUB_FQDN      "your-hub.azure-devices.net"
#define AZURE_DEVICE_ID         "your-device-id"
#define AZURE_DEVICE_KEY        "your-device-primary-key"
```

Or set at runtime:
```c
azure_ota_set_credentials("your-hub.azure-devices.net", "device-id", "key");
```

### Step 4: Initialize in Your Code
```c
#include "azure_ota_addon.h"

void app_main(void) {
    // Initialize NVS
    nvs_flash_init();

    // Initialize and connect WiFi
    wifi_init();

    // Initialize Azure OTA (after network is connected!)
    azure_ota_init();

    // Your application code...
    while(1) {
        // Do your stuff
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
```

### Step 5: Ensure OTA Partitions
Your `partitions.csv` must have OTA partitions:
```csv
# Name,     Type, SubType,  Offset,   Size
nvs,        data, nvs,      0x9000,   0x6000,
phy_init,   data, phy,      0xf000,   0x1000,
otadata,    data, ota,      0x10000,  0x2000,
ota_0,      app,  ota_0,    0x20000,  0x1D0000,
ota_1,      app,  ota_1,    0x1F0000, 0x1D0000,
```

---

## Triggering OTA Update

### Via Azure IoT Hub Device Twin

1. Go to Azure Portal → IoT Hub → Devices → Your Device
2. Click "Device Twin"
3. Add/update desired properties:

```json
{
  "properties": {
    "desired": {
      "ota_url": "https://github.com/user/repo/releases/download/v1.0/firmware.bin"
    }
  }
}
```

4. Device will automatically:
   - Download firmware from URL
   - Flash to OTA partition
   - Reboot with new firmware

### Via Azure CLI
```bash
az iot hub device-twin update \
  --hub-name your-hub \
  --device-id your-device \
  --desired '{"ota_url": "https://example.com/firmware.bin"}'
```

---

## Features

- **Single file** - Just add one .c and one .h file
- **Auto SAS token** - Generates Azure authentication automatically
- **Redirect support** - Handles GitHub releases redirects
- **Progress logging** - Shows download progress in console
- **Automatic reboot** - Reboots after successful update
- **Error handling** - Graceful failure without crashing

---

## Requirements

- ESP-IDF v5.x
- Partition table with OTA partitions
- Azure IoT Hub with device registered
- Network connection (WiFi or cellular)

### Required sdkconfig options:
```
CONFIG_PARTITION_TABLE_CUSTOM=y
CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions.csv"
CONFIG_MBEDTLS_CERTIFICATE_BUNDLE=y
CONFIG_ESP_TLS_INSECURE=y
CONFIG_ESP_TLS_SKIP_SERVER_CERT_VERIFY=y
```

---

## Example: Arduino-Style Project with OTA

```c
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "esp_wifi.h"
#include "azure_ota_addon.h"

// Your application code
void my_sensor_task(void *pvParameters) {
    while(1) {
        printf("Reading sensor...\n");
        // Read sensors, do stuff
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}

void app_main(void) {
    // Basic initialization
    nvs_flash_init();
    esp_netif_init();
    esp_event_loop_create_default();

    // Connect to WiFi (your code here)
    // ...

    // Initialize Azure OTA - THIS ENABLES REMOTE UPDATES!
    azure_ota_set_credentials(
        "my-hub.azure-devices.net",
        "my-device-001",
        "abc123devicekey..."
    );
    azure_ota_init();

    // Start your application tasks
    xTaskCreate(my_sensor_task, "sensor", 4096, NULL, 5, NULL);
}
```

---

## Troubleshooting

### "Time sync failed"
- Ensure device has internet access
- Check if NTP port (123) is not blocked

### "MQTT connection failed"
- Verify Azure credentials are correct
- Check device is registered in IoT Hub
- Ensure device key is the PRIMARY key

### "OTA download failed"
- Check URL is accessible from device
- For GitHub releases, ensure URL is public
- Check firewall allows HTTPS (port 443)

### "No OTA partition found"
- Verify partition table has ota_0 and ota_1
- Flash partition table: `idf.py partition-table-flash`

---

## License

MIT License - Use freely in your projects.

---

## Support

For issues, create a GitHub issue or contact FluxGen Engineering.
