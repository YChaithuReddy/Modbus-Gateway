/**
 * ============================================================================
 * AZURE OTA EXAMPLE - Arduino ESP32
 * ============================================================================
 *
 * This example shows how to add Azure IoT Hub OTA capability to any
 * Arduino ESP32 sketch.
 *
 * SETUP:
 *   1. Install libraries via Library Manager:
 *      - PubSubClient by Nick O'Leary
 *      - ArduinoJson by Benoit Blanchon
 *
 *   2. Board Settings in Arduino IDE:
 *      - Board: "ESP32 Dev Module" (or your board)
 *      - Partition Scheme: "Minimal SPIFFS (Large APP with OTA)"
 *                     OR: "Default 4MB with OTA"
 *
 *   3. Copy AzureOTA.h to your sketch folder
 *
 *   4. Update credentials below
 *
 * TRIGGERING OTA:
 *   In Azure Portal -> IoT Hub -> Device -> Device Twin, add:
 *   {
 *     "properties": {
 *       "desired": {
 *         "ota_url": "https://github.com/user/repo/releases/download/v1.0/firmware.bin"
 *       }
 *     }
 *   }
 */

#include <WiFi.h>
#include "AzureOTA.h"

// ============================================================================
// CONFIGURATION - Update these!
// ============================================================================

// WiFi credentials
const char* WIFI_SSID = "YOUR_WIFI_SSID";
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";

// Azure IoT Hub credentials
const char* AZURE_HUB = "your-hub.azure-devices.net";
const char* DEVICE_ID = "your-device-id";
const char* DEVICE_KEY = "your-device-primary-key";

// ============================================================================
// SETUP
// ============================================================================

void setup() {
    Serial.begin(115200);
    delay(1000);

    Serial.println();
    Serial.println("========================================");
    Serial.println("   Arduino ESP32 with Azure OTA");
    Serial.println("========================================");

    // Connect to WiFi
    Serial.print("Connecting to WiFi");
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }

    Serial.println();
    Serial.println("WiFi connected!");
    Serial.println("IP: " + WiFi.localIP().toString());

    // Initialize Azure OTA
    // This connects to Azure IoT Hub and listens for OTA commands
    AzureOTA.begin(AZURE_HUB, DEVICE_ID, DEVICE_KEY);

    Serial.println();
    Serial.println("Ready! Waiting for OTA commands from Azure...");
    Serial.println();
}

// ============================================================================
// LOOP
// ============================================================================

void loop() {
    // IMPORTANT: Call this to maintain Azure connection and check for OTA
    AzureOTA.loop();

    // Your application code here...
    // Example: Read sensors, control outputs, etc.

    static unsigned long lastPrint = 0;
    if (millis() - lastPrint > 10000) {  // Every 10 seconds
        lastPrint = millis();

        Serial.println("----------------------------------------");
        Serial.println("Status:");
        Serial.println("  Azure Connected: " + String(AzureOTA.isConnected() ? "Yes" : "No"));
        Serial.println("  Free Heap: " + String(ESP.getFreeHeap()) + " bytes");
        Serial.println("  Uptime: " + String(millis() / 1000) + " seconds");
        Serial.println("----------------------------------------");
    }

    delay(100);
}
