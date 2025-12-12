#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"
#include "driver/gpio.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_sntp.h"
#include "esp_timer.h"
#include "esp_task_wdt.h"
#include "lwip/netdb.h"
#include "lwip/sockets.h"
#include "mqtt_client.h"
#include "mbedtls/md.h"
#include "mbedtls/base64.h"
#include "mbedtls/sha256.h"

#include "iot_configs.h"
#include "modbus.h"
#include "web_config.h"
#include "sensor_manager.h"
#include "network_stats.h"
#include "json_templates.h"
#include "sd_card_logger.h"
#include "ds3231_rtc.h"
#include "a7670c_ppp.h"
#include "telegram_bot.h"
#include "ota_update.h"
#include "cJSON.h"
#include "esp_crt_bundle.h"

static const char *TAG = "AZURE_IOT";

// GPIO configuration for AP mode trigger
// GPIO configuration for AP mode triggers
#define CONFIG_GPIO_PIN 34
#define CONFIG_GPIO_BOOT_PIN 0
#define CONFIG_GPIO_INTR_FLAG_34 GPIO_INTR_POSEDGE    // GPIO 34: trigger on rising edge
#define CONFIG_GPIO_INTR_FLAG_0 GPIO_INTR_NEGEDGE     // GPIO 0: trigger on falling edge

// GPIO configuration for modem reset
#define MODEM_RESET_GPIO_PIN 2

// GPIO configuration for status LEDs (LOW = LED ON)
#define WEBSERVER_LED_GPIO_PIN 25   // Web server active LED
#define MQTT_LED_GPIO_PIN 26        // MQTT connection status LED  
#define SENSOR_LED_GPIO_PIN 27      // Sensor response status LED

// WiFi event group
// static EventGroupHandle_t wifi_event_group;  // Unused variable
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

// Task handles for dual core usage
TaskHandle_t modbus_task_handle = NULL;
TaskHandle_t mqtt_task_handle = NULL;
TaskHandle_t telemetry_task_handle = NULL;
QueueHandle_t sensor_data_queue = NULL;

// Global variables
static esp_mqtt_client_handle_t mqtt_client;
static char sas_token[512];
static uint32_t telemetry_send_count = 0;
volatile bool mqtt_connected = false;  // Non-static for external access, volatile for thread-safe reads

// Flow meter configuration now comes from web interface (NVS storage)
// Hardcoded configuration removed - using dynamic sensor configuration

// Configuration flags for debugging different byte orders (moved to web_config.h)

static flow_meter_data_t current_flow_data = {0};

// Production monitoring variables
uint32_t mqtt_reconnect_count = 0;  // Non-static for external access
static uint32_t modbus_failure_count = 0;

// System reliability constants (using values from iot_configs.h)
uint32_t total_telemetry_sent = 0;  // Non-static for external access
static uint32_t system_uptime_start = 0;

// MQTT connection timing for status monitoring
int64_t mqtt_connect_time = 0;  // Timestamp when MQTT connected
int64_t last_telemetry_time = 0;  // Timestamp of last telemetry sent

// SAS Token management for auto-refresh
static int64_t sas_token_generated_time = 0;  // When token was created (epoch seconds)
static uint32_t sas_token_expiry_seconds = 3600;  // Token validity (1 hour)
#define SAS_TOKEN_REFRESH_MARGIN_SEC 300  // Refresh 5 minutes before expiry

// NTP periodic sync management
static int64_t last_ntp_sync_time = 0;  // Last successful NTP sync (epoch seconds)
#define NTP_RESYNC_INTERVAL_SEC (24 * 60 * 60)  // Re-sync every 24 hours

// Recovery and monitoring variables
static int64_t last_successful_telemetry_time = 0;  // For auto-recovery timeout
static int64_t last_heartbeat_time = 0;             // For SD card heartbeat logging
static uint32_t telemetry_failure_count = 0;        // Track consecutive failures
static uint32_t system_restart_count = 0;           // Loaded from NVS on boot

// Static buffers to avoid stack overflow
static char mqtt_broker_uri[256];
static char mqtt_username[256];
static char telemetry_topic[256];
static char telemetry_payload[8192];  // Increased to support up to 20 sensors
static char c2d_topic[256];

// Static buffers for telemetry to prevent heap fragmentation
// These replace malloc/free calls that were causing memory exhaustion
static sensor_reading_t telemetry_readings[20];  // Pre-allocated sensor readings
static char telemetry_temp_json[MAX_JSON_PAYLOAD_SIZE];  // Pre-allocated JSON buffer
static int sensors_already_published = 0;  // Track sensors published in create_telemetry_payload

// GPIO interrupt flag for web server toggle
static volatile bool web_server_toggle_requested = false;
static volatile bool system_shutdown_requested = false;
static volatile bool web_server_running = false;
static volatile bool wifi_initialized_for_sim_mode = false;  // Track if WiFi was init'd in SIM mode

// Modem control variables
static bool modem_reset_enabled = false;
static int modem_reset_gpio_pin = 2; // Default GPIO pin, configurable via web interface
static TaskHandle_t modem_reset_task_handle = NULL;
static int64_t last_modem_reset_time = 0;  // Track last reset to prevent rapid cycling
#define MODEM_RESET_COOLDOWN_SEC 300  // 5 minutes between reset attempts

// LED status variables
static volatile bool sensors_responding = false;
static volatile bool webserver_led_on = false;
static volatile bool mqtt_led_on = false;
static volatile bool sensor_led_on = false;

// Semaphore for task startup synchronization to prevent log interleaving
static SemaphoreHandle_t startup_log_mutex = NULL;

// Telemetry history buffer for web interface
#define TELEMETRY_HISTORY_SIZE 25
typedef struct {
    char timestamp[32];
    char payload[400];  // Truncated version for display
    bool success;
} telemetry_record_t;

static telemetry_record_t telemetry_history[TELEMETRY_HISTORY_SIZE];
static int telemetry_history_index = 0;
static int telemetry_history_count = 0;
static SemaphoreHandle_t telemetry_history_mutex = NULL;

// Forward declarations
static bool send_telemetry(void);
static void init_modem_reset_gpio(void);
static void perform_modem_reset(void);
static void modem_reset_task(void *pvParameters);
static void mqtt_task(void *pvParameters);
static void telemetry_task(void *pvParameters);
static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data);

// Function to add telemetry to history buffer
static void add_telemetry_to_history(const char *payload, bool success) {
    if (telemetry_history_mutex == NULL) return;

    if (xSemaphoreTake(telemetry_history_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        // Get current timestamp in 24-hour format
        time_t now = time(NULL);
        struct tm timeinfo;
        localtime_r(&now, &timeinfo);  // Use local time instead of UTC
        strftime(telemetry_history[telemetry_history_index].timestamp,
                 sizeof(telemetry_history[telemetry_history_index].timestamp),
                 "%d-%m-%Y %H:%M:%S", &timeinfo);  // DD-MM-YYYY HH:MM:SS (24-hour)

        // Copy payload (truncate if needed)
        strncpy(telemetry_history[telemetry_history_index].payload, payload,
                sizeof(telemetry_history[telemetry_history_index].payload) - 1);
        telemetry_history[telemetry_history_index].payload[sizeof(telemetry_history[telemetry_history_index].payload) - 1] = '\0';

        // Set success flag
        telemetry_history[telemetry_history_index].success = success;

        // Update circular buffer indices
        telemetry_history_index = (telemetry_history_index + 1) % TELEMETRY_HISTORY_SIZE;
        if (telemetry_history_count < TELEMETRY_HISTORY_SIZE) {
            telemetry_history_count++;
        }

        xSemaphoreGive(telemetry_history_mutex);
    }
}

// Function to get telemetry history as JSON (called from web_config.c)
int get_telemetry_history_json(char *buffer, size_t buffer_size) {
    if (buffer == NULL || buffer_size < 10) {
        return 0;
    }

    // Always return valid JSON array, even if mutex not ready
    if (telemetry_history_mutex == NULL) {
        return snprintf(buffer, buffer_size, "[]");
    }

    int written = 0;
    written += snprintf(buffer + written, buffer_size - written, "[");

    if (xSemaphoreTake(telemetry_history_mutex, pdMS_TO_TICKS(200)) == pdTRUE) {
        int count = 0;
        // Read in reverse order (newest first)
        for (int i = telemetry_history_count - 1; i >= 0; i--) {
            int actual_index = (telemetry_history_index - 1 - (telemetry_history_count - 1 - i) + TELEMETRY_HISTORY_SIZE) % TELEMETRY_HISTORY_SIZE;

            if (count > 0) {
                written += snprintf(buffer + written, buffer_size - written, ",");
            }

            written += snprintf(buffer + written, buffer_size - written,
                "{\"timestamp\":\"%s\",\"payload\":%s,\"success\":%s}",
                telemetry_history[actual_index].timestamp,
                telemetry_history[actual_index].payload,
                telemetry_history[actual_index].success ? "true" : "false");

            count++;
            if (written >= buffer_size - 100) break; // Leave room for closing bracket
        }
        xSemaphoreGive(telemetry_history_mutex);
    }

    written += snprintf(buffer + written, buffer_size - written, "]");
    return written;
}
static esp_err_t reinit_modem_reset_gpio(int new_gpio_pin);
static void start_web_server(void);
static void stop_web_server(void);
static void handle_web_server_toggle(void);
static void init_status_leds(void);
static void set_status_led(int gpio_pin, bool on);
static void update_led_status(void);
static bool is_network_connected(void);

// Helper function to check network connectivity (replaces network_manager_is_connected)
static bool is_network_connected(void) {
    system_config_t *config = get_system_config();
    if (!config) return false;

    if (config->network_mode == NETWORK_MODE_WIFI) {
        // Check WiFi connection
        wifi_ap_record_t ap_info;
        return (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK);
    } else {
        // Check SIM/PPP connection
        return a7670c_is_connected();
    }
}

// Azure IoT Root CA Certificate
extern const uint8_t azure_root_ca_pem_start[] asm("_binary_azure_ca_cert_pem_start");
extern const uint8_t azure_root_ca_pem_end[] asm("_binary_azure_ca_cert_pem_end");

// Legacy WiFi event handler - replaced by web_config system
/*static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                               int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        ESP_LOGI(TAG, "WiFi station started, connecting to AP...");
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_CONNECTED) {
        wifi_event_sta_connected_t* event = (wifi_event_sta_connected_t*) event_data;
        ESP_LOGI(TAG, "Connected to AP SSID:%s channel:%d", event->ssid, event->channel);
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_event_sta_disconnected_t* event = (wifi_event_sta_disconnected_t*) event_data;
        ESP_LOGW(TAG, "Disconnected from AP. Reason: %d. Retrying...", event->reason);
        esp_wifi_connect();
        xEventGroupClearBits(wifi_event_group, WIFI_CONNECTED_BIT);
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "Got IP:" IPSTR, IP2STR(&event->ip_info.ip));
        ESP_LOGI(TAG, "Netmask:" IPSTR, IP2STR(&event->ip_info.netmask));
        ESP_LOGI(TAG, "Gateway:" IPSTR, IP2STR(&event->ip_info.gw));
        xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
    }
}*/

// Legacy WiFi connection function - replaced by web_config system
/*static void connect_to_wifi(void) {
    wifi_event_group = xEventGroupCreate();
    
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        &instance_got_ip));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = IOT_CONFIG_WIFI_SSID,
            .password = IOT_CONFIG_WIFI_PASSWORD,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
            .pmf_cfg = {
                .capable = true,
                .required = false
            },
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    
    // Disable power saving mode for more reliable connection
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));
    
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "WiFi init finished.");

    // Wait for connection with timeout
    ESP_LOGI(TAG, "Waiting for WiFi connection (timeout: 30 seconds)...");
    EventBits_t bits = xEventGroupWaitBits(wifi_event_group,
            WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
            pdFALSE,
            pdFALSE,
            30000 / portTICK_PERIOD_MS);  // 30 second timeout

    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "[OK] Successfully connected to AP SSID:%s", IOT_CONFIG_WIFI_SSID);
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGE(TAG, "[ERROR] Failed to connect to SSID:%s", IOT_CONFIG_WIFI_SSID);
    } else {
        ESP_LOGE(TAG, "[ERROR] WiFi connection timeout! Check your network settings.");
        ESP_LOGE(TAG, "Troubleshooting steps:");
        ESP_LOGE(TAG, "1. Verify SSID: '%s'", IOT_CONFIG_WIFI_SSID);
        ESP_LOGE(TAG, "2. Check WiFi password is correct");
        ESP_LOGE(TAG, "3. Ensure router is accessible and not blocking new connections");
        ESP_LOGE(TAG, "4. Try moving ESP32 closer to router");
    }
}*/

static void initialize_time(void) {
    ESP_LOGI(TAG, "Initializing SNTP");

    // Set timezone to IST (Indian Standard Time) - UTC+5:30
    setenv("TZ", "IST-5:30", 1);
    tzset();
    ESP_LOGI(TAG, "Timezone set to IST (UTC+5:30)");

    // Check if system time is already valid (from RTC)
    time_t now = time(NULL);
    struct tm timeinfo = { 0 };
    localtime_r(&now, &timeinfo);
    time_t rtc_time = now;  // Save RTC time as fallback

    bool rtc_valid = (timeinfo.tm_year >= (2024 - 1900));
    if (rtc_valid) {
        ESP_LOGI(TAG, "[TIME] RTC has valid time (year %d) - will verify with NTP", timeinfo.tm_year + 1900);
    }

    // Always try NTP sync to ensure correct time (RTC may have drifted or have wrong timezone)
    ESP_LOGI(TAG, "[TIME] Attempting NTP sync for accurate time...");

    // Configure SNTP with multiple servers for redundancy
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_setservername(1, "time.google.com");
    esp_sntp_setservername(2, "time.cloudflare.com");
    esp_sntp_init();

    // Wait for time to be set with retries
    int retry = 0;
    const int retry_count = 10;

    // Reset timeinfo to check for NTP update
    timeinfo.tm_year = 0;

    while (timeinfo.tm_year < (2024 - 1900) && ++retry < retry_count) {
        ESP_LOGI(TAG, "Waiting for NTP sync... (%d/%d)", retry, retry_count);
        vTaskDelay(2000 / portTICK_PERIOD_MS);
        time(&now);
        localtime_r(&now, &timeinfo);
    }

    if (timeinfo.tm_year >= (2024 - 1900)) {
        ESP_LOGI(TAG, "[TIME] ✅ NTP sync successful - year %d", timeinfo.tm_year + 1900);
        last_ntp_sync_time = time(NULL);  // Track sync time for periodic re-sync
    } else if (rtc_valid) {
        // NTP failed but RTC had valid time - restore RTC time
        ESP_LOGW(TAG, "[TIME] ⚠️ NTP sync failed - using RTC time as fallback");
        struct timeval tv = { .tv_sec = rtc_time, .tv_usec = 0 };
        settimeofday(&tv, NULL);
    } else {
        ESP_LOGW(TAG, "[TIME] ⚠️ NTP sync failed and no valid RTC - system time may be incorrect");
    }
    ESP_LOGI(TAG, "Time initialized");
}

// Check and perform periodic NTP re-sync (every 24 hours)
static void check_ntp_resync(void) {
    // Skip if not connected to network
    if (!is_network_connected()) {
        return;
    }

    time_t now = time(NULL);

    // If last sync time not set, use current time (assume synced at boot)
    if (last_ntp_sync_time == 0) {
        last_ntp_sync_time = now;
        return;
    }

    int64_t time_since_sync = now - last_ntp_sync_time;

    // Re-sync if more than 24 hours since last sync
    if (time_since_sync >= NTP_RESYNC_INTERVAL_SEC) {
        ESP_LOGI(TAG, "[NTP] 🔄 Periodic NTP re-sync (last sync %lld hours ago)...",
                 time_since_sync / 3600);

        // Force SNTP to re-sync
        esp_sntp_restart();

        // Wait for sync (max 10 seconds)
        struct tm timeinfo;
        for (int i = 0; i < 10; i++) {
            vTaskDelay(pdMS_TO_TICKS(1000));
            time(&now);
            localtime_r(&now, &timeinfo);
            if (timeinfo.tm_year >= (2024 - 1900)) {
                last_ntp_sync_time = now;
                ESP_LOGI(TAG, "[NTP] ✅ NTP re-sync successful");

                // Update RTC if enabled
                system_config_t* config = get_system_config();
                if (config->rtc_config.enabled && config->rtc_config.update_from_ntp) {
                    ds3231_set_time_tm(&timeinfo);
                    ESP_LOGI(TAG, "[NTP] ✅ RTC updated from NTP");
                }
                return;
            }
        }

        ESP_LOGW(TAG, "[NTP] ⚠️ NTP re-sync timeout - will retry later");
        // Update sync time anyway to prevent repeated attempts
        last_ntp_sync_time = now;
    }
}

// URL encode function
static void url_encode(const char* input, char* output, size_t output_size) {
    static const char hex[] = "0123456789ABCDEF";
    size_t input_len = strlen(input);
    size_t output_len = 0;
    
    for (size_t i = 0; i < input_len && output_len < output_size - 1; i++) {
        char c = input[i];
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || 
            (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') {
            output[output_len++] = c;
        } else {
            if (output_len < output_size - 3) {
                output[output_len++] = '%';
                output[output_len++] = hex[(c >> 4) & 0xF];
                output[output_len++] = hex[c & 0xF];
            }
        }
    }
    output[output_len] = '\0';
}

// Generate Azure IoT Hub SAS Token
static int generate_sas_token(char* token, size_t token_size, uint32_t expiry_seconds) {
    time_t now = time(NULL);
    uint32_t expiry = now + expiry_seconds;
    
    // Create the resource URI (no URL encoding needed here)
    char resource_uri[256];
    char string_to_sign[512];
    char encoded_uri[256];
    
    // Get dynamic configuration
    system_config_t* config = get_system_config();
    ESP_LOGI(TAG, "[DYNAMIC] Using Azure Device ID: %s", config->azure_device_id);
    ESP_LOGI(TAG, "[DYNAMIC] Using Azure Device Key (first 10 chars): %.10s...", config->azure_device_key);
    
    // Resource URI format for Azure IoT Hub
    snprintf(resource_uri, sizeof(resource_uri), "%s/devices/%s", 
             IOT_CONFIG_IOTHUB_FQDN, config->azure_device_id);
    
    // URL encode the resource URI
    url_encode(resource_uri, encoded_uri, sizeof(encoded_uri));
    
    // Create the complete string to sign: encoded_uri + "\n" + expiry
    snprintf(string_to_sign, sizeof(string_to_sign), "%s\n%" PRIu32, encoded_uri, expiry);
    
    ESP_LOGI(TAG, "Resource URI: %s", resource_uri);
    ESP_LOGI(TAG, "Encoded URI: %s", encoded_uri);
    ESP_LOGI(TAG, "Expiry: %" PRIu32, expiry);
    ESP_LOGI(TAG, "String to sign: %s", string_to_sign);
    
    // Decode the device key (base64)
    unsigned char decoded_key[64];
    size_t decoded_key_len;
    
    ESP_LOGI(TAG, "Device key length: %d", strlen(config->azure_device_key));
    
    int ret = mbedtls_base64_decode(decoded_key, sizeof(decoded_key), &decoded_key_len,
                                   (const unsigned char*)config->azure_device_key, 
                                   strlen(config->azure_device_key));
    if (ret != 0) {
        ESP_LOGE(TAG, "Failed to decode device key: %d (key: %.20s...)", ret, config->azure_device_key);
        return -1;
    }
    
    ESP_LOGI(TAG, "Decoded key length: %d", decoded_key_len);
    
    // Generate HMAC-SHA256 signature
    unsigned char signature[32];
    
    mbedtls_md_context_t ctx;
    const mbedtls_md_info_t *info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    
    mbedtls_md_init(&ctx);
    ret = mbedtls_md_setup(&ctx, info, 1);
    if (ret != 0) {
        ESP_LOGE(TAG, "Failed to setup HMAC: %d", ret);
        mbedtls_md_free(&ctx);
        return -1;
    }
    
    ret = mbedtls_md_hmac_starts(&ctx, decoded_key, decoded_key_len);
    if (ret != 0) {
        ESP_LOGE(TAG, "Failed to start HMAC: %d", ret);
        mbedtls_md_free(&ctx);
        return -1;
    }
    
    ret = mbedtls_md_hmac_update(&ctx, (const unsigned char*)string_to_sign, strlen(string_to_sign));
    if (ret != 0) {
        ESP_LOGE(TAG, "Failed to update HMAC: %d", ret);
        mbedtls_md_free(&ctx);
        return -1;
    }
    
    ret = mbedtls_md_hmac_finish(&ctx, signature);
    mbedtls_md_free(&ctx);
    
    if (ret != 0) {
        ESP_LOGE(TAG, "Failed to finish HMAC: %d", ret);
        return -1;
    }
    
    // Base64 encode the signature
    char encoded_signature[128];
    size_t encoded_len;
    
    ret = mbedtls_base64_encode((unsigned char*)encoded_signature, sizeof(encoded_signature), &encoded_len,
                               signature, sizeof(signature));
    if (ret != 0) {
        ESP_LOGE(TAG, "Failed to encode signature: %d", ret);
        return -1;
    }
    
    // URL encode the signature
    char url_encoded_signature[256];
    url_encode(encoded_signature, url_encoded_signature, sizeof(url_encoded_signature));
    
    // Create the SAS token
    snprintf(token, token_size,
             "SharedAccessSignature sr=%s&sig=%s&se=%" PRIu32,
             encoded_uri, url_encoded_signature, expiry);

    // Track token generation time for auto-refresh
    sas_token_generated_time = now;
    sas_token_expiry_seconds = expiry_seconds;

    ESP_LOGI(TAG, "Generated SAS token: %.100s...", token);
    ESP_LOGI(TAG, "[SAS] Token valid for %lu seconds (expires at %lu)", expiry_seconds, expiry);
    return 0;
}

// Check if SAS token needs refresh (returns true if token will expire soon)
static bool sas_token_needs_refresh(void) {
    if (sas_token_generated_time == 0) {
        return false;  // Token not generated yet
    }

    time_t now = time(NULL);
    int64_t token_age = now - sas_token_generated_time;
    int64_t time_until_expiry = sas_token_expiry_seconds - token_age;

    // Refresh if less than 5 minutes until expiry
    if (time_until_expiry <= SAS_TOKEN_REFRESH_MARGIN_SEC) {
        ESP_LOGW(TAG, "[SAS] Token expires in %lld seconds - refresh needed", time_until_expiry);
        return true;
    }

    return false;
}

// Refresh SAS token and reconnect MQTT client
static esp_err_t refresh_sas_token_and_reconnect(void) {
    ESP_LOGI(TAG, "[SAS] 🔄 Refreshing SAS token...");

    // Generate new SAS token (valid for 1 hour)
    if (generate_sas_token(sas_token, sizeof(sas_token), 3600) != 0) {
        ESP_LOGE(TAG, "[SAS] ❌ Failed to generate new SAS token");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "[SAS] ✅ New SAS token generated");

    // Stop existing MQTT client
    if (mqtt_client != NULL) {
        ESP_LOGI(TAG, "[SAS] Stopping existing MQTT client...");
        esp_mqtt_client_stop(mqtt_client);
        vTaskDelay(pdMS_TO_TICKS(1000));
        esp_mqtt_client_destroy(mqtt_client);
        mqtt_client = NULL;
        mqtt_connected = false;
    }

    // Reinitialize MQTT client with new token
    ESP_LOGI(TAG, "[SAS] Reinitializing MQTT client with new token...");

    system_config_t* config = get_system_config();

    // Recreate MQTT username (same format as initialize_mqtt_client)
    snprintf(mqtt_username, sizeof(mqtt_username), "%s/%s/?api-version=2018-06-30",
             IOT_CONFIG_IOTHUB_FQDN, config->azure_device_id);

    esp_mqtt_client_config_t mqtt_config = {
        .broker.address.uri = mqtt_broker_uri,
        .broker.address.port = 8883,
        .credentials.client_id = config->azure_device_id,
        .credentials.username = mqtt_username,
        .credentials.authentication.password = sas_token,
        .session.keepalive = 30,
        .session.disable_clean_session = 0,
        .session.protocol_ver = MQTT_PROTOCOL_V_3_1_1,
        .network.disable_auto_reconnect = false,
        .broker.verification.crt_bundle_attach = esp_crt_bundle_attach,
    };

    mqtt_client = esp_mqtt_client_init(&mqtt_config);
    if (mqtt_client == NULL) {
        ESP_LOGE(TAG, "[SAS] ❌ Failed to reinitialize MQTT client");
        return ESP_FAIL;
    }

    ESP_ERROR_CHECK(esp_mqtt_client_register_event(mqtt_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL));

    esp_err_t start_result = esp_mqtt_client_start(mqtt_client);
    if (start_result != ESP_OK) {
        ESP_LOGE(TAG, "[SAS] ❌ Failed to start MQTT client: %s", esp_err_to_name(start_result));
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "[SAS] ✅ MQTT client restarted with new SAS token");
    ESP_LOGI(TAG, "[SAS] Waiting for MQTT reconnection...");

    // Wait for connection (max 10 seconds)
    for (int i = 0; i < 10; i++) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        if (mqtt_connected) {
            ESP_LOGI(TAG, "[SAS] ✅ MQTT reconnected successfully after token refresh!");
            return ESP_OK;
        }
    }

    ESP_LOGW(TAG, "[SAS] ⚠️ MQTT not reconnected yet - will continue trying in background");
    return ESP_OK;  // MQTT client will auto-reconnect
}

// Callback function for replaying cached SD card messages to MQTT
static void replay_message_callback(const pending_message_t* msg) {
    if (!msg || !mqtt_client) {
        ESP_LOGE(TAG, "[SD] Invalid message or MQTT client not initialized");
        return;
    }

    if (!mqtt_connected) {
        ESP_LOGW(TAG, "[SD] Cannot replay message %lu - MQTT not connected", msg->message_id);
        return;
    }

    ESP_LOGI(TAG, "[SD] 📤 Replaying cached message ID %lu", msg->message_id);
    ESP_LOGI(TAG, "[SD]    Topic: %s", msg->topic);
    ESP_LOGI(TAG, "[SD]    Timestamp: %s", msg->timestamp);
    ESP_LOGI(TAG, "[SD]    Payload: %.100s%s", msg->payload, strlen(msg->payload) > 100 ? "..." : "");

    int msg_id = esp_mqtt_client_publish(mqtt_client, msg->topic, msg->payload, 0, 1, 0);
    if (msg_id == -1) {
        ESP_LOGE(TAG, "[SD] ❌ Failed to publish replayed message %lu", msg->message_id);
    } else {
        ESP_LOGI(TAG, "[SD] ✅ Successfully published replayed message %lu (MQTT msg_id: %d)",
                 msg->message_id, msg_id);

        // Remove the message from SD card after successful publish
        esp_err_t ret = sd_card_remove_message(msg->message_id);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "[SD] Failed to remove replayed message %lu from SD card", msg->message_id);
        }
    }

    // Small delay between replayed messages to avoid overwhelming MQTT broker
    vTaskDelay(pdMS_TO_TICKS(100));
}

// Log heartbeat to SD card for post-mortem debugging
static void log_heartbeat_to_sd(void) {
    system_config_t* config = get_system_config();
    if (!config->sd_config.enabled) {
        return;
    }

    int64_t current_time = esp_timer_get_time() / 1000000;

    // Create heartbeat JSON
    char heartbeat_json[512];
    time_t now = time(NULL);
    struct tm timeinfo;
    gmtime_r(&now, &timeinfo);
    char time_str[32];
    strftime(time_str, sizeof(time_str), "%Y-%m-%dT%H:%M:%SZ", &timeinfo);

    snprintf(heartbeat_json, sizeof(heartbeat_json),
        "{\"type\":\"heartbeat\",\"timestamp\":\"%s\","
        "\"uptime_sec\":%lld,\"free_heap\":%lu,\"min_heap\":%lu,"
        "\"mqtt_connected\":%s,\"telemetry_sent\":%lu,"
        "\"mqtt_reconnects\":%lu,\"telemetry_failures\":%lu,"
        "\"restart_count\":%lu}",
        time_str,
        (long long)(current_time - system_uptime_start),
        esp_get_free_heap_size(),
        esp_get_minimum_free_heap_size(),
        mqtt_connected ? "true" : "false",
        total_telemetry_sent,
        mqtt_reconnect_count,
        telemetry_failure_count,
        system_restart_count);

    // Write to SD card heartbeat log file
    FILE* f = fopen("/sdcard/heartbeat.log", "a");
    if (f) {
        fprintf(f, "%s\n", heartbeat_json);
        fclose(f);
        ESP_LOGI(TAG, "[HEARTBEAT] Logged to SD card: uptime=%llds, heap=%lu",
                 (long long)(current_time - system_uptime_start), esp_get_free_heap_size());
    } else {
        ESP_LOGW(TAG, "[HEARTBEAT] Failed to write to SD card");
    }
}

// Report device status to Azure IoT Hub Device Twin
static void report_device_twin(void) {
    if (!mqtt_connected || mqtt_client == NULL) {
        return;
    }

    system_config_t* config = get_system_config();
    int64_t current_time = esp_timer_get_time() / 1000000;
    int64_t uptime = current_time - system_uptime_start;

    // Get OTA status for Device Twin
    ota_info_t* ota_info = ota_get_info();

    // Create Device Twin reported properties JSON with OTA status
    char twin_json[1536];
    snprintf(twin_json, sizeof(twin_json),
        "{\"deviceId\":\"%s\","
        "\"firmwareVersion\":\"%s\","
        "\"uptimeSeconds\":%lld,"
        "\"freeHeapBytes\":%lu,"
        "\"minFreeHeapBytes\":%lu,"
        "\"mqttReconnectCount\":%lu,"
        "\"telemetrySentCount\":%lu,"
        "\"telemetryFailureCount\":%lu,"
        "\"systemRestartCount\":%lu,"
        "\"networkMode\":\"%s\","
        "\"sdCardEnabled\":%s,"
        "\"sensorCount\":%d,"
        "\"ota\":{"
        "\"status\":\"%s\","
        "\"currentVersion\":\"%s\","
        "\"newVersion\":\"%s\","
        "\"progress\":%d,"
        "\"bytesDownloaded\":%lu,"
        "\"totalBytes\":%lu,"
        "\"isRollback\":%s,"
        "\"bootCount\":%d,"
        "\"errorMsg\":\"%s\"}}",
        config->azure_device_id,
        FW_VERSION_STRING,
        (long long)uptime,
        esp_get_free_heap_size(),
        esp_get_minimum_free_heap_size(),
        mqtt_reconnect_count,
        total_telemetry_sent,
        telemetry_failure_count,
        system_restart_count,
        config->network_mode == NETWORK_MODE_WIFI ? "WiFi" : "SIM",
        config->sd_config.enabled ? "true" : "false",
        config->sensor_count,
        ota_status_to_string(ota_info->status),
        ota_info->current_version,
        ota_info->new_version,
        ota_info->progress,
        ota_info->bytes_downloaded,
        ota_info->total_bytes,
        ota_info->is_rollback ? "true" : "false",
        ota_info->boot_count,
        ota_info->error_msg);

    // Publish to Device Twin reported properties topic
    // Azure IoT Hub Device Twin topic format: $iothub/twin/PATCH/properties/reported/?$rid=<request_id>
    char twin_topic[128];
    static uint32_t twin_request_id = 0;
    snprintf(twin_topic, sizeof(twin_topic), "$iothub/twin/PATCH/properties/reported/?$rid=%lu", ++twin_request_id);

    int msg_id = esp_mqtt_client_publish(mqtt_client, twin_topic, twin_json, 0, 1, 0);
    if (msg_id >= 0) {
        ESP_LOGI(TAG, "[TWIN] Reported device status to Azure IoT Hub");
    } else {
        ESP_LOGW(TAG, "[TWIN] Failed to report device status");
    }
}

// Check for telemetry timeout and force restart if needed
static void check_telemetry_timeout_recovery(void) {
    int64_t current_time = esp_timer_get_time() / 1000000;

    // Only check after initial startup period (5 minutes)
    if (current_time - system_uptime_start < 300) {
        return;
    }

    // If we've never had a successful telemetry, use system start time
    if (last_successful_telemetry_time == 0) {
        last_successful_telemetry_time = system_uptime_start;
    }

    int64_t time_since_last_success = current_time - last_successful_telemetry_time;

    if (time_since_last_success > TELEMETRY_TIMEOUT_SEC) {
        ESP_LOGE(TAG, "[RECOVERY] No successful telemetry for %lld seconds (limit: %d)",
                 (long long)time_since_last_success, TELEMETRY_TIMEOUT_SEC);
        ESP_LOGE(TAG, "[RECOVERY] Forcing system restart to recover...");

        // Log to SD before restart
        log_heartbeat_to_sd();

        // Increment restart count in NVS
        nvs_handle_t nvs;
        if (nvs_open("recovery", NVS_READWRITE, &nvs) == ESP_OK) {
            system_restart_count++;
            nvs_set_u32(nvs, "restart_cnt", system_restart_count);
            nvs_commit(nvs);
            nvs_close(nvs);
        }

        vTaskDelay(pdMS_TO_TICKS(1000));
        esp_restart();
    }
}

// Load restart count from NVS
static void load_restart_count(void) {
    nvs_handle_t nvs;
    if (nvs_open("recovery", NVS_READONLY, &nvs) == ESP_OK) {
        nvs_get_u32(nvs, "restart_cnt", &system_restart_count);
        nvs_close(nvs);
        if (system_restart_count > 0) {
            ESP_LOGW(TAG, "[RECOVERY] System has restarted %lu times due to recovery", system_restart_count);
        }
    }
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data) {
    esp_mqtt_event_handle_t event = event_data;
    switch ((esp_mqtt_event_id_t)event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "[OK] MQTT_EVENT_CONNECTED - Azure IoT Hub connection established!");
            mqtt_connected = true;
            mqtt_connect_time = esp_timer_get_time() / 1000000;  // Record connection time in seconds
            mqtt_reconnect_count = 0; // Reset reconnect counter on successful connection

            // Subscribe to cloud-to-device messages after connection
            system_config_t* config = get_system_config();
            snprintf(c2d_topic, sizeof(c2d_topic), "devices/%s/messages/devicebound/#", config->azure_device_id);
            esp_mqtt_client_subscribe(mqtt_client, c2d_topic, 1);
            ESP_LOGI(TAG, "[MAIL] Subscribed to C2D messages: %s", c2d_topic);
            
            ESP_LOGI(TAG, "[OK] MQTT connected successfully");
            break;
            
        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGW(TAG, "[WARN] MQTT_EVENT_DISCONNECTED");
            mqtt_connected = false;
            mqtt_reconnect_count++;

            // Check if network recovery is needed
            {
                system_config_t* disconnect_config = get_system_config();
                bool need_network_recovery = false;

                if (disconnect_config->network_mode == NETWORK_MODE_SIM) {
                    // SIM mode: Check if PPP connection is down
                    if (!a7670c_ppp_is_connected()) {
                        ESP_LOGW(TAG, "[SIM] 📱 PPP connection lost - will trigger recovery");
                        need_network_recovery = true;
                    }
                } else if (modem_reset_enabled) {
                    // WiFi mode: Use modem reset setting
                    need_network_recovery = true;
                }

                // Trigger network recovery if needed and not already running
                if (need_network_recovery && modem_reset_task_handle == NULL) {
                    // Check cooldown to prevent rapid reset cycling
                    time_t now = time(NULL);
                    int64_t time_since_last_reset = now - last_modem_reset_time;

                    if (last_modem_reset_time == 0 || time_since_last_reset >= MODEM_RESET_COOLDOWN_SEC) {
                        ESP_LOGI(TAG, "[NET] Network disconnected, triggering recovery...");

                        // Create modem reset task (handles both WiFi and SIM modes)
                        BaseType_t result = xTaskCreate(
                            modem_reset_task,
                            "modem_reset",
                            4096,  // Increased stack for SIM mode operations
                            NULL,
                            2,  // Low priority
                            &modem_reset_task_handle
                        );

                        if (result != pdPASS) {
                            ESP_LOGE(TAG, "[NET] Failed to create network recovery task");
                            modem_reset_task_handle = NULL;
                        } else {
                            last_modem_reset_time = now;  // Update last reset time
                        }
                    } else {
                        int64_t remaining = MODEM_RESET_COOLDOWN_SEC - time_since_last_reset;
                        ESP_LOGW(TAG, "[NET] Modem reset cooldown active - %lld seconds remaining", (long long)remaining);
                        ESP_LOGI(TAG, "[NET] System will cache to SD card and retry later");
                    }
                }
            }

            // Check if we've exceeded maximum reconnection attempts
            if (mqtt_reconnect_count >= MAX_MQTT_RECONNECT_ATTEMPTS) {
                ESP_LOGE(TAG, "[ERROR] Exceeded maximum MQTT reconnection attempts (%d)", MAX_MQTT_RECONNECT_ATTEMPTS);
                if (SYSTEM_RESTART_ON_CRITICAL_ERROR) {
                    ESP_LOGE(TAG, "[PROC] Restarting system due to persistent MQTT connection issues...");
                    esp_restart();
                }
            }
            break;
            
        case MQTT_EVENT_SUBSCRIBED:
            ESP_LOGI(TAG, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
            break;
            
        case MQTT_EVENT_UNSUBSCRIBED:
            ESP_LOGI(TAG, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
            break;
            
        case MQTT_EVENT_PUBLISHED:
            ESP_LOGI(TAG, "[OK] TELEMETRY PUBLISHED SUCCESSFULLY! msg_id=%d", event->msg_id);
            total_telemetry_sent++;
            last_telemetry_time = esp_timer_get_time() / 1000000;  // Record last telemetry time in seconds
            break;
            
        case MQTT_EVENT_DATA:
            ESP_LOGI(TAG, "[MSG] CLOUD-TO-DEVICE MESSAGE RECEIVED:");
            printf("TOPIC=%.*s\r\n", event->topic_len, event->topic);
            printf("DATA=%.*s\r\n", event->data_len, event->data);

            // Process C2D command
            if (event->data_len > 0 && event->data_len < 1024) {
                char *message = (char*)malloc(event->data_len + 1);
                if (message) {
                    memcpy(message, event->data, event->data_len);
                    message[event->data_len] = '\0';

                    ESP_LOGI(TAG, "[C2D] Processing command: %s", message);

                    // Find JSON start - Azure may add timestamp prefix like "11/21/2025, 3:08:57 PM - {"
                    char *json_start = strchr(message, '{');
                    if (json_start == NULL) {
                        ESP_LOGW(TAG, "[C2D] No JSON object found in message");
                        free(message);
                        break;
                    }

                    ESP_LOGI(TAG, "[C2D] JSON payload: %s", json_start);

                    // Parse JSON command
                    cJSON *root = cJSON_Parse(json_start);
                    if (root) {
                        cJSON *command = cJSON_GetObjectItem(root, "command");

                        if (command && cJSON_IsString(command)) {
                            const char *cmd = command->valuestring;
                            ESP_LOGI(TAG, "[C2D] Command: %s", cmd);

                            // Handle different commands
                            if (strcmp(cmd, "restart") == 0) {
                                ESP_LOGW(TAG, "[C2D] Restart command received - restarting in 3 seconds...");
                                vTaskDelay(pdMS_TO_TICKS(3000));
                                esp_restart();
                            }
                            else if (strcmp(cmd, "set_telemetry_interval") == 0) {
                                cJSON *interval = cJSON_GetObjectItem(root, "interval");
                                if (interval && cJSON_IsNumber(interval)) {
                                    int new_interval = interval->valueint;
                                    if (new_interval >= 30 && new_interval <= 3600) {
                                        system_config_t *cfg = get_system_config();
                                        cfg->telemetry_interval = new_interval;
                                        config_save_to_nvs(cfg);
                                        ESP_LOGI(TAG, "[C2D] Telemetry interval updated to %d seconds", new_interval);
                                    } else {
                                        ESP_LOGW(TAG, "[C2D] Invalid interval: %d (must be 30-3600)", new_interval);
                                    }
                                }
                            }
                            else if (strcmp(cmd, "get_status") == 0) {
                                ESP_LOGI(TAG, "[C2D] Status request - sending telemetry now");
                                send_telemetry();
                            }
                            else if (strcmp(cmd, "toggle_webserver") == 0) {
                                ESP_LOGI(TAG, "[C2D] Toggling web server");
                                web_server_running = !web_server_running;
                                if (web_server_running) {
                                    web_config_start_ap_mode();
                                } else {
                                    web_config_stop();
                                }
                            }
                            else if (strcmp(cmd, "add_sensor") == 0) {
                                system_config_t *cfg = get_system_config();
                                if (cfg->sensor_count < 20) {
                                    cJSON *sensor = cJSON_GetObjectItem(root, "sensor");
                                    if (sensor) {
                                        int idx = cfg->sensor_count;

                                        // Parse sensor configuration from JSON
                                        cJSON *item;
                                        if ((item = cJSON_GetObjectItem(sensor, "name")))
                                            strncpy(cfg->sensors[idx].name, item->valuestring, 31);
                                        if ((item = cJSON_GetObjectItem(sensor, "unit_id")))
                                            strncpy(cfg->sensors[idx].unit_id, item->valuestring, 15);
                                        if ((item = cJSON_GetObjectItem(sensor, "slave_id")))
                                            cfg->sensors[idx].slave_id = item->valueint;
                                        if ((item = cJSON_GetObjectItem(sensor, "register_address")))
                                            cfg->sensors[idx].register_address = item->valueint;
                                        if ((item = cJSON_GetObjectItem(sensor, "quantity")))
                                            cfg->sensors[idx].quantity = item->valueint;
                                        if ((item = cJSON_GetObjectItem(sensor, "data_type")))
                                            strncpy(cfg->sensors[idx].data_type, item->valuestring, 31);
                                        if ((item = cJSON_GetObjectItem(sensor, "sensor_type")))
                                            strncpy(cfg->sensors[idx].sensor_type, item->valuestring, 15);
                                        if ((item = cJSON_GetObjectItem(sensor, "scale_factor")))
                                            cfg->sensors[idx].scale_factor = (float)item->valuedouble;
                                        if ((item = cJSON_GetObjectItem(sensor, "baud_rate")))
                                            cfg->sensors[idx].baud_rate = item->valueint;

                                        cfg->sensors[idx].enabled = true;
                                        strncpy(cfg->sensors[idx].register_type, "HOLDING", 15);
                                        cfg->sensor_count++;
                                        config_save_to_nvs(cfg);

                                        ESP_LOGI(TAG, "[C2D] Sensor added: %s (total: %d)", cfg->sensors[idx].name, cfg->sensor_count);
                                    }
                                } else {
                                    ESP_LOGW(TAG, "[C2D] Cannot add sensor - limit reached (20 max)");
                                }
                            }
                            else if (strcmp(cmd, "update_sensor") == 0) {
                                cJSON *sensor_idx = cJSON_GetObjectItem(root, "index");
                                if (sensor_idx && cJSON_IsNumber(sensor_idx)) {
                                    int idx = sensor_idx->valueint;
                                    system_config_t *cfg = get_system_config();

                                    if (idx >= 0 && idx < cfg->sensor_count) {
                                        cJSON *updates = cJSON_GetObjectItem(root, "updates");
                                        if (updates) {
                                            cJSON *item;
                                            if ((item = cJSON_GetObjectItem(updates, "enabled")))
                                                cfg->sensors[idx].enabled = cJSON_IsTrue(item);
                                            if ((item = cJSON_GetObjectItem(updates, "name")))
                                                strncpy(cfg->sensors[idx].name, item->valuestring, 31);
                                            if ((item = cJSON_GetObjectItem(updates, "scale_factor")))
                                                cfg->sensors[idx].scale_factor = (float)item->valuedouble;
                                            if ((item = cJSON_GetObjectItem(updates, "baud_rate")))
                                                cfg->sensors[idx].baud_rate = item->valueint;

                                            config_save_to_nvs(cfg);
                                            ESP_LOGI(TAG, "[C2D] Sensor %d updated: %s", idx, cfg->sensors[idx].name);
                                        }
                                    } else {
                                        ESP_LOGW(TAG, "[C2D] Invalid sensor index: %d", idx);
                                    }
                                }
                            }
                            else if (strcmp(cmd, "delete_sensor") == 0) {
                                cJSON *sensor_idx = cJSON_GetObjectItem(root, "index");
                                if (sensor_idx && cJSON_IsNumber(sensor_idx)) {
                                    int idx = sensor_idx->valueint;
                                    system_config_t *cfg = get_system_config();

                                    if (idx >= 0 && idx < cfg->sensor_count) {
                                        // Shift all sensors after this one
                                        for (int i = idx; i < cfg->sensor_count - 1; i++) {
                                            memcpy(&cfg->sensors[i], &cfg->sensors[i + 1], sizeof(sensor_config_t));
                                        }
                                        cfg->sensor_count--;
                                        config_save_to_nvs(cfg);
                                        ESP_LOGI(TAG, "[C2D] Sensor %d deleted (remaining: %d)", idx, cfg->sensor_count);
                                    } else {
                                        ESP_LOGW(TAG, "[C2D] Invalid sensor index: %d", idx);
                                    }
                                }
                            }
                            // OTA (Over-The-Air) Update Commands
                            else if (strcmp(cmd, "ota_update") == 0) {
                                cJSON *url = cJSON_GetObjectItem(root, "url");
                                cJSON *version = cJSON_GetObjectItem(root, "version");

                                if (url && cJSON_IsString(url)) {
                                    const char *fw_url = url->valuestring;
                                    const char *fw_version = (version && cJSON_IsString(version)) ? version->valuestring : "unknown";

                                    ESP_LOGI(TAG, "[C2D] OTA update requested: %s (v%s)", fw_url, fw_version);

                                    esp_err_t ret = ota_start_update(fw_url, fw_version);
                                    if (ret == ESP_OK) {
                                        ESP_LOGI(TAG, "[C2D] OTA update started successfully");
                                    } else {
                                        ESP_LOGE(TAG, "[C2D] OTA update failed to start: %s", esp_err_to_name(ret));
                                    }
                                } else {
                                    ESP_LOGW(TAG, "[C2D] OTA update requires 'url' parameter");
                                }
                            }
                            else if (strcmp(cmd, "ota_status") == 0) {
                                ota_info_t *info = ota_get_info();
                                ESP_LOGI(TAG, "[C2D] OTA Status: %s", ota_status_to_string(info->status));
                                ESP_LOGI(TAG, "  Current version: %s", info->current_version);
                                ESP_LOGI(TAG, "  Progress: %d%% (%lu/%lu bytes)",
                                         info->progress, info->bytes_downloaded, info->total_bytes);
                                if (info->is_rollback) {
                                    ESP_LOGW(TAG, "  Running after ROLLBACK!");
                                }
                                if (info->error_msg[0] != '\0') {
                                    ESP_LOGE(TAG, "  Error: %s", info->error_msg);
                                }
                            }
                            else if (strcmp(cmd, "ota_cancel") == 0) {
                                esp_err_t ret = ota_cancel_update();
                                if (ret == ESP_OK) {
                                    ESP_LOGI(TAG, "[C2D] OTA update cancelled");
                                } else {
                                    ESP_LOGW(TAG, "[C2D] No OTA update in progress to cancel");
                                }
                            }
                            else if (strcmp(cmd, "ota_confirm") == 0) {
                                ota_mark_valid();
                                ESP_LOGI(TAG, "[C2D] Current firmware marked as valid (rollback disabled)");
                            }
                            else if (strcmp(cmd, "ota_reboot") == 0) {
                                ota_info_t *info = ota_get_info();
                                if (info->status == OTA_STATUS_PENDING_REBOOT) {
                                    ESP_LOGI(TAG, "[C2D] Rebooting to apply OTA update...");
                                    vTaskDelay(pdMS_TO_TICKS(1000));
                                    ota_reboot();
                                } else {
                                    ESP_LOGW(TAG, "[C2D] No pending OTA update to apply (status: %s)",
                                             ota_status_to_string(info->status));
                                }
                            }
                            else {
                                ESP_LOGW(TAG, "[C2D] Unknown command: %s", cmd);
                            }
                        }

                        cJSON_Delete(root);
                    } else {
                        ESP_LOGW(TAG, "[C2D] Failed to parse JSON command");
                    }

                    free(message);
                }
            }
            break;
            
        case MQTT_EVENT_ERROR:
            ESP_LOGE(TAG, "[ERROR] MQTT_EVENT_ERROR");
            mqtt_connected = false;
            if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
                ESP_LOGE(TAG, "TCP transport error: %d", event->error_handle->esp_transport_sock_errno);
                ESP_LOGE(TAG, "Possible causes: Network connectivity, firewall, DNS");
            } else if (event->error_handle->error_type == MQTT_ERROR_TYPE_CONNECTION_REFUSED) {
                ESP_LOGE(TAG, "Connection refused: %d", event->error_handle->connect_return_code);
                ESP_LOGE(TAG, "Possible causes: Invalid SAS token, wrong device ID, IoT Hub settings");
                
                // If connection refused, might be SAS token expiry
                if (event->error_handle->connect_return_code == 5) {
                    ESP_LOGE(TAG, "Authentication failed - possibly expired SAS token");
                }
            }
            break;
            
        default:
            ESP_LOGI(TAG, "Other event id:%" PRId32, event_id);
            break;
    }
}

// Function to test DNS resolution
static esp_err_t test_dns_resolution(const char* hostname) {
    ESP_LOGI(TAG, "[FIND] Testing DNS resolution for: %s", hostname);
    
    struct addrinfo hints = {0};
    struct addrinfo *result = NULL;
    
    hints.ai_family = AF_INET; // IPv4
    hints.ai_socktype = SOCK_STREAM;
    
    int ret = getaddrinfo(hostname, "443", &hints, &result);
    if (ret != 0) {
        ESP_LOGE(TAG, "[ERROR] DNS resolution failed for %s: getaddrinfo() returned %d", hostname, ret);
        ESP_LOGE(TAG, "   Error details: %d", ret);
        return ESP_FAIL;
    }
    
    if (result) {
        struct sockaddr_in* addr_in = (struct sockaddr_in*)result->ai_addr;
        ESP_LOGI(TAG, "[OK] DNS resolved %s to: %s", hostname, inet_ntoa(addr_in->sin_addr));
        freeaddrinfo(result);
        return ESP_OK;
    }
    
    ESP_LOGE(TAG, "[ERROR] DNS resolution returned no results for %s", hostname);
    return ESP_FAIL;
}

// Function to test basic internet connectivity
static esp_err_t test_internet_connectivity(void) {
    ESP_LOGI(TAG, "[NET] Testing internet connectivity...");
    
    // Test multiple DNS servers
    const char* dns_servers[] = {
        "8.8.8.8",           // Google DNS
        "1.1.1.1",           // Cloudflare DNS
        "208.67.222.222"     // OpenDNS
    };
    
    bool any_dns_works = false;
    for (int i = 0; i < 3; i++) {
        if (test_dns_resolution(dns_servers[i]) == ESP_OK) {
            any_dns_works = true;
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(1000)); // Wait between tests
    }
    
    if (!any_dns_works) {
        ESP_LOGE(TAG, "[ERROR] No DNS servers are reachable - internet connectivity issue");
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "[OK] Basic internet connectivity confirmed");
    return ESP_OK;
}

// Function to troubleshoot Azure IoT Hub connectivity
static void troubleshoot_azure_connectivity(void) {
    ESP_LOGI(TAG, "[CONFIG] Azure IoT Hub connectivity troubleshooting...");
    system_config_t* config = get_system_config();
    ESP_LOGI(TAG, "Hub FQDN: %s", IOT_CONFIG_IOTHUB_FQDN);
    ESP_LOGI(TAG, "Device ID: %s", config->azure_device_id);
    
    // Check if the hostname format is correct
    if (!strstr(IOT_CONFIG_IOTHUB_FQDN, ".azure-devices.net")) {
        ESP_LOGE(TAG, "[WARN] WARNING: Hostname doesn't end with .azure-devices.net");
        ESP_LOGE(TAG, "   Expected format: <hub-name>.azure-devices.net");
    }
    
    // Try to resolve microsoft.com as a test
    ESP_LOGI(TAG, "[FIND] Testing Microsoft domain resolution...");
    if (test_dns_resolution("microsoft.com") == ESP_OK) {
        ESP_LOGI(TAG, "[OK] Microsoft domains are reachable");
        ESP_LOGI(TAG, "[TIP] Issue is likely with specific IoT Hub hostname");
    } else {
        ESP_LOGE(TAG, "[ERROR] Microsoft domains not reachable - possible firewall/DNS filtering");
    }
    
    // Test Azure IoT Hub service endpoint
    ESP_LOGI(TAG, "[FIND] Testing Azure service endpoint...");
    if (test_dns_resolution("azure-devices.net") == ESP_OK) {
        ESP_LOGI(TAG, "[OK] Azure IoT service is reachable");
        ESP_LOGI(TAG, "[TIP] Issue is likely with specific hub name: %s", IOT_CONFIG_IOTHUB_FQDN);
    } else {
        ESP_LOGE(TAG, "[ERROR] Azure IoT service not reachable - check firewall/DNS");
    }
}

// Helper function to check if time is synchronized
static bool is_time_synced(void) {
    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);
    // Time is synced if year is 2020 or later
    return (timeinfo.tm_year >= (2020 - 1900));
}

static int initialize_mqtt_client(void) {
    ESP_LOGI(TAG, "[LINK] Initializing MQTT client on core %d", xPortGetCoreID());

    system_config_t* config = get_system_config();

    // Wait for time synchronization (required for TLS certificate validation)
    // TLS will fail with CERT_VERIFY_FAILED if system time is 1970
    ESP_LOGI(TAG, "[TIME] Checking time synchronization for TLS...");
    int time_wait_count = 0;
    const int max_time_wait = 30;  // Wait up to 30 seconds for time sync

    while (!is_time_synced() && time_wait_count < max_time_wait) {
        if (time_wait_count == 0) {
            ESP_LOGW(TAG, "[TIME] System time not synced (shows 1970) - waiting for NTP/RTC...");
            ESP_LOGW(TAG, "[TIME] TLS certificate verification requires valid system time");
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
        time_wait_count++;

        if (time_wait_count % 10 == 0) {
            ESP_LOGI(TAG, "[TIME] Still waiting for time sync... (%d/%d)", time_wait_count, max_time_wait);
        }
    }

    if (!is_time_synced()) {
        ESP_LOGE(TAG, "[TIME] Time synchronization failed after %d seconds", max_time_wait);
        ESP_LOGE(TAG, "[TIME] TLS certificate verification will fail - check network/NTP/RTC");
        ESP_LOGE(TAG, "[TIME] Possible causes:");
        ESP_LOGE(TAG, "[TIME]   1. No network connection (NTP unreachable)");
        ESP_LOGE(TAG, "[TIME]   2. DS3231 RTC not connected or not configured");
        ESP_LOGE(TAG, "[TIME]   3. Firewall blocking NTP (port 123 UDP)");
        // Continue anyway - will fail with better error message at TLS handshake
    } else {
        time_t now;
        struct tm timeinfo;
        time(&now);
        localtime_r(&now, &timeinfo);
        char time_str[64];
        strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", &timeinfo);
        ESP_LOGI(TAG, "[TIME] ✅ System time synced: %s", time_str);
    }

    // Check network connection based on mode
    if (config->network_mode == NETWORK_MODE_WIFI) {
        // WiFi mode - check WiFi connection
        wifi_ap_record_t ap_info;
        esp_err_t wifi_status = esp_wifi_sta_get_ap_info(&ap_info);
        if (wifi_status != ESP_OK) {
            ESP_LOGE(TAG, "[ERROR] WiFi not connected: %s", esp_err_to_name(wifi_status));
            return -1;
        }

        ESP_LOGI(TAG, "[WIFI] WiFi connected to: %s (RSSI: %d dBm)", ap_info.ssid, ap_info.rssi);

        // Get and log IP address
        esp_netif_ip_info_t ip_info;
        esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
        if (netif && esp_netif_get_ip_info(netif, &ip_info) == ESP_OK) {
            ESP_LOGI(TAG, "[WEB] IP Address: " IPSTR, IP2STR(&ip_info.ip));
            ESP_LOGI(TAG, "[WEB] Gateway: " IPSTR, IP2STR(&ip_info.gw));
            ESP_LOGI(TAG, "[WEB] Netmask: " IPSTR, IP2STR(&ip_info.netmask));
        }
    } else {
        // SIM mode - check PPP connection
        if (!a7670c_ppp_is_connected()) {
            ESP_LOGE(TAG, "[ERROR] PPP not connected");
            return -1;
        }

        // Get and log PPP IP address
        char ip_str[32];
        if (a7670c_ppp_get_ip_info(ip_str, sizeof(ip_str)) == ESP_OK) {
            ESP_LOGI(TAG, "[SIM] PPP IP Address: %s", ip_str);
        }

        // Get stored signal strength
        signal_strength_t signal;
        if (a7670c_get_stored_signal_strength(&signal) == ESP_OK) {
            ESP_LOGI(TAG, "[SIM] Signal: %d dBm (%s), Operator: %s",
                     signal.rssi_dbm, signal.quality ? signal.quality : "Unknown", signal.operator_name);
        }
    }
    
    // Test basic internet connectivity
    if (test_internet_connectivity() != ESP_OK) {
        ESP_LOGE(TAG, "[ERROR] Basic internet connectivity failed");
        return -1;
    }
    
    // Test Azure IoT Hub DNS resolution
    if (test_dns_resolution(IOT_CONFIG_IOTHUB_FQDN) != ESP_OK) {
        ESP_LOGE(TAG, "[ERROR] Cannot resolve Azure IoT Hub: %s", IOT_CONFIG_IOTHUB_FQDN);
        troubleshoot_azure_connectivity();
        
        ESP_LOGE(TAG, "[TOOLS] TROUBLESHOOTING STEPS:");
        ESP_LOGE(TAG, "   1. Verify IoT Hub name in web configuration");
        ESP_LOGE(TAG, "   2. Check if IoT Hub exists in Azure portal");
        ESP_LOGE(TAG, "   3. Ensure network allows Azure domains");
        ESP_LOGE(TAG, "   4. Try restarting WiFi router");
        ESP_LOGE(TAG, "   5. Check DNS settings (try 8.8.8.8)");
        return -1;
    }
    
    ESP_LOGI(TAG, "[OK] Azure IoT Hub DNS resolution successful");
    
    // Generate SAS token (valid for 1 hour)
    if (generate_sas_token(sas_token, sizeof(sas_token), 3600) != 0) {
        ESP_LOGE(TAG, "Failed to generate SAS token");
        return -1;
    }

    // Use config from earlier declaration (already loaded at start of function)
    ESP_LOGI(TAG, "[DYNAMIC CONFIG] Loading Azure credentials from web configuration");
    ESP_LOGI(TAG, "[DYNAMIC CONFIG] Device ID: %s", config->azure_device_id);
    ESP_LOGI(TAG, "[DYNAMIC CONFIG] Device Key Length: %d", strlen(config->azure_device_key));
    
    // Create Azure IoT Hub MQTT broker URI
    snprintf(mqtt_broker_uri, sizeof(mqtt_broker_uri), "mqtts://%s", IOT_CONFIG_IOTHUB_FQDN);
    
    // Create MQTT username exactly like Arduino CCL (older API version)
    snprintf(mqtt_username, sizeof(mqtt_username), "%s/%s/?api-version=2018-06-30", 
             IOT_CONFIG_IOTHUB_FQDN, config->azure_device_id);
    
    ESP_LOGI(TAG, "MQTT Broker: %s", mqtt_broker_uri);
    ESP_LOGI(TAG, "MQTT Username: %s", mqtt_username);
    ESP_LOGI(TAG, "MQTT Client ID: %s", config->azure_device_id);
    ESP_LOGI(TAG, "SAS Token: %.100s...", sas_token);

    esp_mqtt_client_config_t mqtt_config = {
        .broker.address.uri = mqtt_broker_uri,
        .broker.address.port = 8883,
        .credentials.client_id = config->azure_device_id,
        .credentials.username = mqtt_username,
        .credentials.authentication.password = sas_token,
        .session.keepalive = 30,
        .session.disable_clean_session = 0,
        .session.protocol_ver = MQTT_PROTOCOL_V_3_1_1,  // Force MQTT 3.1.1 like Arduino 1.0.6
        .network.disable_auto_reconnect = false,
        // Use ESP-IDF certificate bundle for better compatibility with PPP mode
        // The bundle includes all major root CAs and handles certificate chains properly
        .broker.verification.crt_bundle_attach = esp_crt_bundle_attach,
    };

    mqtt_client = esp_mqtt_client_init(&mqtt_config);
    if (mqtt_client == NULL) {
        ESP_LOGE(TAG, "Failed to initialize MQTT client");
        return -1;
    }

    ESP_ERROR_CHECK(esp_mqtt_client_register_event(mqtt_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL));

    esp_err_t start_result = esp_mqtt_client_start(mqtt_client);
    if (start_result != ESP_OK) {
        ESP_LOGE(TAG, "Could not start MQTT client: %s", esp_err_to_name(start_result));
        ESP_LOGE(TAG, "[TOOLS] MQTT CLIENT START TROUBLESHOOTING:");
        ESP_LOGE(TAG, "   1. Check SAS token validity");
        ESP_LOGE(TAG, "   2. Verify device exists in IoT Hub");
        ESP_LOGE(TAG, "   3. Check device key is correct");
        ESP_LOGE(TAG, "   4. Ensure IoT Hub allows new connections");
        return -1;
    }

    ESP_LOGI(TAG, "MQTT client started successfully");
    ESP_LOGI(TAG, "[TIME] Waiting for MQTT connection establishment...");
    
    // Give MQTT some time to establish connection
    vTaskDelay(pdMS_TO_TICKS(5000));
    
    if (!mqtt_connected) {
        ESP_LOGW(TAG, "[WARN] MQTT not connected yet after 5 seconds");
        ESP_LOGW(TAG, "   This is normal - connection may take longer");
        ESP_LOGW(TAG, "   Check MQTT_EVENT_CONNECTED logs for success");
    }
    
    return 0;
}

static void create_telemetry_payload(char* payload, size_t payload_size) {
    system_config_t *config = get_system_config();

    // Get network statistics for telemetry
    network_stats_t net_stats = {0};
    if (is_network_connected()) {
        // Gather network stats based on mode
        if (config->network_mode == NETWORK_MODE_WIFI) {
            wifi_ap_record_t ap_info;
            if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
                net_stats.signal_strength = ap_info.rssi;
                strncpy(net_stats.network_type, "WiFi", sizeof(net_stats.network_type));

                // Determine quality
                if (ap_info.rssi >= -60) {
                    strncpy(net_stats.network_quality, "Excellent", sizeof(net_stats.network_quality));
                } else if (ap_info.rssi >= -70) {
                    strncpy(net_stats.network_quality, "Good", sizeof(net_stats.network_quality));
                } else if (ap_info.rssi >= -80) {
                    strncpy(net_stats.network_quality, "Fair", sizeof(net_stats.network_quality));
                } else {
                    strncpy(net_stats.network_quality, "Poor", sizeof(net_stats.network_quality));
                }
            }
        } else {
            // SIM mode - use stored signal strength (cannot use AT commands in PPP mode)
            signal_strength_t signal;
            if (a7670c_get_stored_signal_strength(&signal) == ESP_OK) {
                net_stats.signal_strength = signal.rssi_dbm;
                strncpy(net_stats.network_type, "4G", sizeof(net_stats.network_type));

                // Use quality from stored signal data
                if (signal.quality != NULL) {
                    strncpy(net_stats.network_quality, signal.quality, sizeof(net_stats.network_quality) - 1);
                } else {
                    // Fallback quality determination based on RSSI
                    if (signal.rssi_dbm >= -70) {
                        strncpy(net_stats.network_quality, "Excellent", sizeof(net_stats.network_quality));
                    } else if (signal.rssi_dbm >= -85) {
                        strncpy(net_stats.network_quality, "Good", sizeof(net_stats.network_quality));
                    } else if (signal.rssi_dbm >= -100) {
                        strncpy(net_stats.network_quality, "Fair", sizeof(net_stats.network_quality));
                    } else {
                        strncpy(net_stats.network_quality, "Poor", sizeof(net_stats.network_quality));
                    }
                }
            }
        }
        ESP_LOGI(TAG, "[NET] Signal: %d dBm, Type: %s", net_stats.signal_strength, net_stats.network_type);
    } else {
        // Default values when offline
        net_stats.signal_strength = 0;
        strncpy(net_stats.network_type, "Offline", sizeof(net_stats.network_type));
    }

    // Use pre-allocated static buffers to prevent heap fragmentation
    // (malloc/free pattern was causing memory exhaustion when web server is active)
    // Reset the published sensors counter
    sensors_already_published = 0;

    sensor_reading_t* readings = telemetry_readings;
    char* temp_json = telemetry_temp_json;
    memset(readings, 0, sizeof(telemetry_readings));
    memset(temp_json, 0, sizeof(telemetry_temp_json));

    int actual_count = 0;
    esp_err_t ret = sensor_read_all_configured(readings, 20, &actual_count);
    
    if (ret == ESP_OK && actual_count > 0) {
        ESP_LOGI(TAG, "[FLOW] Creating merged JSON for %d sensors", actual_count);
        
        // Log sensor data for debugging
        for (int i = 0; i < actual_count; i++) {
            ESP_LOGI(TAG, "[DATA] Reading[%d]: Unit=%s, Valid=%d, Value=%.2f, Hex=%s", 
                     i, readings[i].unit_id, readings[i].valid, readings[i].value, readings[i].raw_hex);
        }
        
        int payload_pos = 0;
        int valid_sensors = 0;

        // Check if batch mode is enabled (send all sensors in single JSON)
        if (config->batch_telemetry) {
            ESP_LOGI(TAG, "[BATCH] Building batched JSON for all sensors");

            // Get current timestamp for all sensors
            time_t now;
            struct tm timeinfo;
            char timestamp[32];
            time(&now);
            gmtime_r(&now, &timeinfo);
            strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%SZ", &timeinfo);

            // Start building the batched JSON: {"body":[
            payload_pos = snprintf(payload, payload_size, "{\"body\":[");

            char* last_unit_id = NULL;
            for (int i = 0; i < actual_count; i++) {
                if (readings[i].valid) {
                    // Find the matching sensor config by unit_id
                    sensor_config_t* matching_sensor = NULL;
                    for (int j = 0; j < config->sensor_count; j++) {
                        if (strcmp(config->sensors[j].unit_id, readings[i].unit_id) == 0) {
                            matching_sensor = &config->sensors[j];
                            break;
                        }
                    }

                    if (!matching_sensor || !matching_sensor->enabled) {
                        ESP_LOGW(TAG, "[WARN] Sensor %s not found or disabled", readings[i].unit_id);
                        continue;
                    }

                    // Add comma separator if not first entry
                    if (valid_sensors > 0) {
                        payload_pos += snprintf(payload + payload_pos, payload_size - payload_pos, ",");
                    }

                    // Determine value key based on sensor type
                    const char* value_key = "value";
                    if (strcasecmp(matching_sensor->sensor_type, "Level") == 0 ||
                        strcasecmp(matching_sensor->sensor_type, "Radar Level") == 0) {
                        value_key = "level_filled";
                    } else if (strcasecmp(matching_sensor->sensor_type, "Flow-Meter") == 0) {
                        value_key = "consumption";
                    } else if (strcasecmp(matching_sensor->sensor_type, "RAINGAUGE") == 0) {
                        value_key = "raingauge";
                    } else if (strcasecmp(matching_sensor->sensor_type, "BOREWELL") == 0) {
                        value_key = "borewell";
                    } else if (strcasecmp(matching_sensor->sensor_type, "ENERGY") == 0) {
                        value_key = "ene_con_hex";
                    }

                    // Add sensor entry to body array
                    // Format: {"level_filled":31.55,"created_on":"2025-12-12T17:28:14Z","unit_id":"FG23352L"}
                    payload_pos += snprintf(payload + payload_pos, payload_size - payload_pos,
                        "{\"%s\":%.3f,\"created_on\":\"%s\",\"unit_id\":\"%s\"}",
                        value_key, readings[i].value, timestamp, matching_sensor->unit_id);

                    last_unit_id = matching_sensor->unit_id;
                    valid_sensors++;

                    ESP_LOGI(TAG, "[BATCH] Added sensor %d: %s = %.3f",
                             valid_sensors, matching_sensor->unit_id, readings[i].value);
                }
            }

            // Close body array and add properties
            payload_pos += snprintf(payload + payload_pos, payload_size - payload_pos,
                "],\"properties\":{\"unit_id\":\"%s\",\"flow_factor\":\"$twin.tags.flow_factor\","
                "\"max_capacity\":\"$twin.tags.max_capacity\",\"industry_id\":\"$twin.tags.industry_id\","
                "\"category_id\":\"$twin.tags.category_id\",\"shifttime\":\"$twin.tags.shifttime\","
                "\"unit_threshold\":\"$twin.tags.unit_threshold\"}}",
                last_unit_id ? last_unit_id : config->azure_device_id);

            // Send the batched JSON as a single MQTT message
            if (valid_sensors > 0 && mqtt_connected && mqtt_client != NULL) {
                char sensor_topic[256];
                snprintf(sensor_topic, sizeof(sensor_topic),
                         "devices/%s/messages/events/", config->azure_device_id);

                int msg_id = esp_mqtt_client_publish(
                    mqtt_client,
                    sensor_topic,
                    payload,
                    strlen(payload),
                    0,  // QoS 0
                    0   // No retain
                );

                if (msg_id >= 0) {
                    ESP_LOGI(TAG, "[MQTT] Sent batched telemetry with %d sensors (msg_id=%d, size=%d bytes)",
                             valid_sensors, msg_id, strlen(payload));
                } else {
                    ESP_LOGW(TAG, "[WARN] Failed to publish batched telemetry");
                    valid_sensors = 0;  // Mark as not sent
                }
            }

            ESP_LOGI(TAG, "[OK] Batched telemetry: %d sensors in single message", valid_sensors);
        } else {
            // Individual mode: send each sensor as separate MQTT message (original behavior)
            ESP_LOGI(TAG, "[INDIVIDUAL] Sending sensors as separate messages");

            for (int i = 0; i < actual_count; i++) {
                if (readings[i].valid) {
                    // Find the matching sensor config by unit_id
                    sensor_config_t* matching_sensor = NULL;
                    for (int j = 0; j < config->sensor_count; j++) {
                        if (strcmp(config->sensors[j].unit_id, readings[i].unit_id) == 0) {
                            matching_sensor = &config->sensors[j];
                            break;
                        }
                    }

                    if (!matching_sensor || !matching_sensor->enabled) {
                        ESP_LOGW(TAG, "[WARN] Sensor %s not found or disabled", readings[i].unit_id);
                        continue;
                    }

                    ESP_LOGI(TAG, "[TARGET] Sensor: Name='%s', Unit='%s', Type='%s', Value=%.2f",
                             matching_sensor->name, matching_sensor->unit_id,
                             matching_sensor->sensor_type, readings[i].value);

                    // Generate JSON for this specific sensor using template system
                    esp_err_t json_result;

                    // Check if sensor is ENERGY type and has hex string available
                    if (strcasecmp(matching_sensor->sensor_type, "ENERGY") == 0 &&
                        strlen(readings[i].raw_hex) > 0) {
                        json_result = generate_sensor_json_with_hex(
                            matching_sensor,
                            readings[i].value,
                            readings[i].raw_value,
                            readings[i].raw_hex,
                            &net_stats,
                            temp_json,
                            MAX_JSON_PAYLOAD_SIZE
                        );
                    } else {
                        json_result = generate_sensor_json(
                            matching_sensor,
                            readings[i].value,
                            readings[i].raw_value ? readings[i].raw_value : (uint32_t)(readings[i].value * 10000),
                            &net_stats,
                            temp_json,
                            MAX_JSON_PAYLOAD_SIZE
                        );
                    }

                    if (json_result == ESP_OK) {
                        if (mqtt_connected && mqtt_client != NULL) {
                            char sensor_topic[256];
                            snprintf(sensor_topic, sizeof(sensor_topic),
                                     "devices/%s/messages/events/", config->azure_device_id);

                            int msg_id = esp_mqtt_client_publish(
                                mqtt_client,
                                sensor_topic,
                                temp_json,
                                strlen(temp_json),
                                0,  // QoS 0
                                0   // No retain
                            );

                            if (msg_id >= 0) {
                                ESP_LOGI(TAG, "[MQTT] Sent sensor %d/%d: %s (msg_id=%d)",
                                         valid_sensors + 1, actual_count, matching_sensor->unit_id, msg_id);
                                valid_sensors++;
                                vTaskDelay(pdMS_TO_TICKS(100));
                            } else {
                                ESP_LOGW(TAG, "[WARN] Failed to publish sensor %s", matching_sensor->unit_id);
                            }
                        }

                        // Store last sensor's JSON in payload
                        int remaining_space = payload_size - payload_pos;
                        if (remaining_space > strlen(temp_json) + 10) {
                            payload_pos = snprintf(payload, payload_size, "%s", temp_json);
                        }
                    } else {
                        ESP_LOGW(TAG, "[WARN] Failed to generate JSON for sensor %d", i);
                    }
                }
            }

            ESP_LOGI(TAG, "[OK] Sent individual telemetry for %d/%d sensors", valid_sensors, actual_count);
        }

        sensors_already_published = valid_sensors;  // Track how many were already sent via MQTT
    } else {
        ESP_LOGW(TAG, "[WARN] No valid sensor data available, skipping telemetry");
        payload[0] = '\0'; // Empty payload to indicate no data
    }
    // No free() needed - using static buffers
}

static esp_err_t read_configured_sensors_data(void) {
    ESP_LOGI(TAG, "[FLOW] Reading configured sensors via Modbus RS485...");
    
    system_config_t *config = get_system_config();
    
    if (config->sensor_count == 0) {
        ESP_LOGW(TAG, "[WARN] No sensors configured, using fallback data");
        current_flow_data.data_valid = false;
        return ESP_FAIL;
    }
    
    // Read all configured sensors using sensor_manager
    sensor_reading_t readings[8];
    int actual_count = 0;
    
    esp_err_t ret = sensor_read_all_configured(readings, 8, &actual_count);
    
    if (ret == ESP_OK && actual_count > 0) {
        ESP_LOGI(TAG, "[OK] Successfully read %d sensors", actual_count);
        
        // Use the first valid sensor reading for telemetry
        for (int i = 0; i < actual_count; i++) {
            if (readings[i].valid) {
                // Map sensor_reading_t to flow_meter_data_t for compatibility
                current_flow_data.totalizer_value = readings[i].value;
                current_flow_data.raw_totalizer = (uint32_t)(readings[i].value * 10000); // Approximate raw
                strncpy(current_flow_data.timestamp, readings[i].timestamp, sizeof(current_flow_data.timestamp) - 1);
                current_flow_data.data_valid = true;
                current_flow_data.last_read_time = esp_timer_get_time() / 1000000;
                
                ESP_LOGI(TAG, "[DATA] Primary sensor %s: %.6f (Slave %d, Reg %d)", 
                         readings[i].unit_id, readings[i].value, 
                         config->sensors[i].slave_id, config->sensors[i].register_address);
                break;
            }
        }
        
        modbus_failure_count = 0; // Reset failure counter on success
        
        // Print Modbus statistics
        modbus_stats_t stats;
        modbus_get_statistics(&stats);
        ESP_LOGI(TAG, "[STATS] Modbus Stats - Total: %lu, Success: %lu, Failed: %lu", 
                 stats.total_requests, stats.successful_requests, stats.failed_requests);
    } else {
        ESP_LOGE(TAG, "[ERROR] Failed to read configured sensors");
        current_flow_data.data_valid = false;
        modbus_failure_count++;
        
        // Check if we've exceeded maximum Modbus failures
        if (modbus_failure_count >= MAX_MODBUS_READ_FAILURES) {
            ESP_LOGE(TAG, "[ERROR] Exceeded maximum Modbus read failures (%d)", MAX_MODBUS_READ_FAILURES);
            ESP_LOGE(TAG, "[CONFIG] Attempting to reinitialize Modbus communication...");
            
            // Try to reinitialize Modbus
            modbus_deinit();
            vTaskDelay(pdMS_TO_TICKS(1000)); // Wait 1 second
            
            esp_err_t init_ret = modbus_init();
            if (init_ret == ESP_OK) {
                ESP_LOGI(TAG, "[OK] Modbus reinitialized successfully");
                modbus_failure_count = 0;
            } else {
                ESP_LOGE(TAG, "[ERROR] Failed to reinitialize Modbus: %s", esp_err_to_name(init_ret));
                if (SYSTEM_RESTART_ON_CRITICAL_ERROR) {
                    ESP_LOGE(TAG, "[PROC] Restarting system due to persistent Modbus issues...");
                    esp_restart();
                }
            }
        }
    }
    
    return ret;
}

// GPIO interrupt handler - toggles web server on/off
static void IRAM_ATTR gpio_isr_handler(void* arg)
{
    web_server_toggle_requested = true;
}

// Initialize GPIO for configuration trigger
static void init_config_gpio(int gpio_pin)
{
    // Validate GPIO pin
    if (gpio_pin < 0 || gpio_pin > 39) {
        ESP_LOGW(TAG, "[CONFIG] Invalid trigger GPIO %d, using default GPIO %d", gpio_pin, CONFIG_GPIO_PIN);
        gpio_pin = CONFIG_GPIO_PIN;
    }
   // Configure GPIO 34 (main trigger) - pull-down, trigger on rising edge
    gpio_config_t io_conf_main = {
        .intr_type = CONFIG_GPIO_INTR_FLAG_34,
        .mode = GPIO_MODE_INPUT,
        .pin_bit_mask = (1ULL << gpio_pin),
        .pull_down_en = (gpio_pin == 34) ? 1 : 0,  // Pull-down for GPIO 34
        .pull_up_en = (gpio_pin == 34) ? 0 : 1     // Pull-up for other pins
    };
    gpio_config(&io_conf_main);

    // Configure BOOT button (GPIO 0) - pull-up, trigger on falling edge  
    gpio_config_t io_conf_boot = {
        .intr_type = CONFIG_GPIO_INTR_FLAG_0,
        .mode = GPIO_MODE_INPUT,
        .pin_bit_mask = (1ULL << CONFIG_GPIO_BOOT_PIN),
        .pull_down_en = 0,
        .pull_up_en = 1
    };
    gpio_config(&io_conf_boot);
    
    // Install GPIO ISR service (only once)
    static bool isr_service_installed = false;
    if (!isr_service_installed) {
        gpio_install_isr_service(0);
        isr_service_installed = true;
    }
    
    // Hook ISR handler for specific GPIO pin
    gpio_isr_handler_add(gpio_pin, gpio_isr_handler, NULL);
    gpio_isr_handler_add(CONFIG_GPIO_BOOT_PIN, gpio_isr_handler, NULL);
    
    ESP_LOGI(TAG, "[CORE] GPIO %d configured for web server toggle (connect to 3.3V)", gpio_pin);
    ESP_LOGI(TAG, "[CORE] GPIO 0 (BOOT button) configured for web server toggle (press button)");
}

// Initialize GPIO for modem reset
static void init_modem_reset_gpio(void)
{
    // Validate GPIO pin range
    if (modem_reset_gpio_pin < 0 || modem_reset_gpio_pin > 39) {
        ESP_LOGW(TAG, "[MODEM] Invalid GPIO pin %d, using default GPIO 2", modem_reset_gpio_pin);
        modem_reset_gpio_pin = 2;
    }
    
    // Skip certain pins that are typically reserved
   if (modem_reset_gpio_pin == 1 || modem_reset_gpio_pin == 6 ||
        modem_reset_gpio_pin == 7 || modem_reset_gpio_pin == 8 || modem_reset_gpio_pin == 9 || 
        modem_reset_gpio_pin == 10 || modem_reset_gpio_pin == 11) {
        ESP_LOGW(TAG, "[MODEM] GPIO %d is reserved, using default GPIO 2", modem_reset_gpio_pin);
        modem_reset_gpio_pin = 2;
    }
    
    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = (1ULL << modem_reset_gpio_pin),
        .pull_down_en = 0,
        .pull_up_en = 0
    };
    esp_err_t result = gpio_config(&io_conf);
    
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "[MODEM] Failed to configure GPIO %d: %s", modem_reset_gpio_pin, esp_err_to_name(result));
        return;
    }
    
    // Set initial state to LOW (modem powered on)
    gpio_set_level(modem_reset_gpio_pin, 0);
    
    ESP_LOGI(TAG, "[MODEM] GPIO %d configured for modem reset control", modem_reset_gpio_pin);
}

// Perform modem reset with network reconnection (WiFi or SIM)
static void perform_modem_reset(void)
{
    system_config_t* config = get_system_config();
    int boot_delay = (config->modem_boot_delay > 0) ? config->modem_boot_delay : 15; // Default 15 seconds

    ESP_LOGI(TAG, "[MODEM] Starting modem reset sequence...");
    ESP_LOGI(TAG, "[MODEM] Network mode: %s", config->network_mode == NETWORK_MODE_WIFI ? "WiFi" : "SIM");

    if (config->network_mode == NETWORK_MODE_WIFI) {
        // WiFi Mode Reset
        if (!modem_reset_enabled) {
            ESP_LOGI(TAG, "[MODEM] Modem reset disabled, skipping reset");
            return;
        }

        // Step 1: Disconnect WiFi gracefully
        ESP_LOGI(TAG, "[MODEM] Disconnecting WiFi before modem reset");
        esp_wifi_disconnect();
        vTaskDelay(pdMS_TO_TICKS(1000));

        // Step 2: Reset modem power (2-second power cycle)
        ESP_LOGI(TAG, "[MODEM] Power cycling modem...");
        gpio_set_level(modem_reset_gpio_pin, 1);
        ESP_LOGI(TAG, "[MODEM] Power disconnected (GPIO %d HIGH)", modem_reset_gpio_pin);
        vTaskDelay(pdMS_TO_TICKS(2000));

        // Restore modem power
        gpio_set_level(modem_reset_gpio_pin, 0);
        ESP_LOGI(TAG, "[MODEM] Power restored (GPIO %d LOW)", modem_reset_gpio_pin);

        // Step 3: Wait for modem to boot up
        ESP_LOGI(TAG, "[MODEM] Waiting %d seconds for modem to boot up...", boot_delay);
        vTaskDelay(pdMS_TO_TICKS(boot_delay * 1000));

        // Step 4: Reconnect WiFi
        ESP_LOGI(TAG, "[MODEM] Attempting WiFi reconnection...");
        esp_err_t wifi_result = esp_wifi_connect();
        if (wifi_result == ESP_OK) {
            ESP_LOGI(TAG, "[MODEM] WiFi reconnection initiated successfully");

            wifi_ap_record_t ap_info;
            int retry_count = 0;
            while (retry_count < 30) {
                if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
                    ESP_LOGI(TAG, "[MODEM] WiFi reconnected successfully to: %s", ap_info.ssid);
                    break;
                }
                vTaskDelay(pdMS_TO_TICKS(1000));
                retry_count++;
                if (retry_count % 5 == 0) {
                    ESP_LOGI(TAG, "[MODEM] Still waiting for WiFi connection... (%d/30)", retry_count);
                }
            }
            if (retry_count >= 30) {
                ESP_LOGW(TAG, "[MODEM] WiFi reconnection timeout - check modem and network");
            }
        } else {
            ESP_LOGE(TAG, "[MODEM] Failed to initiate WiFi reconnection: %s", esp_err_to_name(wifi_result));
        }

    } else if (config->network_mode == NETWORK_MODE_SIM) {
        // SIM Mode Reset - Reconnect PPP
        ESP_LOGI(TAG, "[SIM] 📱 Starting SIM module reconnection...");

        // Step 1: Disconnect existing PPP if connected
        if (a7670c_ppp_is_connected()) {
            ESP_LOGI(TAG, "[SIM] Disconnecting existing PPP connection...");
            a7670c_ppp_disconnect();
            vTaskDelay(pdMS_TO_TICKS(2000));
        }

        // Step 2: Deinitialize modem
        ESP_LOGI(TAG, "[SIM] Deinitializing modem...");
        a7670c_ppp_deinit();
        vTaskDelay(pdMS_TO_TICKS(2000));

        // Step 3: Wait for modem to settle
        ESP_LOGI(TAG, "[SIM] Waiting %d seconds for modem to reset...", boot_delay);
        vTaskDelay(pdMS_TO_TICKS(boot_delay * 1000));

        // Step 4: Reinitialize modem with config
        ESP_LOGI(TAG, "[SIM] Reinitializing A7670C modem...");
        ppp_config_t ppp_config = {
            .uart_num = config->sim_config.uart_num,
            .tx_pin = config->sim_config.uart_tx_pin,
            .rx_pin = config->sim_config.uart_rx_pin,
            .pwr_pin = config->sim_config.pwr_pin,
            .reset_pin = config->sim_config.reset_pin,
            .baud_rate = config->sim_config.uart_baud_rate,
            .apn = config->sim_config.apn,
            .user = config->sim_config.apn_user,
            .pass = config->sim_config.apn_pass,
        };

        esp_err_t ret = a7670c_ppp_init(&ppp_config);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "[SIM] ❌ Failed to reinitialize A7670C: %s", esp_err_to_name(ret));
            return;
        }

        // Step 5: Connect PPP
        ESP_LOGI(TAG, "[SIM] Connecting PPP...");
        ret = a7670c_ppp_connect();
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "[SIM] ❌ Failed to connect PPP: %s", esp_err_to_name(ret));
            return;
        }

        // Step 6: Wait for connection
        ESP_LOGI(TAG, "[SIM] ⏳ Waiting for PPP connection...");
        int retry_count = 0;
        while (retry_count < 60) {
            if (a7670c_is_connected()) {
                ESP_LOGI(TAG, "[SIM] ✅ PPP reconnected successfully!");
                mqtt_reconnect_count = 0; // Reset reconnect counter
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(1000));
            retry_count++;
            if (retry_count % 10 == 0) {
                ESP_LOGI(TAG, "[SIM] Still waiting for PPP... (%d/60)", retry_count);
            }
        }

        if (retry_count >= 60) {
            ESP_LOGW(TAG, "[SIM] ⚠️ PPP reconnection timeout");
        }
    }

    ESP_LOGI(TAG, "[MODEM] Network reset sequence completed");
}

// Reinitialize modem reset GPIO with new pin
static esp_err_t reinit_modem_reset_gpio(int new_gpio_pin)
{
    // Reset current GPIO pin to input mode to release it
    if (modem_reset_gpio_pin >= 0 && modem_reset_gpio_pin <= 39) {
        gpio_reset_pin(modem_reset_gpio_pin);
        ESP_LOGI(TAG, "[MODEM] Released GPIO %d", modem_reset_gpio_pin);
    }
    
    // Update to new pin
    modem_reset_gpio_pin = new_gpio_pin;
    
    // Initialize new GPIO pin
    init_modem_reset_gpio();
    
    return ESP_OK;
}

// External function to update modem GPIO pin (called from web_config)
esp_err_t update_modem_gpio_pin(int new_gpio_pin)
{
    return reinit_modem_reset_gpio(new_gpio_pin);
}

// Modem reset task
static void modem_reset_task(void *pvParameters)
{
    ESP_LOGI(TAG, "[MODEM] Modem reset task started");
    
    // Perform the reset
    perform_modem_reset();
    
    // Task cleanup
    modem_reset_task_handle = NULL;
    vTaskDelete(NULL);
}

// Graceful shutdown of all tasks
// Function removed - no longer used in unified operation mode

// Switch to configuration mode without restart
// Function removed - no longer used in unified operation mode

// Function removed - no longer used in unified operation mode

// Start web server for configuration
static void start_web_server(void)
{
    if (!web_server_running) {
        ESP_LOGI(TAG, "[WEB] GPIO trigger detected - starting web server with SoftAP");

        // Check if WiFi needs to be initialized (SIM mode doesn't init WiFi by default)
        system_config_t *config = get_system_config();
        if (config && config->network_mode == NETWORK_MODE_SIM && !wifi_initialized_for_sim_mode) {
            ESP_LOGI(TAG, "[WEB] SIM mode detected - initializing WiFi for web server...");

            // Initialize WiFi in AP-only mode for web server
            wifi_init_config_t wifi_cfg = WIFI_INIT_CONFIG_DEFAULT();
            esp_err_t ret = esp_wifi_init(&wifi_cfg);
            if (ret != ESP_OK && ret != ESP_ERR_WIFI_INIT_STATE) {
                ESP_LOGE(TAG, "[ERROR] Failed to init WiFi: %s", esp_err_to_name(ret));
                return;
            }

            // Create AP network interface if it doesn't exist
            esp_netif_t *ap_netif = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
            if (ap_netif == NULL) {
                ap_netif = esp_netif_create_default_wifi_ap();
                ESP_LOGI(TAG, "[WEB] Created AP network interface");
            }

            // Set WiFi mode to AP only
            ret = esp_wifi_set_mode(WIFI_MODE_AP);
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "[ERROR] Failed to set WiFi AP mode: %s", esp_err_to_name(ret));
                return;
            }

            // Configure the SoftAP
            wifi_config_t ap_config = {
                .ap = {
                    .ssid = "ModbusIoT-Config",
                    .ssid_len = strlen("ModbusIoT-Config"),
                    .channel = 6,
                    .password = "config123",
                    .max_connection = 4,
                    .authmode = WIFI_AUTH_WPA_WPA2_PSK,
                },
            };
            ret = esp_wifi_set_config(WIFI_IF_AP, &ap_config);
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "[ERROR] Failed to configure AP: %s", esp_err_to_name(ret));
                return;
            }

            // Start WiFi
            ret = esp_wifi_start();
            if (ret != ESP_OK && ret != ESP_ERR_WIFI_STATE) {
                ESP_LOGE(TAG, "[ERROR] Failed to start WiFi: %s", esp_err_to_name(ret));
                return;
            }

            wifi_initialized_for_sim_mode = true;
            ESP_LOGI(TAG, "[WEB] WiFi AP initialized successfully for SIM mode");
        }

        esp_err_t ret = web_config_start_server_only();
        if (ret == ESP_OK) {
            web_server_running = true;
            update_led_status();  // Update LED to show web server is running
            ESP_LOGI(TAG, "[WEB] Web server started successfully with SoftAP");
            ESP_LOGI(TAG, "[ACCESS] Connect to WiFi: 'ModbusIoT-Config' (password: config123)");
            ESP_LOGI(TAG, "[ACCESS] Then visit: http://192.168.4.1 to configure");
        } else {
            ESP_LOGE(TAG, "[ERROR] Failed to start web server: %s", esp_err_to_name(ret));
        }
    } else {
        ESP_LOGI(TAG, "[WEB] Web server already running - ignoring trigger");
    }
}

// Stop web server and return to operation only
static void stop_web_server(void)
{
    if (web_server_running) {
        ESP_LOGI(TAG, "[WEB] GPIO trigger detected - stopping web server");
        web_config_stop();
        web_server_running = false;
        update_led_status();  // Update LED to show web server is stopped

        // In SIM mode, stop WiFi to free up memory
        system_config_t *config = get_system_config();
        if (config && config->network_mode == NETWORK_MODE_SIM && wifi_initialized_for_sim_mode) {
            ESP_LOGI(TAG, "[WEB] SIM mode - stopping WiFi to free memory...");
            esp_wifi_stop();
            esp_wifi_deinit();
            wifi_initialized_for_sim_mode = false;
            ESP_LOGI(TAG, "[WEB] WiFi stopped and deinitialized");
        }

        ESP_LOGI(TAG, "[WEB] Web server stopped - returning to operation mode");
    } else {
        ESP_LOGI(TAG, "[WEB] Web server not running - ignoring trigger");
    }
}

// Handle web server toggle based on current state
static void handle_web_server_toggle(void)
{
    if (web_server_running) {
        stop_web_server();
    } else {
        start_web_server();
    }
    
    // Reset the toggle flag
    web_server_toggle_requested = false;
}

// Initialize status LEDs
static void init_status_leds(void)
{
    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = ((1ULL << WEBSERVER_LED_GPIO_PIN) | 
                        (1ULL << MQTT_LED_GPIO_PIN) | 
                        (1ULL << SENSOR_LED_GPIO_PIN)),
        .pull_down_en = 0,
        .pull_up_en = 0
    };
    gpio_config(&io_conf);
    
    // Initialize all LEDs to OFF (HIGH)
    gpio_set_level(WEBSERVER_LED_GPIO_PIN, 1);
    gpio_set_level(MQTT_LED_GPIO_PIN, 1);
    gpio_set_level(SENSOR_LED_GPIO_PIN, 1);
    
    ESP_LOGI(TAG, "[LED] Status LEDs initialized - GPIO %d:%d:%d (LOW=ON)", 
             WEBSERVER_LED_GPIO_PIN, MQTT_LED_GPIO_PIN, SENSOR_LED_GPIO_PIN);
}

// Set LED state (LOW = ON, HIGH = OFF)
static void set_status_led(int gpio_pin, bool on)
{
    gpio_set_level(gpio_pin, on ? 0 : 1);
}

// Update all LED states based on system status
static void update_led_status(void)
{
    // Web server LED: ON when web server is running
    if (web_server_running != webserver_led_on) {
        webserver_led_on = web_server_running;
        set_status_led(WEBSERVER_LED_GPIO_PIN, webserver_led_on);
    }
    
    // MQTT LED: ON when MQTT is connected
    if (mqtt_connected != mqtt_led_on) {
        mqtt_led_on = mqtt_connected;
        set_status_led(MQTT_LED_GPIO_PIN, mqtt_led_on);
    }
    
    // Sensor LED: ON when sensors are responding
    if (sensors_responding != sensor_led_on) {
        sensor_led_on = sensors_responding;
        set_status_led(SENSOR_LED_GPIO_PIN, sensor_led_on);
    }
}

// Modbus reading task (Core 0)
static void modbus_task(void *pvParameters)
{
    // Wait a bit to let the system stabilize before starting
    vTaskDelay(pdMS_TO_TICKS(100));

    // Use mutex to prevent log interleaving during startup (5 second timeout to prevent deadlock)
    if (startup_log_mutex != NULL) {
        if (xSemaphoreTake(startup_log_mutex, pdMS_TO_TICKS(5000)) != pdTRUE) {
            ESP_LOGW(TAG, "[WARN] Semaphore timeout in modbus_task - continuing anyway");
        }
    }

    ESP_LOGI(TAG, "╔══════════════════════════════════════════════════════════╗");
    ESP_LOGI(TAG, "║         🔌 MODBUS MONITOR TASK STARTED 🔌                ║");
    ESP_LOGI(TAG, "╚══════════════════════════════════════════════════════════╝");
    ESP_LOGI(TAG, "[CONFIG] Modbus task started on core %d", xPortGetCoreID());
    ESP_LOGI(TAG, "[CONFIG] Stack: 8192 bytes | Priority: 5");
    ESP_LOGI(TAG, "[CONFIG] Sensor reading handled by Telemetry Task");

    if (startup_log_mutex != NULL) {
        xSemaphoreGive(startup_log_mutex);
    }

    // NOTE: Sensor reading is now done in telemetry_task -> create_telemetry_payload()
    // This task only monitors for shutdown/toggle requests and keeps the task handle alive

    while (1) {
        // Check for web server toggle request (handled by main monitoring loop)
        if (web_server_toggle_requested) {
            ESP_LOGI(TAG, "[WEB] Web server toggle requested via GPIO - signaling main loop");
            // Just set the flag, main loop will handle the transition
        }

        // Check for shutdown request
        if (system_shutdown_requested) {
            ESP_LOGI(TAG, "[CONFIG] Modbus task exiting due to shutdown request");
            modbus_task_handle = NULL;
            vTaskDelete(NULL);
            return;
        }

        // Sleep for a long time - this task is just a monitor now
        vTaskDelay(pdMS_TO_TICKS(30000)); // Check every 30 seconds
    }

    // Task exiting normally (due to config mode request)
    ESP_LOGI(TAG, "[CONFIG] Modbus task exiting normally");
    modbus_task_handle = NULL;
    vTaskDelete(NULL);
}

// MQTT handling task (Core 1)
static void mqtt_task(void *pvParameters)
{
    // Wait a bit to let the system stabilize and avoid log collision
    vTaskDelay(pdMS_TO_TICKS(200));

    // Use mutex to prevent log interleaving during startup (5 second timeout to prevent deadlock)
    if (startup_log_mutex != NULL) {
        if (xSemaphoreTake(startup_log_mutex, pdMS_TO_TICKS(5000)) != pdTRUE) {
            ESP_LOGW(TAG, "[WARN] Semaphore timeout in mqtt_task - continuing anyway");
        }
    }

    ESP_LOGI(TAG, "╔══════════════════════════════════════════════════════════╗");
    ESP_LOGI(TAG, "║           ☁️  MQTT CLIENT TASK STARTED ☁️                 ║");
    ESP_LOGI(TAG, "╚══════════════════════════════════════════════════════════╝");
    ESP_LOGI(TAG, "[NET] MQTT task started on core %d", xPortGetCoreID());
    ESP_LOGI(TAG, "[NET] Stack: 8192 bytes | Priority: 4");

    if (startup_log_mutex != NULL) {
        xSemaphoreGive(startup_log_mutex);
    }

    // Skip MQTT initialization in setup mode - only needed for operation mode
    // This saves memory when WiFi AP + PPP are both running for configuration
    if (get_config_state() == CONFIG_STATE_SETUP) {
        ESP_LOGW(TAG, "[MQTT] Setup mode active - skipping MQTT initialization");
        ESP_LOGW(TAG, "[MQTT] MQTT will connect after clicking 'Start Operation'");
        ESP_LOGW(TAG, "[MQTT] This saves memory for stable web configuration");
        mqtt_task_handle = NULL;
        vTaskDelete(NULL);
        return;
    }

    // Initialize MQTT client (may fail if network not ready at startup)
    bool mqtt_initialized = (initialize_mqtt_client() == 0);
    if (!mqtt_initialized) {
        ESP_LOGW(TAG, "[WARN] MQTT initialization failed - will retry when network available");
    }

    while (1) {
        // Check for shutdown request only (web server toggle doesn't affect MQTT)
        if (system_shutdown_requested) {
            ESP_LOGI(TAG, "[NET] MQTT task exiting due to shutdown request");
            break;
        }

        // If MQTT not initialized yet, try to initialize when network becomes available
        if (!mqtt_initialized && is_network_connected()) {
            ESP_LOGI(TAG, "[MQTT] Network available - attempting MQTT initialization...");
            mqtt_initialized = (initialize_mqtt_client() == 0);
            if (mqtt_initialized) {
                ESP_LOGI(TAG, "[MQTT] ✅ MQTT client initialized successfully after network reconnect");
            } else {
                ESP_LOGW(TAG, "[MQTT] ⚠️ MQTT initialization still failing - will retry in 30 seconds");
                vTaskDelay(pdMS_TO_TICKS(30000));  // Wait before retry
                continue;
            }
        }

        // Check SAS token expiry and refresh if needed (before it expires)
        // This prevents MQTT disconnection due to expired authentication
        if (mqtt_initialized && is_network_connected() && sas_token_needs_refresh()) {
            ESP_LOGI(TAG, "[SAS] 🔄 SAS token expiring soon - initiating refresh...");
            if (refresh_sas_token_and_reconnect() == ESP_OK) {
                ESP_LOGI(TAG, "[SAS] ✅ SAS token refreshed successfully - MQTT will reconnect");
            } else {
                ESP_LOGE(TAG, "[SAS] ❌ SAS token refresh failed - will retry next cycle");
            }
        }

        // Check for periodic NTP re-sync (every 24 hours)
        // This ensures time stays accurate for TLS certificate validation and timestamps
        check_ntp_resync();

        // Handle MQTT events and maintain connection
        if (mqtt_initialized && !mqtt_connected) {
            ESP_LOGW(TAG, "[WARN] MQTT disconnected, checking connection...");
        }

        // Check every 10 seconds to reduce power consumption
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
    
    // Task exiting normally
    ESP_LOGI(TAG, "[NET] MQTT task exiting normally");
    mqtt_task_handle = NULL;
    vTaskDelete(NULL);
}

// Telemetry sending task (Core 1)
static void telemetry_task(void *pvParameters)
{
    // Wait a bit to let the system stabilize and avoid log collision
    vTaskDelay(pdMS_TO_TICKS(300));

    // Skip telemetry in setup mode - depends on MQTT which is also skipped
    if (get_config_state() == CONFIG_STATE_SETUP) {
        ESP_LOGW(TAG, "[DATA] Setup mode active - skipping telemetry task");
        ESP_LOGW(TAG, "[DATA] Telemetry will start after clicking 'Start Operation'");
        telemetry_task_handle = NULL;
        vTaskDelete(NULL);
        return;
    }

    // Use mutex to prevent log interleaving during startup (5 second timeout to prevent deadlock)
    if (startup_log_mutex != NULL) {
        if (xSemaphoreTake(startup_log_mutex, pdMS_TO_TICKS(5000)) != pdTRUE) {
            ESP_LOGW(TAG, "[WARN] Semaphore timeout in telemetry_task - continuing anyway");
        }
    }

    ESP_LOGI(TAG, "╔══════════════════════════════════════════════════════════╗");
    ESP_LOGI(TAG, "║         📊 TELEMETRY SENDER TASK STARTED 📊              ║");
    ESP_LOGI(TAG, "╚══════════════════════════════════════════════════════════╝");
    ESP_LOGI(TAG, "[DATA] Telemetry task started on core %d", xPortGetCoreID());
    ESP_LOGI(TAG, "[DATA] Stack: 8192 bytes | Priority: 3");

    if (startup_log_mutex != NULL) {
        xSemaphoreGive(startup_log_mutex);
    }
    
    system_config_t* config = get_system_config();
    TickType_t last_send_time = 0;
    bool first_telemetry = true;  // Flag to send first telemetry immediately after startup

    // Wait a bit before first telemetry to let system stabilize
    ESP_LOGI(TAG, "[DATA] Waiting 10 seconds before first telemetry...");
    vTaskDelay(pdMS_TO_TICKS(10000));

    while (1) {
        // Check for shutdown request only (web server toggle doesn't affect telemetry)
        if (system_shutdown_requested) {
            ESP_LOGI(TAG, "[DATA] Telemetry task exiting due to shutdown request");
            break;
        }

        TickType_t current_time = xTaskGetTickCount();

        // Check if enough time has passed since last send (based on telemetry_interval)
        // First telemetry is sent immediately after startup delay
        bool should_send_telemetry = first_telemetry ||
            ((current_time - last_send_time) >= pdMS_TO_TICKS(config->telemetry_interval * 1000));

        if (should_send_telemetry) {
            first_telemetry = false;  // Clear flag after first send
            // Check network connection before sending telemetry
            // If disconnected, try to reconnect before sending
            if (!is_network_connected()) {
                if (config->network_mode == NETWORK_MODE_WIFI) {
                    // WiFi mode: trigger WiFi reconnection
                    ESP_LOGI(TAG, "[WIFI] Network disconnected - attempting reconnection before telemetry...");
                    wifi_trigger_reconnect();

                    // Wait for connection to establish (max 15 seconds)
                    for (int i = 0; i < 15; i++) {
                        vTaskDelay(pdMS_TO_TICKS(1000));
                        if (is_network_connected()) {
                            ESP_LOGI(TAG, "[WIFI] ✅ WiFi reconnected successfully!");
                            break;
                        }
                    }

                    if (!is_network_connected()) {
                        ESP_LOGW(TAG, "[WIFI] ⚠️ WiFi reconnection failed - will cache to SD card");
                    }
                } else if (config->network_mode == NETWORK_MODE_SIM) {
                    // SIM mode: trigger PPP reconnection
                    ESP_LOGI(TAG, "[SIM] PPP disconnected - attempting reconnection before telemetry...");

                    // Try to reconnect PPP
                    esp_err_t ppp_ret = a7670c_ppp_connect();
                    if (ppp_ret == ESP_OK) {
                        // Wait for PPP connection to establish (max 30 seconds)
                        for (int i = 0; i < 30; i++) {
                            vTaskDelay(pdMS_TO_TICKS(1000));
                            if (is_network_connected()) {
                                ESP_LOGI(TAG, "[SIM] ✅ PPP reconnected successfully!");
                                break;
                            }
                            if (i % 10 == 9) {
                                ESP_LOGI(TAG, "[SIM] Still waiting for PPP... (%d/30)", i + 1);
                            }
                        }
                    }

                    if (!is_network_connected()) {
                        ESP_LOGW(TAG, "[SIM] ⚠️ PPP reconnection failed - will cache to SD card");
                    }
                }
            }

            // If network is connected, verify internet connectivity (WiFi connected but no internet scenario)
            // Only check occasionally to avoid overhead (every 10th telemetry or when MQTT is disconnected)
            static uint8_t internet_check_counter = 0;
            internet_check_counter++;
            if (is_network_connected() && !mqtt_connected && (internet_check_counter % 5 == 0)) {
                ESP_LOGI(TAG, "[NET] Verifying internet connectivity...");
                if (test_internet_connectivity() != ESP_OK) {
                    ESP_LOGW(TAG, "[NET] ⚠️ WiFi connected but no internet - triggering reconnection");
                    if (config->network_mode == NETWORK_MODE_WIFI) {
                        // WiFi connected but no internet - disconnect and reconnect
                        esp_wifi_disconnect();
                        vTaskDelay(pdMS_TO_TICKS(2000));
                        wifi_trigger_reconnect();
                        vTaskDelay(pdMS_TO_TICKS(10000));  // Wait for reconnection
                    }
                }
            }

            // Always call send_telemetry() - it will handle SD caching if MQTT is disconnected
            bool telemetry_success = send_telemetry();

            // ALWAYS update last_send_time after attempting telemetry
            // This ensures we respect the interval even when caching to SD card
            // Otherwise we'd cache every 5 seconds instead of every 300 seconds
            last_send_time = current_time;

            if (telemetry_success) {
                ESP_LOGI(TAG, "[OK] Telemetry sent to MQTT successfully");

                // Mark OTA firmware as valid after first successful telemetry
                // This prevents automatic rollback once system is confirmed working
                static bool ota_marked_valid = false;
                if (!ota_marked_valid) {
                    ota_mark_valid();
                    ESP_LOGI(TAG, "[OTA] Firmware marked as valid after successful telemetry");
                    ota_marked_valid = true;
                }
            } else {
                ESP_LOGW(TAG, "[WARN] Telemetry not sent to MQTT (cached to SD or skipped) - next attempt in %d seconds", config->telemetry_interval);
            }
        }
        
        vTaskDelay(pdMS_TO_TICKS(5000)); // Increased to 5 seconds to prevent timing edge cases
    }
    
    // Task exiting normally
    ESP_LOGI(TAG, "[DATA] Telemetry task exiting normally");
    telemetry_task_handle = NULL;
    vTaskDelete(NULL);
}

static bool send_telemetry(void) {
    static uint32_t call_counter = 0;
    static bool send_in_progress = false;
    system_config_t* config = get_system_config();

    call_counter++;
    ESP_LOGI(TAG, "[TRACK] send_telemetry() called #%lu, mqtt_connected=%d",
             call_counter, mqtt_connected);

    // Check if a send is already in progress
    if (send_in_progress) {
        ESP_LOGW(TAG, "[WARN] Telemetry send already in progress, skipping duplicate call #%lu", call_counter);
        return false;
    }

    // Note: Interval checking is handled by telemetry_task() - no duplicate check here
    send_in_progress = true;

    // Check network connectivity first
    bool network_available = is_network_connected();

    if (!network_available) {
        ESP_LOGW(TAG, "[WARN] ⚠️ Network not connected");

        // If SD card is enabled and caching is enabled, cache the telemetry
        if (config->sd_config.enabled && config->sd_config.cache_on_failure) {
            ESP_LOGI(TAG, "[SD] 💾 Caching telemetry to SD card (network unavailable)...");

            // Generate the telemetry payload first
            snprintf(telemetry_topic, sizeof(telemetry_topic),
                     "devices/%s/messages/events/", config->azure_device_id);
            create_telemetry_payload(telemetry_payload, sizeof(telemetry_payload));

            if (strlen(telemetry_payload) > 0) {
                // Generate timestamp for SD card message
                time_t now = time(NULL);
                struct tm timeinfo;
                gmtime_r(&now, &timeinfo);
                char timestamp[32];
                strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%SZ", &timeinfo);

                esp_err_t ret = sd_card_save_message(telemetry_topic, telemetry_payload, timestamp);
                if (ret == ESP_OK) {
                    ESP_LOGI(TAG, "[SD] ✅ Telemetry cached to SD card - will replay when network reconnects");
                    send_in_progress = false;
                    // Return FALSE to indicate not sent to cloud (only cached locally)
                    // Telemetry task will retry when network comes back online
                    return false;
                } else {
                    ESP_LOGE(TAG, "[SD] ❌ Failed to cache telemetry: %s", esp_err_to_name(ret));
                }
            }
        }

        send_in_progress = false;
        return false;
    }

    // Check if MQTT client and connection are valid
    if (!mqtt_client) {
        ESP_LOGE(TAG, "[ERROR] MQTT client not initialized - skipping telemetry send");
        send_in_progress = false;
        return false;
    }

    if (!mqtt_connected) {
        ESP_LOGW(TAG, "[WARN]  MQTT not connected");

        // Debug: Log SD configuration status
        ESP_LOGI(TAG, "[SD] DEBUG: SD enabled=%d, cache_on_failure=%d",
                 config->sd_config.enabled, config->sd_config.cache_on_failure);

        // If SD card caching is enabled, cache the telemetry
        if (config->sd_config.enabled && config->sd_config.cache_on_failure) {
            ESP_LOGI(TAG, "[SD] 💾 Caching telemetry to SD card (MQTT disconnected)...");

            // Generate the telemetry payload first
            snprintf(telemetry_topic, sizeof(telemetry_topic),
                     "devices/%s/messages/events/", config->azure_device_id);
            create_telemetry_payload(telemetry_payload, sizeof(telemetry_payload));

            if (strlen(telemetry_payload) > 0) {
                // Generate timestamp for SD card message
                time_t now = time(NULL);
                struct tm timeinfo;
                gmtime_r(&now, &timeinfo);
                char timestamp[32];
                strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%SZ", &timeinfo);

                esp_err_t ret = sd_card_save_message(telemetry_topic, telemetry_payload, timestamp);
                if (ret == ESP_OK) {
                    ESP_LOGI(TAG, "[SD] ✅ Telemetry cached to SD card - will replay when MQTT reconnects");
                    send_in_progress = false;
                    return false;
                } else {
                    ESP_LOGE(TAG, "[SD] ❌ Failed to cache telemetry: %s", esp_err_to_name(ret));
                }
            }
        } else {
            if (!config->sd_config.enabled) {
                ESP_LOGW(TAG, "[SD] SD card is DISABLED in configuration - enable it in web portal");
            } else if (!config->sd_config.cache_on_failure) {
                ESP_LOGW(TAG, "[SD] SD caching is DISABLED - enable 'Cache Messages When Network Unavailable' in web portal");
            }
        }

        ESP_LOGW(TAG, "[WARN] Skipping telemetry - no MQTT connection and no SD caching");
        send_in_progress = false;
        return false;
    }
    
    ESP_LOGI(TAG, "[SEND] Sending telemetry message #%lu...", telemetry_send_count);

    // IMPORTANT: Replay cached offline messages FIRST before sending live data
    // This ensures chronological order - older cached data must be sent before newer live data
    // Otherwise cloud analytics will use live data as reference and cached data becomes useless
    if (config->sd_config.enabled) {
        uint32_t pending_count = 0;
        sd_card_get_pending_count(&pending_count);
        if (pending_count > 0) {
            ESP_LOGI(TAG, "[SD] 📤 Found %lu cached messages - sending FIRST (before live data)", pending_count);
            esp_err_t replay_ret = sd_card_replay_messages(replay_message_callback);
            if (replay_ret == ESP_OK) {
                ESP_LOGI(TAG, "[SD] ✅ Cached messages sent successfully - now sending live data");
            } else {
                ESP_LOGW(TAG, "[SD] ⚠️ Some cached messages failed: %s", esp_err_to_name(replay_ret));
            }
            // Small delay to ensure cached messages are processed before live data
            vTaskDelay(pdMS_TO_TICKS(500));
        }
    }

    // Data is now provided by the modbus task via queue

    // Use Azure IoT Hub D2C telemetry topic format
    snprintf(telemetry_topic, sizeof(telemetry_topic),
             "devices/%s/messages/events/", config->azure_device_id);
    
    // Validate topic format
    if (strlen(telemetry_topic) == 0 || !strstr(telemetry_topic, "devices/")) {
        ESP_LOGE(TAG, "[ERROR] Invalid telemetry topic format: %s", telemetry_topic);
        send_in_progress = false;
        return false;
    }

    create_telemetry_payload(telemetry_payload, sizeof(telemetry_payload));

    // Check if payload is empty (no valid sensor data)
    if (strlen(telemetry_payload) == 0) {
        ESP_LOGW(TAG, "[WARN] No sensor data available, skipping telemetry transmission");
        send_in_progress = false;
        return false;
    }

    // Check if sensors were already published in create_telemetry_payload
    if (sensors_already_published > 0) {
        ESP_LOGI(TAG, "[OK] %d sensors already sent via MQTT in create_telemetry_payload", sensors_already_published);
        ESP_LOGI(TAG, "[SEND] Published to Azure IoT Hub:");
        ESP_LOGI(TAG, "   Topic: %s", telemetry_topic);
        ESP_LOGI(TAG, "   Sensors sent: %d", sensors_already_published);
        ESP_LOGI(TAG, "   Last payload: %s", telemetry_payload);

        telemetry_send_count++;
        total_telemetry_sent += sensors_already_published;
        last_telemetry_time = esp_timer_get_time() / 1000000;
        last_successful_telemetry_time = esp_timer_get_time() / 1000000;
        telemetry_failure_count = 0;

        ESP_LOGI(TAG, "[OK] Telemetry sent to MQTT successfully");
        send_in_progress = false;
        return true;
    }

    // If no sensors were published yet (e.g., MQTT was not connected during create_telemetry_payload)
    // Fall back to publishing the single payload
    ESP_LOGI(TAG, "[LOC] Topic: %s", telemetry_topic);
    ESP_LOGI(TAG, "[PKG] Payload: %s", telemetry_payload);
    ESP_LOGI(TAG, "[PKG] Payload Length: %d bytes", strlen(telemetry_payload));
    ESP_LOGI(TAG, "[KEY] Using SAS Token: %.50s...", sas_token);
    ESP_LOGI(TAG, "[NET] Device ID: %s", config->azure_device_id);
    ESP_LOGI(TAG, "[HUB] IoT Hub: %s", IOT_CONFIG_IOTHUB_FQDN);
    ESP_LOGI(TAG, "[LINK] MQTT Connected: %s", mqtt_connected ? "YES" : "NO");

    // Check payload size limit (Azure IoT Hub has 256KB limit)
    if (strlen(telemetry_payload) > 262144) {
        ESP_LOGE(TAG, "[ERROR] Payload too large: %d bytes (max 256KB)", strlen(telemetry_payload));
        send_in_progress = false;
        return false;
    }

    // Try QoS 0 for compatibility with Arduino 1.0.6
    int msg_id = esp_mqtt_client_publish(
        mqtt_client,
        telemetry_topic,
        telemetry_payload,
        strlen(telemetry_payload),
        0,  // QoS 0 for Arduino 1.0.6 compatibility
        0   // DO_NOT_RETAIN_MSG
    );

    if (msg_id == -1) {
        ESP_LOGE(TAG, "[ERROR] FAILED to publish telemetry - MQTT client error");
        ESP_LOGE(TAG, "   Check: MQTT connection, topic format, payload size");
        ESP_LOGE(TAG, "   Topic: %s", telemetry_topic);
        ESP_LOGE(TAG, "   Payload size: %d bytes", strlen(telemetry_payload));
        ESP_LOGE(TAG, "   MQTT connected: %s", mqtt_connected ? "YES" : "NO");

        // Try to reconnect MQTT if disconnected
        if (!mqtt_connected && mqtt_client != NULL) {
            ESP_LOGW(TAG, "[WARN] Attempting MQTT reconnection...");
            esp_mqtt_client_reconnect(mqtt_client);
        }
        send_in_progress = false; // Reset flag on failure
        telemetry_failure_count++;  // Track failures for recovery monitoring
        return false;
    } else {
        ESP_LOGI(TAG, "[OK] Telemetry queued for publish, msg_id=%d", msg_id);
        ESP_LOGI(TAG, "   Waiting for MQTT_EVENT_PUBLISHED confirmation...");
        telemetry_send_count++;
        total_telemetry_sent++; // Increment counter for web interface (Azure IoT doesn't send PUBACK)
        last_telemetry_time = esp_timer_get_time() / 1000000; // Update last telemetry timestamp
        last_successful_telemetry_time = esp_timer_get_time() / 1000000;  // For recovery timeout
        telemetry_failure_count = 0;  // Reset failure count on success

        // Log detailed publish info
        ESP_LOGI(TAG, "[SEND] Published to Azure IoT Hub:");
        ESP_LOGI(TAG, "   Topic: %s", telemetry_topic);
        ESP_LOGI(TAG, "   Message ID: %d", msg_id);
        ESP_LOGI(TAG, "   Payload: %.200s%s", telemetry_payload, strlen(telemetry_payload) > 200 ? "..." : "");

        // Store in telemetry history for web interface
        add_telemetry_to_history(telemetry_payload, true);

        send_in_progress = false; // Reset flag on success
        return true;
    }
}

void app_main(void) {
    ESP_LOGI(TAG, "╔══════════════════════════════════════════════════════════╗");
    ESP_LOGI(TAG, "║  🚀 MODBUS IoT GATEWAY v%s - SYSTEM STARTUP 🚀        ║", FW_VERSION_STRING);
    ESP_LOGI(TAG, "╠══════════════════════════════════════════════════════════╣");
    ESP_LOGI(TAG, "║    FluxGen Technologies | Industrial IoT Solutions       ║");
    ESP_LOGI(TAG, "╚══════════════════════════════════════════════════════════╝");
    ESP_LOGI(TAG, "[START] Starting Unified Modbus IoT Operation System");

    // Initialize system uptime tracking
    system_uptime_start = esp_timer_get_time() / 1000000;

    // Initialize Hardware Watchdog Timer (prevents system hang)
    ESP_LOGI(TAG, "[WDT] Initializing hardware watchdog timer (%d seconds)...", WATCHDOG_TIMEOUT_SEC);
    esp_task_wdt_config_t wdt_config = {
        .timeout_ms = WATCHDOG_TIMEOUT_SEC * 1000,
        .idle_core_mask = 0,  // Don't watch idle tasks
        .trigger_panic = true  // Restart on timeout
    };
    esp_err_t wdt_ret = esp_task_wdt_reconfigure(&wdt_config);
    if (wdt_ret != ESP_OK) {
        ESP_LOGW(TAG, "[WDT] Failed to reconfigure watchdog: %s", esp_err_to_name(wdt_ret));
    }
    // Add main task to watchdog
    wdt_ret = esp_task_wdt_add(NULL);
    if (wdt_ret == ESP_OK) {
        ESP_LOGI(TAG, "[WDT] Main task added to watchdog monitoring");
    }

    // Initialize NVS
    ESP_LOGI(TAG, "╔══════════════════════════════════════════════════════════╗");
    ESP_LOGI(TAG, "║           📦 NVS FLASH INITIALIZATION 📦                 ║");
    ESP_LOGI(TAG, "╚══════════════════════════════════════════════════════════╝");
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Load recovery restart count from NVS
    load_restart_count();

    // Initialize OTA (Over-The-Air) update module
    ESP_LOGI(TAG, "╔══════════════════════════════════════════════════════════╗");
    ESP_LOGI(TAG, "║           🔄 OTA UPDATE MODULE INITIALIZATION 🔄         ║");
    ESP_LOGI(TAG, "╚══════════════════════════════════════════════════════════╝");
    ret = ota_init();
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "[OTA] Module initialized - Firmware v%s", ota_get_version());
        if (ota_is_rollback()) {
            ESP_LOGW(TAG, "[OTA] ⚠️ RUNNING AFTER ROLLBACK - Previous firmware failed!");
            ESP_LOGW(TAG, "[OTA] Please verify system functionality before confirming.");
        }
    } else {
        ESP_LOGW(TAG, "[OTA] Module initialization failed: %s", esp_err_to_name(ret));
    }

    // Initialize web configuration system
    ESP_LOGI(TAG, "╔══════════════════════════════════════════════════════════╗");
    ESP_LOGI(TAG, "║           🌐 WEB CONFIGURATION SYSTEM 🌐                 ║");
    ESP_LOGI(TAG, "╚══════════════════════════════════════════════════════════╝");
    ESP_LOGI(TAG, "[WEB] Initializing web configuration system...");
    ret = web_config_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "[ERROR] Failed to initialize web config: %s", esp_err_to_name(ret));
        return;
    }

    // Get system configuration
    system_config_t* config = get_system_config();

    // Initialize status LEDs early so they can be used in both SETUP and OPERATION modes
    ESP_LOGI(TAG, "╔══════════════════════════════════════════════════════════╗");
    ESP_LOGI(TAG, "║           💡 STATUS LED INITIALIZATION 💡                ║");
    ESP_LOGI(TAG, "╚══════════════════════════════════════════════════════════╝");
    init_status_leds();

    // Check if we need to auto-start web server for initial configuration
    if (web_config_needs_auto_start()) {
        ESP_LOGI(TAG, "[SETUP] No configuration found - starting web server for initial setup");
        // Start WiFi in AP mode for configuration
        ret = web_config_start_ap_mode();
        if (ret == ESP_OK) {
            ret = web_config_start_server_only();
            if (ret == ESP_OK) {
                web_server_running = true;  // Set flag to indicate web server is running
                update_led_status();         // Update LED to show web server is active
                ESP_LOGI(TAG, "[WEB] Web server started automatically for initial configuration");
                ESP_LOGI(TAG, "[ACCESS] Connect to WiFi: 'ModbusIoT-Config' (password: config123)");
                ESP_LOGI(TAG, "[ACCESS] Then visit: http://192.168.4.1 to configure");
                ESP_LOGI(TAG, "[ACCESS] Please configure network mode (WiFi/SIM), sensors, and Azure settings");
                // Keep the system running in setup mode
                set_config_state(CONFIG_STATE_SETUP);
            } else {
                ESP_LOGE(TAG, "[ERROR] Failed to start web server: %s", esp_err_to_name(ret));
                set_config_state(CONFIG_STATE_OPERATION);  // Fall back to operation mode
            }
        } else {
            ESP_LOGE(TAG, "[ERROR] Failed to start AP mode: %s", esp_err_to_name(ret));
            set_config_state(CONFIG_STATE_OPERATION);  // Fall back to operation mode
        }
    } else {
        // Force operation mode for unified operation (web server + operation mode together)
        ESP_LOGI(TAG, "[SYS] Starting in UNIFIED OPERATION mode");
        set_config_state(CONFIG_STATE_OPERATION);
    }

    // Log Azure configuration loaded from NVS
    ESP_LOGI(TAG, "[AZURE CONFIG] Loaded from NVS:");
    ESP_LOGI(TAG, "  - Device ID: %s", config->azure_device_id);
    ESP_LOGI(TAG, "  - Device Key (first 10 chars): %.10s...", config->azure_device_key);
    ESP_LOGI(TAG, "  - Device Key Length: %d", strlen(config->azure_device_key));
    ESP_LOGI(TAG, "  - Telemetry Interval: %d seconds", config->telemetry_interval);
    ESP_LOGI(TAG, "  - Network Mode: %s", config->network_mode == NETWORK_MODE_WIFI ? "WiFi" : "SIM Module");

    // Initialize RTC if enabled
    if (config->rtc_config.enabled) {
        ESP_LOGI(TAG, "╔══════════════════════════════════════════════════════════╗");
        ESP_LOGI(TAG, "║         🕐 DS3231 REAL-TIME CLOCK SETUP 🕐               ║");
        ESP_LOGI(TAG, "╚══════════════════════════════════════════════════════════╝");
        ESP_LOGI(TAG, "[RTC] 🕐 Initializing DS3231 Real-Time Clock...");
        esp_err_t rtc_ret = ds3231_init();
        if (rtc_ret == ESP_OK) {
            ESP_LOGI(TAG, "[RTC] ✅ RTC initialized successfully");

            // Sync system time FROM RTC only if RTC has valid time (year >= 2024)
            rtc_ret = ds3231_sync_system_time();
            if (rtc_ret == ESP_OK) {
                time_t now = time(NULL);
                struct tm timeinfo;
                gmtime_r(&now, &timeinfo);
                char time_str[32];
                strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", &timeinfo);

                // Only accept RTC time if it's valid (year >= 2024)
                if (timeinfo.tm_year >= (2024 - 1900)) {
                    ESP_LOGI(TAG, "[RTC] ✅ System time synced from RTC: %s UTC", time_str);
                } else {
                    ESP_LOGW(TAG, "[RTC] ⚠️ RTC has invalid time (%s) - will sync from NTP", time_str);
                    // Reset system time to epoch so NTP will try to sync
                    struct timeval tv = { .tv_sec = 0, .tv_usec = 0 };
                    settimeofday(&tv, NULL);
                }
            } else {
                ESP_LOGW(TAG, "[RTC] ⚠️ Could not read time from RTC");
            }
        } else {
            ESP_LOGW(TAG, "[RTC] ⚠️ RTC initialization failed: %s (optional feature - continuing)",
                     esp_err_to_name(rtc_ret));
        }
    } else {
        ESP_LOGI(TAG, "[RTC] RTC disabled in configuration");
    }

    // Initialize SD card if enabled
    if (config->sd_config.enabled) {
        ESP_LOGI(TAG, "╔══════════════════════════════════════════════════════════╗");
        ESP_LOGI(TAG, "║           💾 SD CARD INITIALIZATION 💾                   ║");
        ESP_LOGI(TAG, "╚══════════════════════════════════════════════════════════╝");
        ESP_LOGI(TAG, "[SD] 🔧 SD Config: enabled=%d, cache_on_failure=%d",
                 config->sd_config.enabled, config->sd_config.cache_on_failure);
        ESP_LOGI(TAG, "[SD] 💾 Initializing SD Card for offline data caching...");
        esp_err_t sd_ret = sd_card_init();
        if (sd_ret == ESP_OK) {
            ESP_LOGI(TAG, "[SD] ✅ SD card mounted successfully");
            ESP_LOGI(TAG, "[SD] 📊 Caching enabled: %s", config->sd_config.cache_on_failure ? "YES" : "NO");
        } else {
            ESP_LOGW(TAG, "[SD] ⚠️ SD card mount failed: %s", esp_err_to_name(sd_ret));
            ESP_LOGW(TAG, "[SD] System will continue without offline caching");
            config->sd_config.enabled = false; // Disable SD if mount fails
        }
    } else {
        ESP_LOGI(TAG, "[SD] SD card logging disabled in configuration");
    }
    
    // Load modem reset settings from configuration
    modem_reset_enabled = config->modem_reset_enabled;
    modem_reset_gpio_pin = (config->modem_reset_gpio_pin > 0) ? config->modem_reset_gpio_pin : 2;
    
    // Initialize GPIO for web server toggle (use configured pin)
    int trigger_gpio = (config->trigger_gpio_pin > 0) ? config->trigger_gpio_pin : CONFIG_GPIO_PIN;
    ESP_LOGI(TAG, "[WEB] GPIO %d configured for web server toggle", trigger_gpio);
    init_config_gpio(trigger_gpio);
    
    // Initialize GPIO for modem reset
    init_modem_reset_gpio();
    
    // Initialize Modbus RS485 communication
    ESP_LOGI(TAG, "╔══════════════════════════════════════════════════════════╗");
    ESP_LOGI(TAG, "║         🔌 MODBUS RS485 INITIALIZATION 🔌                ║");
    ESP_LOGI(TAG, "╚══════════════════════════════════════════════════════════╝");
    ESP_LOGI(TAG, "[CONFIG] Initializing Modbus RS485 communication...");
    ret = modbus_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "[ERROR] Failed to initialize Modbus: %s", esp_err_to_name(ret));
        ESP_LOGE(TAG, "[WARN] System will continue with simulated data only");
    } else {
        ESP_LOGI(TAG, "[OK] Modbus RS485 initialized successfully");
        
        // Test all configured sensors
        ESP_LOGI(TAG, "[TEST] Testing %d configured sensors...", config->sensor_count);
        for (int i = 0; i < config->sensor_count; i++) {
            if (config->sensors[i].enabled) {
                ESP_LOGI(TAG, "Testing sensor %d: %s (Unit: %s)", 
                         i + 1, config->sensors[i].name, config->sensors[i].unit_id);
                // TODO: Test individual sensor
            }
        }
    }
    
    // Create queue for sensor data communication between tasks
    sensor_data_queue = xQueueCreate(5, sizeof(flow_meter_data_t));
    if (sensor_data_queue == NULL) {
        ESP_LOGE(TAG, "[ERROR] Failed to create sensor data queue");
        return;
    }

    // Initialize WiFi stack only if:
    // 1. Not already initialized during auto-start (setup mode)
    // 2. AND we are using WiFi mode (not SIM mode - to save ~50KB heap)
    if (get_config_state() != CONFIG_STATE_SETUP) {
        if (config->network_mode == NETWORK_MODE_WIFI) {
            // WiFi mode - initialize WiFi stack
            ret = web_config_start_ap_mode();  // This actually starts STA mode for normal operation
            if (ret != ESP_OK) {
                ESP_LOGW(TAG, "[WARN] WiFi initialization had issues - some features may not work");
                // Don't return - allow system to continue for modbus-only or SIM operation
            }
        } else {
            // SIM mode - skip WiFi initialization to save memory
            ESP_LOGI(TAG, "[NET] SIM mode selected - skipping WiFi initialization to save memory");
            ESP_LOGI(TAG, "[NET] WiFi AP can be enabled later via GPIO toggle if needed");

            // Still need to initialize netif and event loop for PPP
            esp_err_t netif_ret = esp_netif_init();
            if (netif_ret != ESP_OK && netif_ret != ESP_ERR_INVALID_STATE) {
                ESP_LOGE(TAG, "Failed to initialize netif: %s", esp_err_to_name(netif_ret));
            }
            netif_ret = esp_event_loop_create_default();
            if (netif_ret != ESP_OK && netif_ret != ESP_ERR_INVALID_STATE) {
                ESP_LOGE(TAG, "Failed to create event loop: %s", esp_err_to_name(netif_ret));
            }
        }
    } else {
        ESP_LOGI(TAG, "[NET] WiFi already initialized during setup mode - skipping re-initialization");
    }

    // Network initialization based on configured mode (WiFi or SIM)
    ESP_LOGI(TAG, "╔══════════════════════════════════════════════════════════╗");
    ESP_LOGI(TAG, "║         📡 NETWORK CONNECTION SETUP 📡                   ║");
    ESP_LOGI(TAG, "╚══════════════════════════════════════════════════════════╝");
    ESP_LOGI(TAG, "[NET] Network Mode: %s",
             config->network_mode == NETWORK_MODE_WIFI ? "WiFi" : "SIM Module");
    ESP_LOGI(TAG, "[NET] Status: INITIALIZING");

    if (config->network_mode == NETWORK_MODE_WIFI) {
        // WiFi Mode - Check if WiFi was initialized successfully
        if (strlen(config->wifi_ssid) == 0) {
            ESP_LOGW(TAG, "[WIFI] WARNING: WiFi SSID not configured");
            ESP_LOGI(TAG, "[WIFI] TIP: To use WiFi:");
            ESP_LOGI(TAG, "[WIFI]    1. Configure WiFi via web interface");
            ESP_LOGI(TAG, "[WIFI]    2. Or switch to SIM module mode");
            ESP_LOGI(TAG, "[WIFI] System will operate in offline mode (Modbus only)");
        } else {
            ESP_LOGI(TAG, "[WIFI] OK: WiFi STA mode already configured by web_config");
            ESP_LOGI(TAG, "[WIFI] Waiting for WiFi connection to %s...", config->wifi_ssid);

            // Just wait for connection to complete
            int retry = 0;
            while (retry < 30) {
                wifi_ap_record_t ap_info;
                if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
                    ESP_LOGI(TAG, "[WIFI] ✅ Connected successfully");
                    ESP_LOGI(TAG, "[WIFI] 📊 Signal Strength: %d dBm", ap_info.rssi);
                    break;
                }
                vTaskDelay(pdMS_TO_TICKS(1000));
                retry++;
            }

            if (retry >= 30) {
                ESP_LOGW(TAG, "[WIFI] ⚠️ Connection timeout - continuing in offline mode");
                ESP_LOGW(TAG, "[WIFI] System will cache telemetry to SD card if enabled");
            }
        }

    } else if (config->network_mode == NETWORK_MODE_SIM) {
        // SIM Mode - Direct A7670C initialization
        ESP_LOGI(TAG, "[SIM] 📱 Starting SIM module (A7670C)...");

        ppp_config_t ppp_config = {
            .uart_num = config->sim_config.uart_num,
            .tx_pin = config->sim_config.uart_tx_pin,
            .rx_pin = config->sim_config.uart_rx_pin,
            .pwr_pin = config->sim_config.pwr_pin,
            .reset_pin = config->sim_config.reset_pin,
            .baud_rate = config->sim_config.uart_baud_rate,
            .apn = config->sim_config.apn,
            .user = config->sim_config.apn_user,
            .pass = config->sim_config.apn_pass,
        };

        ret = a7670c_ppp_init(&ppp_config);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "[SIM] ❌ Failed to initialize A7670C: %s", esp_err_to_name(ret));
            ESP_LOGW(TAG, "[SIM] Entering offline mode");
        } else {
            ret = a7670c_ppp_connect();
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "[SIM] ❌ Failed to connect PPP: %s", esp_err_to_name(ret));
                ESP_LOGW(TAG, "[SIM] Entering offline mode");
            } else {
                // Wait for PPP connection with timeout
                ESP_LOGI(TAG, "[SIM] ⏳ Waiting for PPP connection...");
                int retry = 0;
                while (retry < 60) {
                    if (a7670c_is_connected()) {
                        ESP_LOGI(TAG, "[SIM] ✅ PPP connection established");

                        // Get stored signal strength (checked before PPP mode)
                        // IMPORTANT: Cannot use AT commands once in PPP mode!
                        signal_strength_t signal;
                        if (a7670c_get_stored_signal_strength(&signal) == ESP_OK) {
                            ESP_LOGI(TAG, "[SIM] 📊 Signal Strength: %d dBm (%s)",
                                     signal.rssi_dbm, signal.quality ? signal.quality : "Unknown");
                            ESP_LOGI(TAG, "[SIM] 📡 Operator: %s", signal.operator_name);
                        }
                        break;
                    }
                    vTaskDelay(pdMS_TO_TICKS(1000));
                    retry++;
                }

                if (retry >= 60) {
                    ESP_LOGW(TAG, "[SIM] ⚠️ PPP connection timeout - entering offline mode");
                    ESP_LOGW(TAG, "[SIM] System will cache telemetry to SD card if enabled");
                }
            }
        }
    }

    // Initialize SNTP time synchronization (always run, will timeout gracefully if network unavailable)
    ESP_LOGI(TAG, "[TIME] 🕐 Initializing SNTP time synchronization...");
    initialize_time();

    // If RTC is enabled, sync it with NTP time ONLY if NTP succeeded (time is valid)
    if (config->rtc_config.enabled && is_network_connected()) {
        time_t now = time(NULL);
        struct tm timeinfo;
        gmtime_r(&now, &timeinfo);

        // Only update RTC if system time is valid (NTP succeeded)
        if (timeinfo.tm_year >= (2024 - 1900)) {
            ESP_LOGI(TAG, "[RTC] 🔄 Syncing RTC with NTP time...");
            esp_err_t sync_ret = ds3231_update_from_system_time();
            if (sync_ret == ESP_OK) {
                ESP_LOGI(TAG, "[RTC] ✅ RTC synchronized with NTP");
            } else {
                ESP_LOGW(TAG, "[RTC] ⚠️ Failed to sync RTC: %s", esp_err_to_name(sync_ret));
            }
        } else {
            ESP_LOGW(TAG, "[RTC] ⚠️ NTP sync failed - NOT updating RTC with invalid time");
        }
    }

    // Wait a bit to ensure time is properly synchronized
    vTaskDelay(5000 / portTICK_PERIOD_MS);

    ESP_LOGI(TAG, "╔══════════════════════════════════════════════════════════╗");
    ESP_LOGI(TAG, "║          ⚙️  DUAL-CORE TASK CREATION ⚙️                 ║");
    ESP_LOGI(TAG, "╚══════════════════════════════════════════════════════════╝");
    ESP_LOGI(TAG, "[TASK] Core 0: Modbus Reading");
    ESP_LOGI(TAG, "[TASK] Core 1: MQTT & Telemetry");
    ESP_LOGI(TAG, "[START] Starting dual-core task distribution...");

    // Create mutex for synchronized logging during task startup
    startup_log_mutex = xSemaphoreCreateMutex();
    if (startup_log_mutex == NULL) {
        ESP_LOGW(TAG, "[WARN] Failed to create startup log mutex - logs may interleave");
    }

    // Create mutex for telemetry history buffer
    telemetry_history_mutex = xSemaphoreCreateMutex();
    if (telemetry_history_mutex == NULL) {
        ESP_LOGW(TAG, "[WARN] Failed to create telemetry history mutex");
    }

    // Create Modbus task on Core 0 (dedicated for sensor reading)
    BaseType_t modbus_result = xTaskCreatePinnedToCore(
        modbus_task,
        "modbus_task",
        8192,  // Increased from 4096 to handle sensor_manager integration
        NULL,
        5,  // High priority for sensor reading
        &modbus_task_handle,
        0   // Core 0
    );
    
    if (modbus_result != pdPASS) {
        ESP_LOGE(TAG, "[ERROR] Failed to create Modbus task");
        return;
    }
    
    // Create MQTT task on Core 1 (handles connectivity)
    BaseType_t mqtt_result = xTaskCreatePinnedToCore(
        mqtt_task,
        "mqtt_task",
        8192,
        NULL,
        4,  // Medium priority
        &mqtt_task_handle,
        1   // Core 1
    );
    
    if (mqtt_result != pdPASS) {
        ESP_LOGE(TAG, "[ERROR] Failed to create MQTT task");
        return;
    }
    
    // Create Telemetry task on Core 1 (handles data transmission)
    BaseType_t telemetry_result = xTaskCreatePinnedToCore(
        telemetry_task,
        "telemetry_task",
        8192,  // Increased stack size to prevent overflow
        NULL,
        3,  // Lower priority than MQTT
        &telemetry_task_handle,
        1   // Core 1
    );
    
    if (telemetry_result != pdPASS) {
        ESP_LOGE(TAG, "[ERROR] Failed to create Telemetry task");
        return;
    }
    
    ESP_LOGI(TAG, "[OK] All tasks created successfully");
    ESP_LOGI(TAG, "[CORE] Modbus reading: Core 0 (priority 5)");
    ESP_LOGI(TAG, "[NET] MQTT handling: Core 1 (priority 4)");
    ESP_LOGI(TAG, "[DATA] Telemetry sending: Core 1 (priority 3)");
    ESP_LOGI(TAG, "[WEB] GPIO %d: Pull LOW to toggle web server ON/OFF", trigger_gpio);

    // Wait for all tasks to display their startup messages
    vTaskDelay(pdMS_TO_TICKS(500));

    // System ready announcement - display correct mode
    ESP_LOGI(TAG, "╔══════════════════════════════════════════════════════════╗");
    if (get_config_state() == CONFIG_STATE_SETUP) {
        ESP_LOGI(TAG, "║          🔧 SYSTEM READY - SETUP MODE ACTIVE 🔧         ║");
        ESP_LOGI(TAG, "╠══════════════════════════════════════════════════════════╣");
        ESP_LOGI(TAG, "║    Web server running - please complete configuration   ║");
    } else {
        ESP_LOGI(TAG, "║        ✅ SYSTEM READY - ENTERING OPERATION MODE ✅      ║");
        ESP_LOGI(TAG, "╠══════════════════════════════════════════════════════════╣");
        ESP_LOGI(TAG, "║         All subsystems initialized and operational       ║");
    }
    ESP_LOGI(TAG, "╚══════════════════════════════════════════════════════════╝");

    // DISABLED: Telegram bot feature (not compiled)
    // telegram_bot_init();
    // if (telegram_is_enabled()) {
    //     ESP_LOGI(TAG, "[TELEGRAM] Starting Telegram bot...");
    //     telegram_bot_start();
    // } else {
    //     ESP_LOGI(TAG, "[TELEGRAM] Telegram bot disabled in configuration");
    // }

    // Main monitoring loop with web server toggle support
    while (1) {
        // Check for web server toggle request
        if (web_server_toggle_requested) {
            ESP_LOGI(TAG, "[WEB] GPIO %d trigger detected - toggling web server", trigger_gpio);
            handle_web_server_toggle();
        }
        
        // Update LED status based on current system state
        update_led_status();
        
        // Log system status every 30 seconds
        static uint32_t last_status_log = 0;
        uint32_t current_time_ms = esp_timer_get_time() / 1000;
        
        if (current_time_ms - last_status_log > 30000) {
            char buffer[100];

            ESP_LOGI(TAG, "+----------------------------------------------+");
            ESP_LOGI(TAG, "|           SYSTEM STATUS MONITOR             |");
            ESP_LOGI(TAG, "+----------------------------------------------+");

            snprintf(buffer, sizeof(buffer),
                     "| MQTT: %-15s Messages: %-10lu |",
                     mqtt_connected ? "CONNECTED" : "OFFLINE",
                     telemetry_send_count);
            ESP_LOGI(TAG, "%s", buffer);

            snprintf(buffer, sizeof(buffer),
                     "| Sensors: %-12d Web: %-14s |",
                     config->sensor_count,
                     web_server_running ? "RUNNING" : "STOPPED");
            ESP_LOGI(TAG, "%s", buffer);

            snprintf(buffer, sizeof(buffer),
                     "| GPIO: %-15d LEDs: W=%-3s M=%-3s S=%-3s |",
                     trigger_gpio,
                     webserver_led_on ? "ON" : "OFF",
                     mqtt_led_on ? "ON" : "OFF",
                     sensor_led_on ? "ON" : "OFF");
            ESP_LOGI(TAG, "%s", buffer);

            snprintf(buffer, sizeof(buffer),
                     "| Free Heap: %-10lu Min Free: %-10lu |",
                     esp_get_free_heap_size(),
                     esp_get_minimum_free_heap_size());
            ESP_LOGI(TAG, "%s", buffer);

            snprintf(buffer, sizeof(buffer),
                     "| Tasks: Modbus=%-3s    MQTT=%-3s   Telem=%-3s |",
                     (modbus_task_handle != NULL) ? "OK" : "NO",
                     (mqtt_task_handle != NULL) ? "OK" : "NO",
                     (telemetry_task_handle != NULL) ? "OK" : "NO");
            ESP_LOGI(TAG, "%s", buffer);

            ESP_LOGI(TAG, "+----------------------------------------------+");

            last_status_log = current_time_ms;
        }

        // Feed the hardware watchdog to prevent system reset
        esp_task_wdt_reset();

        // Check for telemetry timeout and force restart if needed (only in operation mode)
        if (get_config_state() != CONFIG_STATE_SETUP) {
            check_telemetry_timeout_recovery();
        }

        // Heartbeat logging to SD card (every 5 minutes)
        int64_t current_time_sec = esp_timer_get_time() / 1000000;
        if (current_time_sec - last_heartbeat_time >= HEARTBEAT_LOG_INTERVAL_SEC) {
            log_heartbeat_to_sd();
            last_heartbeat_time = current_time_sec;
        }

        // Device Twin reporting to Azure (every 1 minute when connected)
        static int64_t last_twin_report = 0;
        if (mqtt_connected && (current_time_sec - last_twin_report >= DEVICE_TWIN_UPDATE_INTERVAL_SEC)) {
            report_device_twin();
            last_twin_report = current_time_sec;
        }

        vTaskDelay(pdMS_TO_TICKS(5000)); // Check every 5 seconds
    }
}