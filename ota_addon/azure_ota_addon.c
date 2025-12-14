/**
 * ============================================================================
 * AZURE IOT HUB OTA ADDON - Single File Solution
 * ============================================================================
 *
 * PURPOSE: Add this file to ANY ESP32 project to enable OTA updates via
 *          Azure IoT Hub Device Twin.
 *
 * REQUIREMENTS:
 *   1. ESP-IDF v5.x project
 *   2. Partition table with OTA partitions (ota_0, ota_1)
 *   3. Azure IoT Hub with device registered
 *
 * HOW TO USE:
 *   1. Copy this file to your project's main/ folder
 *   2. Add to CMakeLists.txt: azure_ota_addon.c
 *   3. Call azure_ota_init() from your app_main()
 *   4. Configure credentials via the functions or hardcode below
 *
 * OTA TRIGGER:
 *   Update Device Twin in Azure IoT Hub:
 *   {
 *     "properties": {
 *       "desired": {
 *         "ota_url": "https://your-server.com/firmware.bin"
 *       }
 *     }
 *   }
 *
 * Author: FluxGen Engineering
 * License: MIT
 * ============================================================================
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_ota_ops.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "esp_tls.h"
#include "esp_crt_bundle.h"
#include "nvs_flash.h"
#include "mqtt_client.h"
#include "cJSON.h"
#include "mbedtls/base64.h"
#include "mbedtls/md.h"

static const char *TAG = "AZURE_OTA";

// ============================================================================
// CONFIGURATION - Modify these for your project
// ============================================================================

// Option 1: Hardcode credentials (simple but less secure)
#define AZURE_IOT_HUB_FQDN      "your-hub.azure-devices.net"
#define AZURE_DEVICE_ID         "your-device-id"
#define AZURE_DEVICE_KEY        "your-device-primary-key"

// Option 2: Set credentials at runtime via azure_ota_set_credentials()

// SAS Token validity (seconds)
#define SAS_TOKEN_VALIDITY_SECS  86400  // 24 hours

// OTA Configuration
#define OTA_RECV_TIMEOUT_MS     60000   // 60 seconds
#define OTA_BUFFER_SIZE         4096
#define OTA_MAX_REDIRECTS       5

// ============================================================================
// INTERNAL VARIABLES
// ============================================================================

static esp_mqtt_client_handle_t s_mqtt_client = NULL;
static bool s_mqtt_connected = false;
static char s_sas_token[512] = {0};
static char s_ota_url[512] = {0};
static bool s_ota_in_progress = false;
static SemaphoreHandle_t s_ota_mutex = NULL;

// Runtime credentials (override hardcoded if set)
static char s_hub_fqdn[128] = {0};
static char s_device_id[64] = {0};
static char s_device_key[128] = {0};

// ============================================================================
// HELPER FUNCTIONS
// ============================================================================

static const char* get_hub_fqdn(void) {
    return strlen(s_hub_fqdn) > 0 ? s_hub_fqdn : AZURE_IOT_HUB_FQDN;
}

static const char* get_device_id(void) {
    return strlen(s_device_id) > 0 ? s_device_id : AZURE_DEVICE_ID;
}

static const char* get_device_key(void) {
    return strlen(s_device_key) > 0 ? s_device_key : AZURE_DEVICE_KEY;
}

/**
 * URL encode a string
 */
static void url_encode(const char* input, char* output, size_t output_size) {
    size_t i, j = 0;
    for (i = 0; input[i] && j < output_size - 4; i++) {
        char c = input[i];
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') {
            output[j++] = c;
        } else {
            snprintf(&output[j], 4, "%%%02X", (unsigned char)c);
            j += 3;
        }
    }
    output[j] = '\0';
}

/**
 * Generate Azure IoT Hub SAS Token
 */
static int generate_sas_token(void) {
    char resource_uri[256];
    char encoded_uri[512];
    char signature_raw[256];
    unsigned char hmac_output[32];
    char signature_b64[64];
    size_t signature_b64_len;
    char encoded_signature[128];

    uint32_t expiry = (uint32_t)(time(NULL) + SAS_TOKEN_VALIDITY_SECS);

    // Resource URI
    snprintf(resource_uri, sizeof(resource_uri), "%s/devices/%s",
             get_hub_fqdn(), get_device_id());
    url_encode(resource_uri, encoded_uri, sizeof(encoded_uri));

    // String to sign
    snprintf(signature_raw, sizeof(signature_raw), "%s\n%lu", encoded_uri, (unsigned long)expiry);

    // Decode device key
    unsigned char decoded_key[64];
    size_t decoded_key_len;
    int ret = mbedtls_base64_decode(decoded_key, sizeof(decoded_key), &decoded_key_len,
                                    (const unsigned char*)get_device_key(), strlen(get_device_key()));
    if (ret != 0) {
        ESP_LOGE(TAG, "Failed to decode device key: %d", ret);
        return -1;
    }

    // HMAC-SHA256
    ret = mbedtls_md_hmac(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256),
                          decoded_key, decoded_key_len,
                          (const unsigned char*)signature_raw, strlen(signature_raw),
                          hmac_output);
    if (ret != 0) {
        ESP_LOGE(TAG, "HMAC failed: %d", ret);
        return -1;
    }

    // Base64 encode signature
    mbedtls_base64_encode((unsigned char*)signature_b64, sizeof(signature_b64),
                          &signature_b64_len, hmac_output, 32);
    signature_b64[signature_b64_len] = '\0';

    // URL encode signature
    url_encode(signature_b64, encoded_signature, sizeof(encoded_signature));

    // Build SAS token
    snprintf(s_sas_token, sizeof(s_sas_token),
             "SharedAccessSignature sr=%s&sig=%s&se=%lu",
             encoded_uri, encoded_signature, (unsigned long)expiry);

    ESP_LOGI(TAG, "SAS token generated (expires in %d seconds)", SAS_TOKEN_VALIDITY_SECS);
    return 0;
}

// ============================================================================
// OTA UPDATE IMPLEMENTATION
// ============================================================================

// Redirect URL storage for HTTP event handler
static char s_redirect_url[512] = {0};

static esp_err_t ota_http_event_handler(esp_http_client_event_t *evt) {
    switch (evt->event_id) {
        case HTTP_EVENT_ON_HEADER:
            if (strcasecmp(evt->header_key, "Location") == 0 && evt->header_value) {
                strncpy(s_redirect_url, evt->header_value, sizeof(s_redirect_url) - 1);
                ESP_LOGI(TAG, "Redirect URL: %s", s_redirect_url);
            }
            break;
        default:
            break;
    }
    return ESP_OK;
}

static void ota_task(void *pvParameters) {
    char *url = (char*)pvParameters;
    esp_err_t ret;

    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "       STARTING OTA UPDATE");
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "URL: %s", url);
    ESP_LOGI(TAG, "Free heap: %lu bytes", esp_get_free_heap_size());

    // Get OTA partition
    const esp_partition_t *update_partition = esp_ota_get_next_update_partition(NULL);
    if (!update_partition) {
        ESP_LOGE(TAG, "No OTA partition found!");
        goto cleanup;
    }
    ESP_LOGI(TAG, "Writing to partition: %s @ 0x%lx",
             update_partition->label, update_partition->address);

    esp_ota_handle_t ota_handle;
    ret = esp_ota_begin(update_partition, OTA_WITH_SEQUENTIAL_WRITES, &ota_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_begin failed: %s", esp_err_to_name(ret));
        goto cleanup;
    }

    // Handle redirects (GitHub, etc.)
    char current_url[512];
    strncpy(current_url, url, sizeof(current_url) - 1);

    for (int redirect = 0; redirect < OTA_MAX_REDIRECTS; redirect++) {
        s_redirect_url[0] = '\0';

        esp_http_client_config_t http_config = {
            .url = current_url,
            .timeout_ms = OTA_RECV_TIMEOUT_MS,
            .buffer_size = OTA_BUFFER_SIZE,
            .buffer_size_tx = 2048,
            .event_handler = ota_http_event_handler,
            .crt_bundle_attach = esp_crt_bundle_attach,
            .skip_cert_common_name_check = true,
        };

        esp_http_client_handle_t client = esp_http_client_init(&http_config);
        if (!client) {
            ESP_LOGE(TAG, "Failed to create HTTP client");
            esp_ota_abort(ota_handle);
            goto cleanup;
        }

        ret = esp_http_client_open(client, 0);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "HTTP open failed: %s", esp_err_to_name(ret));
            esp_http_client_cleanup(client);
            esp_ota_abort(ota_handle);
            goto cleanup;
        }

        int content_length = esp_http_client_fetch_headers(client);
        int status_code = esp_http_client_get_status_code(client);

        ESP_LOGI(TAG, "HTTP Status: %d, Content-Length: %d", status_code, content_length);

        // Handle redirect
        if (status_code == 301 || status_code == 302 || status_code == 307 || status_code == 308) {
            esp_http_client_cleanup(client);
            if (strlen(s_redirect_url) > 0) {
                ESP_LOGI(TAG, "Following redirect %d...", redirect + 1);
                strncpy(current_url, s_redirect_url, sizeof(current_url) - 1);
                continue;
            } else {
                ESP_LOGE(TAG, "Redirect without Location header");
                esp_ota_abort(ota_handle);
                goto cleanup;
            }
        }

        if (status_code != 200) {
            ESP_LOGE(TAG, "HTTP error: %d", status_code);
            esp_http_client_cleanup(client);
            esp_ota_abort(ota_handle);
            goto cleanup;
        }

        // Download firmware
        char *buffer = malloc(OTA_BUFFER_SIZE);
        if (!buffer) {
            ESP_LOGE(TAG, "Failed to allocate buffer");
            esp_http_client_cleanup(client);
            esp_ota_abort(ota_handle);
            goto cleanup;
        }

        int total_read = 0;
        int last_progress = -1;

        while (1) {
            int read_len = esp_http_client_read(client, buffer, OTA_BUFFER_SIZE);
            if (read_len < 0) {
                ESP_LOGE(TAG, "Read error");
                break;
            } else if (read_len == 0) {
                break;  // Done
            }

            ret = esp_ota_write(ota_handle, buffer, read_len);
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "OTA write failed: %s", esp_err_to_name(ret));
                break;
            }

            total_read += read_len;

            // Progress update every 10%
            if (content_length > 0) {
                int progress = (total_read * 100) / content_length;
                if (progress / 10 > last_progress / 10) {
                    last_progress = progress;
                    ESP_LOGI(TAG, "Progress: %d%% (%d/%d bytes)", progress, total_read, content_length);
                }
            }
        }

        free(buffer);
        esp_http_client_cleanup(client);

        if (ret == ESP_OK && total_read > 0) {
            ESP_LOGI(TAG, "Download complete: %d bytes", total_read);

            ret = esp_ota_end(ota_handle);
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "esp_ota_end failed: %s", esp_err_to_name(ret));
                goto cleanup;
            }

            ret = esp_ota_set_boot_partition(update_partition);
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "esp_ota_set_boot_partition failed: %s", esp_err_to_name(ret));
                goto cleanup;
            }

            ESP_LOGI(TAG, "========================================");
            ESP_LOGI(TAG, "    OTA UPDATE SUCCESSFUL!");
            ESP_LOGI(TAG, "    Rebooting in 5 seconds...");
            ESP_LOGI(TAG, "========================================");

            vTaskDelay(pdMS_TO_TICKS(5000));
            esp_restart();
        }

        break;  // Exit redirect loop
    }

cleanup:
    ESP_LOGE(TAG, "OTA update failed!");
    s_ota_in_progress = false;
    if (s_ota_mutex) xSemaphoreGive(s_ota_mutex);
    vTaskDelete(NULL);
}

/**
 * Start OTA update from URL
 */
static void start_ota_update(const char* url) {
    if (s_ota_in_progress) {
        ESP_LOGW(TAG, "OTA already in progress");
        return;
    }

    if (s_ota_mutex == NULL) {
        s_ota_mutex = xSemaphoreCreateMutex();
    }

    if (xSemaphoreTake(s_ota_mutex, 0) != pdTRUE) {
        ESP_LOGW(TAG, "Could not acquire OTA mutex");
        return;
    }

    s_ota_in_progress = true;
    strncpy(s_ota_url, url, sizeof(s_ota_url) - 1);

    xTaskCreate(ota_task, "ota_task", 8192, s_ota_url, 5, NULL);
}

// ============================================================================
// DEVICE TWIN HANDLER
// ============================================================================

static void handle_device_twin(const char* data, int len) {
    ESP_LOGI(TAG, "Device Twin update received");

    cJSON *root = cJSON_ParseWithLength(data, len);
    if (!root) {
        ESP_LOGE(TAG, "Failed to parse Device Twin JSON");
        return;
    }

    // Check for OTA URL in desired properties
    cJSON *ota_url = cJSON_GetObjectItem(root, "ota_url");
    if (ota_url && cJSON_IsString(ota_url) && strlen(ota_url->valuestring) > 0) {
        ESP_LOGI(TAG, "OTA URL found: %s", ota_url->valuestring);
        start_ota_update(ota_url->valuestring);
    }

    cJSON_Delete(root);
}

// ============================================================================
// MQTT EVENT HANDLER
// ============================================================================

static void mqtt_event_handler(void *handler_args, esp_event_base_t base,
                                int32_t event_id, void *event_data) {
    esp_mqtt_event_handle_t event = event_data;

    switch ((esp_mqtt_event_id_t)event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "MQTT Connected to Azure IoT Hub");
            s_mqtt_connected = true;

            // Subscribe to Device Twin desired properties
            char topic[128];
            snprintf(topic, sizeof(topic), "$iothub/twin/PATCH/properties/desired/#");
            esp_mqtt_client_subscribe(s_mqtt_client, topic, 1);
            ESP_LOGI(TAG, "Subscribed to Device Twin updates");

            // Request full Device Twin
            snprintf(topic, sizeof(topic), "$iothub/twin/GET/?$rid=0");
            esp_mqtt_client_publish(s_mqtt_client, topic, "", 0, 1, 0);
            break;

        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGW(TAG, "MQTT Disconnected");
            s_mqtt_connected = false;
            break;

        case MQTT_EVENT_DATA:
            // Check if this is a Device Twin message
            if (event->topic && strstr(event->topic, "$iothub/twin")) {
                handle_device_twin(event->data, event->data_len);
            }
            break;

        case MQTT_EVENT_ERROR:
            ESP_LOGE(TAG, "MQTT Error");
            if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
                ESP_LOGE(TAG, "TCP transport error: %s",
                         esp_err_to_name(event->error_handle->esp_transport_sock_errno));
            }
            break;

        default:
            break;
    }
}

// ============================================================================
// PUBLIC API
// ============================================================================

/**
 * Set Azure IoT Hub credentials at runtime
 * Call this BEFORE azure_ota_init() if not using hardcoded values
 */
void azure_ota_set_credentials(const char* hub_fqdn, const char* device_id, const char* device_key) {
    if (hub_fqdn) strncpy(s_hub_fqdn, hub_fqdn, sizeof(s_hub_fqdn) - 1);
    if (device_id) strncpy(s_device_id, device_id, sizeof(s_device_id) - 1);
    if (device_key) strncpy(s_device_key, device_key, sizeof(s_device_key) - 1);
    ESP_LOGI(TAG, "Credentials set - Hub: %s, Device: %s", get_hub_fqdn(), get_device_id());
}

/**
 * Initialize Azure OTA addon
 * Call this from your app_main() AFTER WiFi/network is connected
 */
esp_err_t azure_ota_init(void) {
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "   AZURE IOT HUB OTA ADDON v1.0");
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "Hub: %s", get_hub_fqdn());
    ESP_LOGI(TAG, "Device: %s", get_device_id());

    // Sync time (required for SAS token)
    ESP_LOGI(TAG, "Syncing time via SNTP...");
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_init();

    // Wait for time sync (max 30 seconds)
    time_t now = 0;
    int retry = 0;
    while (now < 1700000000 && retry < 30) {  // Year ~2023
        vTaskDelay(pdMS_TO_TICKS(1000));
        time(&now);
        retry++;
    }

    if (now < 1700000000) {
        ESP_LOGE(TAG, "Time sync failed - SAS token may not work");
    } else {
        ESP_LOGI(TAG, "Time synced: %lld", (long long)now);
    }

    // Generate SAS token
    if (generate_sas_token() != 0) {
        ESP_LOGE(TAG, "Failed to generate SAS token");
        return ESP_FAIL;
    }

    // Build MQTT broker URI and username
    char mqtt_uri[256];
    char mqtt_username[256];
    snprintf(mqtt_uri, sizeof(mqtt_uri), "mqtts://%s:8883", get_hub_fqdn());
    snprintf(mqtt_username, sizeof(mqtt_username), "%s/%s/?api-version=2021-04-12",
             get_hub_fqdn(), get_device_id());

    // Configure MQTT client
    esp_mqtt_client_config_t mqtt_config = {
        .broker.address.uri = mqtt_uri,
        .credentials.client_id = get_device_id(),
        .credentials.username = mqtt_username,
        .credentials.authentication.password = s_sas_token,
        .session.keepalive = 120,
        .network.timeout_ms = 30000,
        .buffer.size = 4096,
    };

    s_mqtt_client = esp_mqtt_client_init(&mqtt_config);
    if (!s_mqtt_client) {
        ESP_LOGE(TAG, "Failed to create MQTT client");
        return ESP_FAIL;
    }

    esp_mqtt_client_register_event(s_mqtt_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(s_mqtt_client);

    ESP_LOGI(TAG, "Azure OTA addon initialized - waiting for Device Twin updates");
    return ESP_OK;
}

/**
 * Check if MQTT is connected
 */
bool azure_ota_is_connected(void) {
    return s_mqtt_connected;
}

/**
 * Manually trigger OTA update (optional - normally triggered via Device Twin)
 */
void azure_ota_trigger(const char* url) {
    start_ota_update(url);
}
