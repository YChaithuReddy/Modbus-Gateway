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
 *   - Arduino ESP32 Board (1.x, 2.x or 3.x)
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
#include <mbedtls/md.h>
#include <mbedtls/base64.h>
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

    // ========================================================================
    // Built-in Base64 functions (compatible with all ESP32 Arduino versions)
    // ========================================================================

    int base64_encode_len(int inputLen) {
        return ((inputLen + 2) / 3) * 4 + 1;
    }

    int base64_decode_len(const char* input, int inputLen) {
        int padding = 0;
        if (inputLen >= 2) {
            if (input[inputLen - 1] == '=') padding++;
            if (input[inputLen - 2] == '=') padding++;
        }
        return (inputLen * 3) / 4 - padding;
    }

    int base64_encode(char* output, const uint8_t* input, int inputLen) {
        size_t outputLen = 0;
        int ret = mbedtls_base64_encode(
            (unsigned char*)output,
            base64_encode_len(inputLen),
            &outputLen,
            input,
            inputLen
        );
        if (ret == 0) {
            output[outputLen] = '\0';
            return outputLen;
        }
        return 0;
    }

    int base64_decode(uint8_t* output, const char* input, int inputLen) {
        size_t outputLen = 0;
        int ret = mbedtls_base64_decode(
            output,
            base64_decode_len(input, inputLen) + 1,
            &outputLen,
            (const unsigned char*)input,
            inputLen
        );
        if (ret == 0) {
            return outputLen;
        }
        return 0;
    }

    // ========================================================================
    // URL Encoding
    // ========================================================================

    String urlEncode(const String& str) {
        String encoded = "";
        char c;
        char code0;
        char code1;
        for (unsigned int i = 0; i < str.length(); i++) {
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

    // ========================================================================
    // SAS Token Generation
    // ========================================================================

    String generateSasToken(unsigned long expirySeconds = 86400) {
        String resourceUri = _hubHost + "/devices/" + _deviceId;
        String encodedUri = urlEncode(resourceUri);

        unsigned long expiry = time(nullptr) + expirySeconds;
        String toSign = encodedUri + "\n" + String(expiry);

        // Decode the device key from base64
        int keyLen = base64_decode_len(_deviceKey.c_str(), _deviceKey.length());
        uint8_t* decodedKey = new uint8_t[keyLen + 1];
        int actualKeyLen = base64_decode(decodedKey, _deviceKey.c_str(), _deviceKey.length());

        // HMAC-SHA256
        uint8_t hmacResult[32];
        mbedtls_md_hmac(
            mbedtls_md_info_from_type(MBEDTLS_MD_SHA256),
            decodedKey, actualKeyLen,
            (const uint8_t*)toSign.c_str(), toSign.length(),
            hmacResult
        );
        delete[] decodedKey;

        // Base64 encode the signature
        int encodedLen = base64_encode_len(32);
        char* signature = new char[encodedLen + 1];
        base64_encode(signature, hmacResult, 32);

        String encodedSignature = urlEncode(String(signature));
        delete[] signature;

        return "SharedAccessSignature sr=" + encodedUri +
               "&sig=" + encodedSignature +
               "&se=" + String(expiry);
    }

    // ========================================================================
    // MQTT Callback
    // ========================================================================

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

            // Parse JSON - use StaticJsonDocument for better compatibility
            StaticJsonDocument<2048> doc;
            DeserializationError error = deserializeJson(doc, message);

            if (error) {
                Serial.print("[AZURE] JSON parse error: ");
                Serial.println(error.c_str());
                return;
            }

            // Check for OTA URL in root
            if (doc.containsKey("ota_url")) {
                _otaUrl = doc["ota_url"].as<String>();
                if (_otaUrl.length() > 0) {
                    Serial.println("[AZURE] OTA URL found: " + _otaUrl);
                    startOTA();
                }
            }
            // Also check in desired properties (full twin response)
            else if (doc.containsKey("desired") && doc["desired"].containsKey("ota_url")) {
                _otaUrl = doc["desired"]["ota_url"].as<String>();
                if (_otaUrl.length() > 0) {
                    Serial.println("[AZURE] OTA URL found in desired: " + _otaUrl);
                    startOTA();
                }
            }
        }
    }

    // ========================================================================
    // OTA Update
    // ========================================================================

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
        WiFiClientSecure httpsClient;
        httpsClient.setInsecure();  // Skip certificate verification

        for (int redirect = 0; redirect < 5; redirect++) {
            Serial.println("[OTA] Connecting to: " + currentUrl);

            // Use secure client for HTTPS
            if (currentUrl.startsWith("https://")) {
                http.begin(httpsClient, currentUrl);
            } else {
                http.begin(currentUrl);
            }

            http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
            http.setTimeout(30000);  // 30 second timeout

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
                Update.printError(Serial);
                http.end();
                _otaInProgress = false;
                return;
            }

            // Download and write
            WiFiClient* stream = http.getStreamPtr();
            uint8_t buff[1024];
            int written = 0;
            int lastProgress = -1;
            unsigned long lastActivity = millis();

            while (http.connected() && written < contentLength) {
                size_t available = stream->available();
                if (available) {
                    lastActivity = millis();
                    int toRead = min(available, sizeof(buff));
                    int readBytes = stream->readBytes(buff, toRead);
                    int writeBytes = Update.write(buff, readBytes);
                    written += writeBytes;

                    int progress = (written * 100) / contentLength;
                    if (progress / 10 > lastProgress / 10) {
                        lastProgress = progress;
                        Serial.println("[OTA] Progress: " + String(progress) + "%");
                    }
                } else {
                    // Check for timeout
                    if (millis() - lastActivity > 30000) {
                        Serial.println("[OTA] Download timeout");
                        Update.abort();
                        http.end();
                        _otaInProgress = false;
                        return;
                    }
                    delay(10);
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
                Serial.print("[OTA] Update error: ");
                Update.printError(Serial);
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

        // Sync time via NTP
        Serial.println("[AZURE] Syncing time...");
        configTime(0, 0, "pool.ntp.org", "time.nist.gov");

        time_t now = time(nullptr);
        int retry = 0;
        while (now < 1700000000 && retry < 30) {
            delay(1000);
            now = time(nullptr);
            retry++;
            Serial.print(".");
        }
        Serial.println();
        Serial.println("[AZURE] Time synced: " + String(now));

        // Generate SAS token
        _sasToken = generateSasToken();
        Serial.println("[AZURE] SAS Token generated");

        // Configure MQTT
        _wifiClient.setInsecure();  // Skip certificate verification
        _mqttClient.setServer(_hubHost.c_str(), 8883);
        _mqttClient.setCallback(mqttCallback);
        _mqttClient.setBufferSize(4096);
        _mqttClient.setKeepAlive(60);

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

            // Subscribe to Device Twin desired property updates
            String twinPatchTopic = "$iothub/twin/PATCH/properties/desired/#";
            _mqttClient.subscribe(twinPatchTopic.c_str());
            Serial.println("[AZURE] Subscribed to twin patches");

            // Subscribe to Device Twin response
            String twinResponseTopic = "$iothub/twin/res/#";
            _mqttClient.subscribe(twinResponseTopic.c_str());
            Serial.println("[AZURE] Subscribed to twin responses");

            // Request full Device Twin
            String getTwin = "$iothub/twin/GET/?$rid=getTwin";
            _mqttClient.publish(getTwin.c_str(), "");
            Serial.println("[AZURE] Requested Device Twin");
        } else {
            int state = _mqttClient.state();
            Serial.print("[AZURE] Connection failed, state: ");
            Serial.println(state);

            // Print human-readable error
            switch (state) {
                case -4: Serial.println("  -> Connection timeout"); break;
                case -3: Serial.println("  -> Connection lost"); break;
                case -2: Serial.println("  -> Connect failed"); break;
                case -1: Serial.println("  -> Disconnected"); break;
                case 1:  Serial.println("  -> Bad protocol"); break;
                case 2:  Serial.println("  -> Bad client ID"); break;
                case 3:  Serial.println("  -> Unavailable"); break;
                case 4:  Serial.println("  -> Bad credentials"); break;
                case 5:  Serial.println("  -> Unauthorized"); break;
            }

            _connected = false;
            delay(5000);
        }
    }

    /**
     * Check if connected to Azure
     */
    bool isConnected() {
        return _mqttClient.connected();
    }

    /**
     * Manually trigger OTA from URL
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
