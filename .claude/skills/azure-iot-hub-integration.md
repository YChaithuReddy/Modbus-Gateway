# Azure IoT Hub Integration - Complete Guide

## Purpose
Master Azure IoT Hub integration for industrial IoT devices using MQTT, SAS token authentication, and telemetry management.

## When to Use
- Connecting ESP32 devices to Azure IoT Hub
- Implementing device-to-cloud telemetry
- Troubleshooting Azure connectivity issues
- Understanding SAS token generation
- Optimizing cloud communication

---

## 1. Azure IoT Hub Overview

### What is Azure IoT Hub?

Azure IoT Hub is a managed cloud service that acts as a central message hub for bi-directional communication between IoT applications and devices.

```
Key Features:
- Device-to-cloud telemetry (D2C)
- Cloud-to-device commands (C2D)
- Device twins (device state management)
- Direct methods (synchronous operations)
- File upload from devices
- Built-in security with per-device authentication
- MQTT, AMQP, and HTTPS protocol support
```

### Architecture

```
┌──────────────┐
│   Device     │ MQTT/TLS
│   (ESP32)    │───────────┐
└──────────────┘           │
                           ▼
┌──────────────┐     ┌──────────────┐     ┌──────────────┐
│   Device     │────▶│  Azure IoT   │────▶│   Backend    │
│   (ESP32)    │     │     Hub      │     │ Applications │
└──────────────┘     └──────────────┘     └──────────────┘
                           │
┌──────────────┐           │
│   Device     │───────────┘
│   (ESP32)    │ MQTT/TLS
└──────────────┘
```

---

## 2. Device Registration

### Create IoT Hub (Azure Portal)

```bash
1. Login to Azure Portal (portal.azure.com)
2. Create a Resource → Internet of Things → IoT Hub
3. Configure:
   - Subscription: Your subscription
   - Resource Group: Create new or use existing
   - Region: Nearest to your devices
   - IoT Hub Name: my-iot-hub (must be globally unique)
   - Tier: Free (F1) or Standard (S1)
```

### Register Device

```bash
# Using Azure Portal
1. Navigate to your IoT Hub
2. Device Management → Devices → Add Device
3. Enter Device ID: "testing_3"
4. Authentication: Symmetric Key (auto-generate)
5. Save

# Using Azure CLI
az iot hub device-identity create \
  --hub-name my-iot-hub \
  --device-id testing_3 \
  --auth-method shared_private_key
```

### Get Device Credentials

```bash
# Azure Portal
IoT Hub → Devices → Select "testing_3" → Copy Primary Key

# Azure CLI
az iot hub device-identity connection-string show \
  --hub-name my-iot-hub \
  --device-id testing_3

# Output:
HostName=my-iot-hub.azure-devices.net;
DeviceId=testing_3;
SharedAccessKey=xyz...==
```

### Store Credentials in ESP32

```c
// iot_configs.h
#define IOT_CONFIG_IOTHUB_FQDN "my-iot-hub.azure-devices.net"
#define AZURE_DEVICE_ID "testing_3"
#define AZURE_DEVICE_KEY "your-primary-key-here"
```

---

## 3. MQTT Protocol for Azure IoT Hub

### Connection Parameters

```c
// MQTT Broker
Host: <your-hub-name>.azure-devices.net
Port: 8883 (MQTT over TLS)
Protocol: MQTTv3.1.1

// Client ID
Client ID: <device-id>
Example: "testing_3"

// Username
Format: <hub-name>.azure-devices.net/<device-id>/?api-version=2021-04-12
Example: "my-iot-hub.azure-devices.net/testing_3/?api-version=2021-04-12"

// Password
SAS Token (generated dynamically)
```

### MQTT Topics

```c
// Device-to-Cloud (Telemetry)
Topic: devices/<device-id>/messages/events/
Example: devices/testing_3/messages/events/

// With message properties (optional)
Topic: devices/<device-id>/messages/events/<property-bag>
Example: devices/testing_3/messages/events/$.ct=application%2Fjson&$.ce=utf-8

// Cloud-to-Device (Commands)
Subscribe: devices/<device-id>/messages/devicebound/#
Example: devices/testing_3/messages/devicebound/#
```

---

## 4. SAS Token Authentication

### SAS Token Structure

```
SharedAccessSignature sr=<resource-uri>&sig=<signature>&se=<expiry>

Components:
- sr: Resource URI (URL-encoded)
- sig: HMAC-SHA256 signature (URL-encoded, Base64)
- se: Expiry timestamp (Unix epoch in seconds)
```

### SAS Token Generation Algorithm

```c
#include "mbedtls/md.h"
#include "mbedtls/base64.h"

void generate_sas_token(const char *resource_uri,
                        const char *device_key,
                        time_t expiry,
                        char *sas_token,
                        size_t size) {
    // 1. Create string to sign
    char string_to_sign[256];
    snprintf(string_to_sign, sizeof(string_to_sign),
             "%s\n%ld", resource_uri, expiry);

    // 2. Decode Base64 device key
    unsigned char decoded_key[64];
    size_t decoded_key_len;
    mbedtls_base64_decode(decoded_key, sizeof(decoded_key),
                          &decoded_key_len,
                          (const unsigned char*)device_key,
                          strlen(device_key));

    // 3. Calculate HMAC-SHA256 signature
    unsigned char hmac[32];
    mbedtls_md_context_t ctx;
    mbedtls_md_init(&ctx);
    mbedtls_md_setup(&ctx, mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), 1);
    mbedtls_md_hmac_starts(&ctx, decoded_key, decoded_key_len);
    mbedtls_md_hmac_update(&ctx, (const unsigned char*)string_to_sign,
                           strlen(string_to_sign));
    mbedtls_md_hmac_finish(&ctx, hmac);
    mbedtls_md_free(&ctx);

    // 4. Base64 encode signature
    unsigned char signature_b64[64];
    size_t signature_b64_len;
    mbedtls_base64_encode(signature_b64, sizeof(signature_b64),
                          &signature_b64_len, hmac, 32);

    // 5. URL encode signature
    char signature_encoded[128];
    url_encode((const char*)signature_b64, signature_encoded,
               sizeof(signature_encoded));

    // 6. Build SAS token
    snprintf(sas_token, size,
             "SharedAccessSignature sr=%s&sig=%s&se=%ld",
             resource_uri, signature_encoded, expiry);
}

// URL encoding helper
void url_encode(const char *str, char *encoded, size_t size) {
    const char *hex = "0123456789ABCDEF";
    size_t j = 0;

    for (size_t i = 0; str[i] && j < size - 1; i++) {
        char c = str[i];
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' ||
            c == '.' || c == '~') {
            encoded[j++] = c;
        } else {
            if (j + 3 < size) {
                encoded[j++] = '%';
                encoded[j++] = hex[(c >> 4) & 0xF];
                encoded[j++] = hex[c & 0xF];
            }
        }
    }
    encoded[j] = '\0';
}
```

### Example SAS Token Generation

```c
// Configuration
const char *hub_name = "my-iot-hub";
const char *device_id = "testing_3";
const char *device_key = "abc...xyz==";  // Primary key from Azure

// Resource URI (URL-encoded)
char resource_uri[256];
snprintf(resource_uri, sizeof(resource_uri),
         "%s.azure-devices.net%%2Fdevices%%2F%s",
         hub_name, device_id);
// Result: "my-iot-hub.azure-devices.net%2Fdevices%2Ftesting_3"

// Expiry (1 hour from now)
time_t now = time(NULL);
time_t expiry = now + 3600;

// Generate token
char sas_token[512];
generate_sas_token(resource_uri, device_key, expiry, sas_token,
                   sizeof(sas_token));

// Result:
// SharedAccessSignature sr=my-iot-hub.azure-devices.net%2Fdevices%2Ftesting_3
//   &sig=xyz...&se=1704816000
```

---

## 5. ESP-IDF MQTT Implementation

### MQTT Client Configuration

```c
#include "mqtt_client.h"

// Azure IoT Hub CA certificate (root CA)
extern const uint8_t azure_ca_cert_pem_start[] asm("_binary_azure_ca_cert_pem_start");
extern const uint8_t azure_ca_cert_pem_end[] asm("_binary_azure_ca_cert_pem_end");

esp_mqtt_client_handle_t mqtt_client = NULL;

void mqtt_init(void) {
    // Generate SAS token
    char sas_token[512];
    time_t expiry = time(NULL) + 3600;  // 1 hour validity
    generate_sas_token(...);

    // MQTT client configuration
    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = "mqtts://my-iot-hub.azure-devices.net:8883",
        .broker.verification.certificate = (const char*)azure_ca_cert_pem_start,
        .credentials.client_id = "testing_3",
        .credentials.username = "my-iot-hub.azure-devices.net/testing_3/"
                                "?api-version=2021-04-12",
        .credentials.authentication.password = sas_token,
        .session.keepalive = 60,
        .network.timeout_ms = 10000,
        .network.reconnect_timeout_ms = 5000,
    };

    mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(mqtt_client, ESP_EVENT_ANY_ID,
                                    mqtt_event_handler, NULL);
    esp_mqtt_client_start(mqtt_client);
}
```

### MQTT Event Handler

```c
static void mqtt_event_handler(void *handler_args,
                               esp_event_base_t base,
                               int32_t event_id,
                               void *event_data) {
    esp_mqtt_event_handle_t event = event_data;
    esp_mqtt_client_handle_t client = event->client;

    switch ((esp_mqtt_event_id_t)event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "✅ MQTT connected to Azure IoT Hub");

            // Subscribe to cloud-to-device messages
            int msg_id = esp_mqtt_client_subscribe(client,
                "devices/testing_3/messages/devicebound/#", 0);
            ESP_LOGI(TAG, "Subscribed to C2D messages, msg_id=%d", msg_id);
            break;

        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGW(TAG, "❌ MQTT disconnected from Azure IoT Hub");
            break;

        case MQTT_EVENT_SUBSCRIBED:
            ESP_LOGI(TAG, "MQTT subscribed, msg_id=%d", event->msg_id);
            break;

        case MQTT_EVENT_PUBLISHED:
            ESP_LOGI(TAG, "📤 Telemetry published, msg_id=%d", event->msg_id);
            break;

        case MQTT_EVENT_DATA:
            ESP_LOGI(TAG, "📥 Received C2D message:");
            ESP_LOGI(TAG, "  Topic: %.*s", event->topic_len, event->topic);
            ESP_LOGI(TAG, "  Data: %.*s", event->data_len, event->data);

            // Process cloud-to-device command
            process_c2d_message(event->data, event->data_len);
            break;

        case MQTT_EVENT_ERROR:
            ESP_LOGE(TAG, "MQTT error occurred");
            if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
                ESP_LOGE(TAG, "TCP error: 0x%x", event->error_handle->esp_tls_last_esp_err);
            }
            break;

        default:
            break;
    }
}
```

---

## 6. Telemetry Publishing

### JSON Telemetry Format

```json
{
  "deviceId": "testing_3",
  "timestamp": "2025-01-12T14:30:00Z",
  "sensors": [
    {
      "name": "Water Tank Level",
      "unitId": "TANK01",
      "value": 45.5,
      "unit": "%",
      "sensorType": "Level",
      "status": "OK"
    },
    {
      "name": "Flow Meter",
      "unitId": "FLOW01",
      "value": 125.8,
      "unit": "L/min",
      "sensorType": "GENERIC",
      "status": "OK"
    }
  ],
  "systemHealth": {
    "freeHeap": 185000,
    "rssi": -62,
    "uptime": 3600
  }
}
```

### Publish Telemetry

```c
#include "cJSON.h"

esp_err_t publish_telemetry(sensor_reading_t *readings, int count) {
    // Create JSON payload
    cJSON *root = cJSON_CreateObject();

    // Device info
    cJSON_AddStringToObject(root, "deviceId", AZURE_DEVICE_ID);

    // Timestamp (ISO 8601 format)
    time_t now = time(NULL);
    struct tm timeinfo;
    gmtime_r(&now, &timeinfo);
    char timestamp[32];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%SZ", &timeinfo);
    cJSON_AddStringToObject(root, "timestamp", timestamp);

    // Sensors array
    cJSON *sensors = cJSON_CreateArray();
    for (int i = 0; i < count; i++) {
        cJSON *sensor = cJSON_CreateObject();
        cJSON_AddStringToObject(sensor, "name", readings[i].name);
        cJSON_AddStringToObject(sensor, "unitId", readings[i].unit_id);
        cJSON_AddNumberToObject(sensor, "value", readings[i].value);
        cJSON_AddStringToObject(sensor, "unit", readings[i].unit);
        cJSON_AddStringToObject(sensor, "sensorType", readings[i].sensor_type);
        cJSON_AddStringToObject(sensor, "status", readings[i].success ? "OK" : "ERROR");
        cJSON_AddItemToArray(sensors, sensor);
    }
    cJSON_AddItemToObject(root, "sensors", sensors);

    // System health
    cJSON *health = cJSON_CreateObject();
    cJSON_AddNumberToObject(health, "freeHeap", esp_get_free_heap_size());
    cJSON_AddNumberToObject(health, "rssi", get_wifi_rssi());
    cJSON_AddNumberToObject(health, "uptime", esp_timer_get_time() / 1000000);
    cJSON_AddItemToObject(root, "systemHealth", health);

    // Convert to string
    char *json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    // Publish to Azure IoT Hub
    char topic[128];
    snprintf(topic, sizeof(topic), "devices/%s/messages/events/", AZURE_DEVICE_ID);

    int msg_id = esp_mqtt_client_publish(mqtt_client, topic, json_str, 0, 1, 0);

    free(json_str);

    if (msg_id == -1) {
        ESP_LOGE(TAG, "Failed to publish telemetry");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "📤 Telemetry published, msg_id=%d", msg_id);
    return ESP_OK;
}
```

### Telemetry with Message Properties

```c
// Add custom properties to telemetry message
char topic[256];
snprintf(topic, sizeof(topic),
         "devices/%s/messages/events/$.ct=application%%2Fjson&$.ce=utf-8"
         "&alertLevel=high&location=building-a",
         AZURE_DEVICE_ID);

esp_mqtt_client_publish(mqtt_client, topic, json_str, 0, 1, 0);
```

---

## 7. Cloud-to-Device Messages

### Receive Commands from Cloud

```c
void process_c2d_message(const char *data, int length) {
    // Parse JSON command
    cJSON *root = cJSON_ParseWithLength(data, length);
    if (root == NULL) {
        ESP_LOGE(TAG, "Invalid JSON command");
        return;
    }

    // Extract command type
    cJSON *cmd = cJSON_GetObjectItem(root, "command");
    if (cmd == NULL) {
        ESP_LOGE(TAG, "Missing 'command' field");
        cJSON_Delete(root);
        return;
    }

    const char *command = cmd->valuestring;

    // Handle different commands
    if (strcmp(command, "restart") == 0) {
        ESP_LOGI(TAG, "🔄 Restart command received");
        esp_restart();
    }
    else if (strcmp(command, "read_sensors") == 0) {
        ESP_LOGI(TAG, "📊 Read sensors command received");
        // Trigger immediate sensor reading
        xTaskNotifyGive(modbus_task_handle);
    }
    else if (strcmp(command, "set_interval") == 0) {
        cJSON *interval = cJSON_GetObjectItem(root, "interval");
        if (interval != NULL) {
            g_system_config.telemetry_interval = interval->valueint;
            ESP_LOGI(TAG, "⏱️ Telemetry interval set to %d seconds",
                     g_system_config.telemetry_interval);
            save_config_to_nvs();
        }
    }
    else {
        ESP_LOGW(TAG, "Unknown command: %s", command);
    }

    cJSON_Delete(root);
}
```

### Send Command from Azure (Azure Portal)

```
1. Navigate to IoT Hub → Devices → Select "testing_3"
2. Click "Message to Device"
3. Enter message body (JSON):
   {
     "command": "read_sensors"
   }
4. Click "Send Message"
```

### Send Command from Azure (Azure CLI)

```bash
az iot device c2d-message send \
  --hub-name my-iot-hub \
  --device-id testing_3 \
  --data '{"command":"restart"}'
```

---

## 8. Connection Management

### SAS Token Refresh

```c
// Refresh SAS token every hour (before expiry)
void sas_token_refresh_task(void *pvParameters) {
    while (1) {
        // Wait 50 minutes (refresh 10 minutes before expiry)
        vTaskDelay(pdMS_TO_TICKS(50 * 60 * 1000));

        ESP_LOGI(TAG, "🔑 Refreshing SAS token...");

        // Generate new token
        char sas_token[512];
        time_t expiry = time(NULL) + 3600;
        generate_sas_token(...);

        // Disconnect and reconnect with new token
        esp_mqtt_client_stop(mqtt_client);
        esp_mqtt_client_destroy(mqtt_client);

        // Reinitialize with new token
        mqtt_init();

        ESP_LOGI(TAG, "✅ SAS token refreshed");
    }
}
```

### Reconnection Strategy

```c
// Automatic reconnection with exponential backoff
int reconnect_attempts = 0;
int max_attempts = 5;
int base_delay_ms = 5000;  // 5 seconds

void mqtt_reconnect(void) {
    while (reconnect_attempts < max_attempts) {
        ESP_LOGI(TAG, "🔄 MQTT reconnection attempt %d/%d",
                 reconnect_attempts + 1, max_attempts);

        esp_err_t err = esp_mqtt_client_reconnect(mqtt_client);

        if (err == ESP_OK) {
            ESP_LOGI(TAG, "✅ MQTT reconnected successfully");
            reconnect_attempts = 0;
            return;
        }

        // Exponential backoff
        int delay_ms = base_delay_ms * (1 << reconnect_attempts);
        ESP_LOGW(TAG, "❌ Reconnection failed, retrying in %d ms", delay_ms);
        vTaskDelay(pdMS_TO_TICKS(delay_ms));

        reconnect_attempts++;
    }

    ESP_LOGE(TAG, "❌ Max reconnection attempts reached");
    // Optionally: restart device or trigger alarm
}
```

---

## 9. Monitoring and Diagnostics

### View Telemetry in Azure Portal

```
1. Navigate to IoT Hub → Overview
2. Check "Device-to-cloud messages" metric
3. For detailed messages:
   - IoT Hub → Built-in endpoints → Events
   - Click "View data" (opens Event Hub consumer)

Alternative (better):
Use Azure IoT Explorer desktop app
```

### Azure IoT Explorer Setup

```bash
1. Download Azure IoT Explorer:
   https://github.com/Azure/azure-iot-explorer/releases

2. Launch application

3. Add IoT Hub connection:
   - Copy IoT Hub connection string from Azure Portal:
     IoT Hub → Settings → Shared access policies → iothubowner
   - Paste into Azure IoT Explorer

4. View device telemetry:
   - Select device "testing_3"
   - Click "Telemetry"
   - Click "Start"
   - See live telemetry data
```

### Azure CLI Monitoring

```bash
# Monitor device-to-cloud messages
az iot hub monitor-events \
  --hub-name my-iot-hub \
  --device-id testing_3

# View device properties
az iot hub device-identity show \
  --hub-name my-iot-hub \
  --device-id testing_3

# Get device statistics
az iot hub device-identity list \
  --hub-name my-iot-hub \
  --query "[].{DeviceId:deviceId,Status:status,ConnectionState:connectionState}"
```

### ESP32 Diagnostics

```c
// Log connection status
void log_mqtt_status(void) {
    ESP_LOGI(TAG, "=== MQTT Status ===");
    ESP_LOGI(TAG, "Connected: %s", mqtt_client ? "Yes" : "No");
    ESP_LOGI(TAG, "Device ID: %s", AZURE_DEVICE_ID);
    ESP_LOGI(TAG, "Hub FQDN: %s", IOT_CONFIG_IOTHUB_FQDN);
    ESP_LOGI(TAG, "Telemetry interval: %d seconds",
             g_system_config.telemetry_interval);
    ESP_LOGI(TAG, "Messages sent: %d", telemetry_count);
    ESP_LOGI(TAG, "Failed sends: %d", failed_sends);
    ESP_LOGI(TAG, "==================");
}
```

---

## 10. Troubleshooting

### Connection Refused (Code 5)

```
Error: MQTT connection refused, return code 5

Causes:
□ Invalid SAS token (expired or incorrect)
□ Wrong device ID or hub name
□ Device key is incorrect
□ Device is disabled in Azure portal
□ Clock time is incorrect (affects SAS token)

Solutions:
1. Verify device credentials in Azure portal
2. Check system time with NTP:
   sntp_sync_time();
3. Regenerate SAS token
4. Ensure device is enabled in Azure portal
```

### TLS/SSL Errors

```
Error: TLS handshake failed

Causes:
□ Missing or incorrect root CA certificate
□ Clock time is incorrect (certificate validation fails)
□ Network firewall blocks port 8883

Solutions:
1. Verify azure_ca_cert.pem is correct (Baltimore CyberTrust Root)
2. Sync time with NTP before connecting
3. Test connection:
   openssl s_client -connect my-iot-hub.azure-devices.net:8883
```

### Publish Failed

```
Error: esp_mqtt_client_publish returned -1

Causes:
□ Not connected to MQTT broker
□ Payload too large (max 256KB)
□ Network connectivity issues

Solutions:
1. Check MQTT connection status
2. Reduce payload size
3. Verify WiFi connection:
   wifi_status = esp_wifi_sta_get_ap_info(&ap_info);
```

### No Telemetry Appearing in Azure

```
Checklist:
□ Check device connection state in Azure portal
□ Verify telemetry topic format
□ Check JSON payload is valid
□ Ensure QoS is 0 or 1 (not 2)
□ Check Azure IoT Hub service status
□ Verify message routing rules (if configured)

Test:
Use Azure IoT Explorer to monitor live telemetry
```

---

## 11. Best Practices

### Security

```c
// ✅ Do:
- Store device key in NVS (encrypted if possible)
- Regenerate SAS tokens before expiry
- Use TLS/SSL for all connections
- Validate device key format before use
- Rotate device keys periodically

// ❌ Don't:
- Hardcode device keys in source code
- Share device keys across multiple devices
- Disable certificate validation
- Use expired SAS tokens
- Expose device keys in logs
```

### Performance

```c
// Optimize telemetry frequency
#define MIN_TELEMETRY_INTERVAL 30   // seconds
#define MAX_TELEMETRY_INTERVAL 3600 // seconds

// Batch sensor readings
void telemetry_task(void *pvParameters) {
    while (1) {
        // Wait for interval
        vTaskDelay(pdMS_TO_TICKS(telemetry_interval * 1000));

        // Read all sensors at once
        sensor_reading_t readings[MAX_SENSORS];
        int count = read_all_sensors(readings);

        // Publish in single message
        publish_telemetry(readings, count);
    }
}

// Use QoS 1 for important messages, QoS 0 for high-frequency data
esp_mqtt_client_publish(client, topic, payload, 0, 1, 0);  // QoS 1
```

### Reliability

```c
// Implement offline caching (SD card)
if (mqtt_publish_failed) {
    if (sd_config.cache_on_failure) {
        save_to_sd_card(telemetry_json);
    }
}

// Replay cached messages when connected
if (mqtt_connected && has_cached_messages()) {
    replay_cached_messages();
}

// Monitor heap usage
if (esp_get_free_heap_size() < 50000) {
    ESP_LOGW(TAG, "⚠️ Low heap memory: %d bytes",
             esp_get_free_heap_size());
}
```

---

## 12. Advanced Features

### Device Twins (Future Enhancement)

```c
// Subscribe to device twin updates
esp_mqtt_client_subscribe(client,
    "$iothub/twin/PATCH/properties/desired/#", 1);

// Request current twin state
esp_mqtt_client_publish(client,
    "$iothub/twin/GET/?$rid=1", "", 0, 1, 0);

// Report twin properties
const char *twin_update = "{\"firmwareVersion\":\"1.1.0\"}";
esp_mqtt_client_publish(client,
    "$iothub/twin/PATCH/properties/reported/?$rid=2",
    twin_update, 0, 1, 0);
```

### Direct Methods (Future Enhancement)

```c
// Subscribe to direct method requests
esp_mqtt_client_subscribe(client,
    "$iothub/methods/POST/#", 1);

// Handle method invocation
void process_direct_method(const char *method_name,
                          const char *payload,
                          int request_id) {
    cJSON *response = cJSON_CreateObject();
    int status = 200;

    if (strcmp(method_name, "reboot") == 0) {
        cJSON_AddStringToObject(response, "result", "Rebooting...");
        // Send response first
        send_method_response(request_id, status, response);
        esp_restart();
    } else {
        status = 404;
        cJSON_AddStringToObject(response, "error", "Method not found");
        send_method_response(request_id, status, response);
    }

    cJSON_Delete(response);
}
```

---

## Conclusion

This guide covers complete Azure IoT Hub integration for ESP32 devices. Use it when:
- Connecting devices to Azure cloud
- Implementing secure authentication
- Publishing telemetry data
- Receiving cloud commands
- Troubleshooting connectivity issues

**Key Takeaways:**
- Use MQTT over TLS (port 8883) for Azure IoT Hub
- Generate SAS tokens dynamically (refresh before expiry)
- Implement reconnection with exponential backoff
- Cache telemetry offline when disconnected
- Monitor connection status and diagnostics
- Follow security best practices

**Related Resources:**
- [Azure IoT Hub MQTT Support](https://docs.microsoft.com/en-us/azure/iot-hub/iot-hub-mqtt-support)
- [Azure IoT Explorer](https://github.com/Azure/azure-iot-explorer)
- [ESP-IDF MQTT Client](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/protocols/mqtt.html)
- ESP32 Modbus Gateway Technical Reference

---

**Version**: 1.0
**Last Updated**: 2025-01-12
**Project**: ESP32 Modbus IoT Gateway
