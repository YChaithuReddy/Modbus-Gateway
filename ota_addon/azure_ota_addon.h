/**
 * ============================================================================
 * AZURE IOT HUB OTA ADDON - Header File
 * ============================================================================
 *
 * Add this to ANY ESP32 project to enable OTA updates via Azure IoT Hub.
 *
 * Usage:
 *   #include "azure_ota_addon.h"
 *
 *   void app_main(void) {
 *       // Initialize WiFi first...
 *
 *       // Option 1: Use hardcoded credentials (modify in .c file)
 *       azure_ota_init();
 *
 *       // Option 2: Set credentials at runtime
 *       azure_ota_set_credentials("your-hub.azure-devices.net", "device-id", "device-key");
 *       azure_ota_init();
 *   }
 */

#ifndef AZURE_OTA_ADDON_H
#define AZURE_OTA_ADDON_H

#include "esp_err.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Set Azure IoT Hub credentials at runtime
 * Call this BEFORE azure_ota_init() if not using hardcoded values
 *
 * @param hub_fqdn    Azure IoT Hub hostname (e.g., "your-hub.azure-devices.net")
 * @param device_id   Device ID registered in Azure IoT Hub
 * @param device_key  Device primary key from Azure IoT Hub
 */
void azure_ota_set_credentials(const char* hub_fqdn, const char* device_id, const char* device_key);

/**
 * Initialize Azure OTA addon
 * Call this from your app_main() AFTER WiFi/network is connected
 *
 * This will:
 *   - Sync time via SNTP (required for SAS token)
 *   - Generate SAS authentication token
 *   - Connect to Azure IoT Hub via MQTT
 *   - Subscribe to Device Twin updates
 *   - Automatically start OTA when ota_url is received
 *
 * @return ESP_OK on success, ESP_FAIL on error
 */
esp_err_t azure_ota_init(void);

/**
 * Check if MQTT is connected to Azure IoT Hub
 *
 * @return true if connected, false otherwise
 */
bool azure_ota_is_connected(void);

/**
 * Manually trigger OTA update from URL
 * Normally OTA is triggered automatically via Device Twin, but this
 * allows manual triggering if needed.
 *
 * @param url  URL to firmware .bin file
 */
void azure_ota_trigger(const char* url);

#ifdef __cplusplus
}
#endif

#endif // AZURE_OTA_ADDON_H
