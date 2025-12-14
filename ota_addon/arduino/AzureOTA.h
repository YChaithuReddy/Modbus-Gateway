/**
 * ============================================================================
 * AZURE IOT HUB OTA - Arduino ESP32 Library
 * ============================================================================
 *
 * Single-header library for Arduino IDE to enable OTA updates via Azure IoT Hub.
 *
 * HOW TO USE:
 *   1. Copy this file (AzureOTA.h) to your Arduino sketch folder
 *   2. Include it in your sketch: #include "AzureOTA.h"
 *   3. Call AzureOTA.begin() in setup()
 *   4. Call AzureOTA.loop() in loop()
 *
 * REQUIREMENTS:
 *   - Arduino ESP32 Board (2.x or 3.x)
 *   - Board: ESP32 Dev Module (or similar)
 *   - Partition Scheme: "Minimal SPIFFS (Large APP with OTA)"
 *   - Libraries: PubSubClient, ArduinoJson (install via Library Manager)
 *
 * OTA TRIGGER:
 *   Update Device Twin in Azure IoT Hub:
 *   {
 *     "properties": {
 *       "desired": {
 *         "ota_url": "https://github.com/.../firmware.bin"
 *       }
 *     }
 *   }
 *
 * Author: FluxGen Engineering
 * License: MIT
 * ============================================================================
 */

#ifndef AZURE_OTA_H
#define AZURE_OTA_H

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <Update.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <base64.h>
#include <mbedtls/md.h>
#include <time.h>

class AzureOTAClass {
private:
    // Azure credentials
    String _hubHost;
    String _deviceId;
    String _deviceKey;
    String _sasToken;

    // MQTT
    WiFiClientSecure _wifiClient;
    PubSubClient _mqttClient;
    bool _connected = false;

    // OTA
    String _otaUrl;
    bool _otaInProgress = false;

    // SAS Token generation
    String urlEncode(const String& str) {
        String encoded = "";
        char c;
        char code0;
        char code1;
        for (int i = 0; i < str.length(); i++) {
            c = str.charAt(i);
            if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
                encoded += c;
            } else {
                code1 = (c & 0xf) + '0';
                if ((c & 0xf) > 9) code1 = (c & 0xf) - 10 + 'A';
                c = (c >> 4) & 0xf;
                code0 = c + '0';
                if (c > 9) code0 = c - 10 + 'A';
                encoded += '%';
                encoded += code0;
                encoded += code1;
            }
        }
        return encoded;
    }

    String generateSasToken(unsigned long expirySeconds = 86400) {
        String resourceUri = _hubHost + "/devices/" + _deviceId;
        String encodedUri = urlEncode(resourceUri);

        unsigned long expiry = time(nullptr) + expirySeconds;
        String toSign = encodedUri + "\n" + String(expiry);

        // Decode key
        int keyLen = base64_dec_len((char*)_deviceKey.c_str(), _deviceKey.length());
        uint8_t* decodedKey = new uint8_t[keyLen];
        base64_decode((char*)decodedKey, (char*)_deviceKey.c_str(), _deviceKey.length());

        // HMAC-SHA256
        uint8_t hmacResult[32];
        mbedtls_md_hmac(
            mbedtls_md_info_from_type(MBEDTLS_MD_SHA256),
            decodedKey, keyLen,
            (const uint8_t*)toSign.c_str(), toSign.length(),
            hmacResult
        );
        delete[] decodedKey;

        // Base64 encode
        int encodedLen = base64_enc_len(32);
        char* signature = new char[encodedLen + 1];
        base64_encode(signature, (char*)hmacResult, 32);
        signature[encodedLen] = '\0';

        String encodedSignature = urlEncode(String(signature));
        delete[] signature;

        return "SharedAccessSignature sr=" + encodedUri +
               "&sig=" + encodedSignature +
               "&se=" + String(expiry);
    }

    // MQTT Callback
    static AzureOTAClass* _instance;

    static void mqttCallback(char* topic, byte* payload, unsigned int length) {
        if (_instance) {
            _instance->handleMessage(topic, payload, length);
        }
    }

    void handleMessage(char* topic, byte* payload, unsigned int length) {
        String topicStr = String(topic);

        // Check for Device Twin message
        if (topicStr.indexOf("$iothub/twin") >= 0) {
            String message = "";
            for (unsigned int i = 0; i < length; i++) {
                message += (char)payload[i];
            }

            Serial.println("[AZURE] Device Twin update received");

            // Parse JSON
            DynamicJsonDocument doc(2048);
            DeserializationError error = deserializeJson(doc, message);

            if (error) {
                Serial.println("[AZURE] JSON parse error");
                return;
            }

            // Check for OTA URL
            if (doc.containsKey("ota_url")) {
                _otaUrl = doc["ota_url"].as<String>();
                if (_otaUrl.length() > 0) {
                    Serial.println("[AZURE] OTA URL found: " + _otaUrl);
                    startOTA();
                }
            }
        }
    }

    void startOTA() {
        if (_otaInProgress) {
            Serial.println("[OTA] Already in progress");
            return;
        }

        _otaInProgress = true;

        Serial.println("========================================");
        Serial.println("       STARTING OTA UPDATE");
        Serial.println("========================================");
        Serial.println("URL: " + _otaUrl);
        Serial.println("Free heap: " + String(ESP.getFreeHeap()));

        // Handle redirects (GitHub releases)
        String currentUrl = _otaUrl;
        HTTPClient http;

        for (int redirect = 0; redirect < 5; redirect++) {
            Serial.println("[OTA] Connecting to: " + currentUrl);

            http.begin(currentUrl);
            http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);

            int httpCode = http.GET();
            Serial.println("[OTA] HTTP Code: " + String(httpCode));

            if (httpCode == 301 || httpCode == 302 || httpCode == 307 || httpCode == 308) {
                String newUrl = http.getLocation();
                http.end();

                if (newUrl.length() > 0) {
                    Serial.println("[OTA] Redirect to: " + newUrl);
                    currentUrl = newUrl;
                    continue;
                }
            }

            if (httpCode != 200) {
                Serial.println("[OTA] HTTP Error: " + String(httpCode));
                http.end();
                _otaInProgress = false;
                return;
            }

            // Get content length
            int contentLength = http.getSize();
            Serial.println("[OTA] Firmware size: " + String(contentLength) + " bytes");

            if (contentLength <= 0) {
                Serial.println("[OTA] Invalid content length");
                http.end();
                _otaInProgress = false;
                return;
            }

            // Start OTA update
            if (!Update.begin(contentLength)) {
                Serial.println("[OTA] Not enough space");
                http.end();
                _otaInProgress = false;
                return;
            }

            // Download and write
            WiFiClient* stream = http.getStreamPtr();
            uint8_t buff[1024];
            int written = 0;
            int lastProgress = -1;

            while (http.connected() && written < contentLength) {
                size_t available = stream->available();
                if (available) {
                    int readBytes = stream->readBytes(buff, min(available, sizeof(buff)));
                    int writeBytes = Update.write(buff, readBytes);
                    written += writeBytes;

                    int progress = (written * 100) / contentLength;
                    if (progress / 10 > lastProgress / 10) {
                        lastProgress = progress;
                        Serial.println("[OTA] Progress: " + String(progress) + "%");
                    }
                }
                yield();
            }

            http.end();

            if (Update.end()) {
                if (Update.isFinished()) {
                    Serial.println("========================================");
                    Serial.println("    OTA UPDATE SUCCESSFUL!");
                    Serial.println("    Rebooting in 5 seconds...");
                    Serial.println("========================================");
                    delay(5000);
                    ESP.restart();
                } else {
                    Serial.println("[OTA] Update not finished");
                }
            } else {
                Serial.println("[OTA] Update error: " + String(Update.getError()));
            }

            break;
        }

        _otaInProgress = false;
    }

public:
    AzureOTAClass() : _mqttClient(_wifiClient) {
        _instance = this;
    }

    /**
     * Initialize with Azure credentials
     */
    void begin(const char* hubHost, const char* deviceId, const char* deviceKey) {
        _hubHost = String(hubHost);
        _deviceId = String(deviceId);
        _deviceKey = String(deviceKey);

        Serial.println("========================================");
        Serial.println("   AZURE IOT HUB OTA - Arduino");
        Serial.println("========================================");
        Serial.println("Hub: " + _hubHost);
        Serial.println("Device: " + _deviceId);

        // Sync time
        Serial.println("[AZURE] Syncing time...");
        configTime(0, 0, "pool.ntp.org", "time.nist.gov");

        time_t now = time(nullptr);
        int retry = 0;
        while (now < 1700000000 && retry < 30) {
            delay(1000);
            now = time(nullptr);
            retry++;
        }
        Serial.println("[AZURE] Time: " + String(now));

        // Generate SAS token
        _sasToken = generateSasToken();
        Serial.println("[AZURE] SAS Token generated");

        // Configure MQTT
        _wifiClient.setInsecure();  // Skip certificate verification
        _mqttClient.setServer(_hubHost.c_str(), 8883);
        _mqttClient.setCallback(mqttCallback);
        _mqttClient.setBufferSize(4096);

        Serial.println("[AZURE] Initialization complete");
    }

    /**
     * Call this in loop() to maintain connection
     */
    void loop() {
        if (!_mqttClient.connected()) {
            reconnect();
        }
        _mqttClient.loop();
    }

    /**
     * Reconnect to MQTT
     */
    void reconnect() {
        if (_mqttClient.connected()) return;

        Serial.println("[AZURE] Connecting to IoT Hub...");

        String username = _hubHost + "/" + _deviceId + "/?api-version=2021-04-12";

        if (_mqttClient.connect(_deviceId.c_str(), username.c_str(), _sasToken.c_str())) {
            Serial.println("[AZURE] Connected!");
            _connected = true;

            // Subscribe to Device Twin
            String twinTopic = "$iothub/twin/PATCH/properties/desired/#";
            _mqttClient.subscribe(twinTopic.c_str());
            Serial.println("[AZURE] Subscribed to Device Twin");

            // Request full twin
            String getTwin = "$iothub/twin/GET/?$rid=0";
            _mqttClient.publish(getTwin.c_str(), "");
        } else {
            Serial.println("[AZURE] Connection failed: " + String(_mqttClient.state()));
            _connected = false;
            delay(5000);
        }
    }

    /**
     * Check if connected
     */
    bool isConnected() {
        return _mqttClient.connected();
    }

    /**
     * Manually trigger OTA
     */
    void triggerOTA(const char* url) {
        _otaUrl = String(url);
        startOTA();
    }
};

// Static instance pointer
AzureOTAClass* AzureOTAClass::_instance = nullptr;

// Global instance
AzureOTAClass AzureOTA;

#endif // AZURE_OTA_H
