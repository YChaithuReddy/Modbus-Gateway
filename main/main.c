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
// #include "telegram_bot.h"  // Disabled to save memory
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

// SD card replay control - prevents overwhelming Azure IoT Hub
static volatile bool sd_replay_should_stop = false;    // Flag to stop replay on MQTT disconnect
static volatile uint32_t sd_replay_messages_sent = 0;  // Messages sent in current batch
static volatile uint32_t sd_replay_last_msg_id = 0;    // Track last message ID being replayed

// Static buffers to avoid stack overflow
static char mqtt_broker_uri[256];
static char mqtt_username[256];
static char telemetry_topic[256];
static char telemetry_payload[4096];  // Reduced to save 4KB RAM (supports ~10 sensors)
static char c2d_topic[256];

// Device Twin topic patterns for Azure IoT Hub
static const char *DEVICE_TWIN_DESIRED_TOPIC = "$iothub/twin/PATCH/properties/desired/#";
static const char *DEVICE_TWIN_RES_TOPIC = "$iothub/twin/res/#";
static char device_twin_reported_topic[128];
static int device_twin_request_id = 0;


// Static buffers for telemetry to prevent heap fragmentation
// These replace malloc/free calls that were causing memory exhaustion
static sensor_reading_t telemetry_readings[10];  // Reduced from 20 to match sensors[10]
static char telemetry_temp_json[MAX_JSON_PAYLOAD_SIZE];  // Pre-allocated JSON buffer
static int sensors_already_published = 0;  // Track sensors published in create_telemetry_payload

// GPIO interrupt flag for web server toggle
static volatile bool web_server_toggle_requested = false;
static volatile bool system_shutdown_requested = false;
static volatile bool web_server_running = false;
static volatile bool wifi_initialized_for_sim_mode = false;  // Track if WiFi was init'd in SIM mode

// Device Twin remote control variables
static volatile bool maintenance_mode = false;    // Pause all telemetry when true
static volatile bool ota_enabled = true;          // Allow/block OTA updates
static char ota_url[256] = "";                    // Remote OTA firmware URL

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
#define TELEMETRY_HISTORY_SIZE 10  // Reduced from 25 to save ~6.5KB RAM
typedef struct {
    char timestamp[32];
    char payload[200];  // Reduced from 400 to save ~5KB RAM
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
static void handle_device_twin_desired_properties(const char *data, int data_len);
static void report_device_twin_properties(void);
static void ota_status_callback(ota_status_t status, const char* message);
static int initialize_mqtt_client(void);

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

// Apply sensor type presets - auto-configure register addresses and settings based on sensor_type
// This allows users to add sensors with minimal configuration (just name, unit_id, slave_id, sensor_type)
static void apply_sensor_type_presets(sensor_config_t* sensor) {
    if (!sensor || strlen(sensor->sensor_type) == 0) {
        return;
    }

    // Only apply register_address preset if not explicitly set (still at default 0)
    bool apply_register = (sensor->register_address == 0);

    ESP_LOGI(TAG, "[PRESET] Checking presets for sensor_type: %s (apply_register=%d)",
             sensor->sensor_type, apply_register);

    // ZEST (AquaGen Flow Meter)
    // Register 4121 (0x1019), 4 registers, UINT16 integer + FLOAT32 decimal
    if (strcmp(sensor->sensor_type, "ZEST") == 0) {
        if (apply_register) sensor->register_address = 4121;
        sensor->quantity = 4;
        ESP_LOGI(TAG, "[PRESET] Applied ZEST preset: reg=%d, qty=%d",
                 sensor->register_address, sensor->quantity);
    }
    // Panda EMF (Electromagnetic Flow Meter)
    // Register 4114 (0x1012), 4 registers, INT32 integer + FLOAT32 decimal
    else if (strcmp(sensor->sensor_type, "Panda_EMF") == 0) {
        if (apply_register) sensor->register_address = 4114;
        sensor->quantity = 4;
        ESP_LOGI(TAG, "[PRESET] Applied Panda_EMF preset: reg=%d, qty=%d",
                 sensor->register_address, sensor->quantity);
    }
    // Panda USM (Ultrasonic Flow Meter)
    // Register 8, 4 registers, FLOAT64 (64-bit double)
    else if (strcmp(sensor->sensor_type, "Panda_USM") == 0) {
        if (apply_register) sensor->register_address = 8;
        sensor->quantity = 4;
        ESP_LOGI(TAG, "[PRESET] Applied Panda_USM preset: reg=%d, qty=%d",
                 sensor->register_address, sensor->quantity);
    }
    // Panda Level (Water Level Sensor)
    // Register 1, 1 register, UINT16
    else if (strcmp(sensor->sensor_type, "Panda_Level") == 0) {
        if (apply_register) sensor->register_address = 1;
        sensor->quantity = 1;
        strncpy(sensor->data_type, "UINT16", sizeof(sensor->data_type) - 1);
        ESP_LOGI(TAG, "[PRESET] Applied Panda_Level preset: reg=%d, qty=%d, data_type=%s",
                 sensor->register_address, sensor->quantity, sensor->data_type);
    }
    // Dailian EMF (Electromagnetic Flow Meter)
    // Register 2006 (0x07D6), 2 registers, UINT32 word-swapped totaliser
    else if (strcmp(sensor->sensor_type, "Dailian_EMF") == 0) {
        if (apply_register) sensor->register_address = 2006;
        sensor->quantity = 2;
        ESP_LOGI(TAG, "[PRESET] Applied Dailian_EMF preset: reg=%d, qty=%d",
                 sensor->register_address, sensor->quantity);
    }
    // Clampon (Clamp-on Flow Meter)
    // 4 registers, UINT32_BADC + FLOAT32_BADC (register address varies by installation)
    else if (strcmp(sensor->sensor_type, "Clampon") == 0) {
        sensor->quantity = 4;
        ESP_LOGI(TAG, "[PRESET] Applied Clampon preset: qty=%d (register must be specified)",
                 sensor->quantity);
    }
    // Flow-Meter (Generic Flow Meter)
    // 4 registers, UINT32_BADC + FLOAT32_BADC (register address varies)
    else if (strcmp(sensor->sensor_type, "Flow-Meter") == 0) {
        sensor->quantity = 4;
        ESP_LOGI(TAG, "[PRESET] Applied Flow-Meter preset: qty=%d (register must be specified)",
                 sensor->quantity);
    }
    // Radar Level sensor
    else if (strcmp(sensor->sensor_type, "Radar Level") == 0 ||
             strcmp(sensor->sensor_type, "Level") == 0) {
        sensor->quantity = 2;
        strncpy(sensor->data_type, "FLOAT32", sizeof(sensor->data_type) - 1);
        ESP_LOGI(TAG, "[PRESET] Applied Level preset: qty=%d, data_type=%s",
                 sensor->quantity, sensor->data_type);
    }
    // Piezometer (Water Level)
    else if (strcmp(sensor->sensor_type, "Piezometer") == 0) {
        sensor->quantity = 2;
        strncpy(sensor->data_type, "FLOAT32", sizeof(sensor->data_type) - 1);
        ESP_LOGI(TAG, "[PRESET] Applied Piezometer preset: qty=%d, data_type=%s",
                 sensor->quantity, sensor->data_type);
    }
    else {
        ESP_LOGW(TAG, "[PRESET] No preset found for sensor_type: %s", sensor->sensor_type);
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

// ============================================================================
// MQTT Stop/Start functions for OTA (SIM mode needs exclusive PPP access)
// ============================================================================

// Stop MQTT temporarily for OTA - frees PPP for exclusive OTA use
void mqtt_stop_for_ota(void) {
    ESP_LOGI(TAG, "[OTA] Stopping MQTT to free PPP for OTA download...");

    if (mqtt_client != NULL) {
        esp_mqtt_client_stop(mqtt_client);
        esp_mqtt_client_destroy(mqtt_client);
        mqtt_client = NULL;
        mqtt_connected = false;
        ESP_LOGI(TAG, "[OTA] MQTT stopped and destroyed - PPP now available for OTA");
    } else {
        ESP_LOGW(TAG, "[OTA] MQTT client was not running");
    }

    // Give network time to settle
    vTaskDelay(pdMS_TO_TICKS(2000));
}

// Restart MQTT after OTA completes or fails
void mqtt_restart_after_ota(void) {
    ESP_LOGI(TAG, "[OTA] Restarting MQTT after OTA...");

    if (mqtt_client == NULL) {
        ESP_LOGI(TAG, "[OTA] Re-initializing MQTT client...");
        if (initialize_mqtt_client() == 0) {
            ESP_LOGI(TAG, "[OTA] MQTT client re-initialized successfully");
        } else {
            ESP_LOGE(TAG, "[OTA] Failed to re-initialize MQTT adapter");
        }
    } else {
        ESP_LOGW(TAG, "[OTA] MQTT client already exists - restarting...");
        esp_err_t result = esp_mqtt_client_start(mqtt_client);
        if (result != ESP_OK) {
             ESP_LOGE(TAG, "[OTA] Failed to restart MQTT: %s", esp_err_to_name(result));
        }
    }
}

// Callback function for replaying cached SD card messages to MQTT
// Uses rate limiting from iot_configs.h to prevent Azure IoT Hub from disconnecting
static void replay_message_callback(const pending_message_t* msg) {
    if (!msg || !mqtt_client) {
        ESP_LOGE(TAG, "[SD] Invalid message or MQTT client not initialized");
        sd_replay_should_stop = true;
        return;
    }

    // Check if we should stop (MQTT disconnected during previous message)
    if (sd_replay_should_stop) {
        ESP_LOGW(TAG, "[SD] Replay stopped - MQTT connection lost");
        return;
    }

    // Check MQTT connection before each message (critical for rate limiting)
    if (!mqtt_connected) {
        ESP_LOGW(TAG, "[SD] Cannot replay message %lu - MQTT not connected", msg->message_id);
        sd_replay_should_stop = true;  // Signal to stop further attempts
        return;
    }

    // Check if we've hit the batch limit
    if (sd_replay_messages_sent >= SD_REPLAY_MAX_MESSAGES_PER_BATCH) {
        ESP_LOGI(TAG, "[SD] Batch limit reached (%d messages) - pausing for next batch",
                 SD_REPLAY_MAX_MESSAGES_PER_BATCH);
        sd_replay_should_stop = true;
        return;
    }

    ESP_LOGI(TAG, "[SD] 📤 Replaying cached message ID %lu", msg->message_id);
    ESP_LOGI(TAG, "[SD]    Topic: %s", msg->topic);
    ESP_LOGI(TAG, "[SD]    Timestamp: %s", msg->timestamp);
    ESP_LOGI(TAG, "[SD]    Payload: %.100s%s", msg->payload, strlen(msg->payload) > 100 ? "..." : "");

    // Track this message in case we need to stop mid-send
    sd_replay_last_msg_id = msg->message_id;

    // Publish with QoS 0 for replay - faster and we still have the message on SD if it fails
    // QoS 1 was causing Azure IoT Hub to close connection due to unacknowledged messages
    int msg_id = esp_mqtt_client_publish(mqtt_client, msg->topic, msg->payload, 0, 0, 0);
    if (msg_id == -1) {
        ESP_LOGE(TAG, "[SD] ❌ Failed to publish replayed message %lu - stopping replay", msg->message_id);
        sd_replay_should_stop = true;  // Stop on publish failure
        return;
    }

    ESP_LOGI(TAG, "[SD] ✅ Successfully published replayed message %lu (MQTT msg_id: %d)",
             msg->message_id, msg_id);
    sd_replay_messages_sent++;

    // Wait for MQTT to process the message before removing from SD
    // This prevents losing messages if connection drops immediately after publish
    vTaskDelay(pdMS_TO_TICKS(SD_REPLAY_WAIT_FOR_ACK_MS));

    // Double-check connection is still alive after the wait
    if (!mqtt_connected) {
        ESP_LOGW(TAG, "[SD] MQTT disconnected after publish - NOT removing message %lu from SD",
                 msg->message_id);
        sd_replay_should_stop = true;
        return;  // Keep message on SD for retry
    }

    // Remove the message from SD card after successful publish and connection confirmed
    esp_err_t ret = sd_card_remove_message(msg->message_id);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "[SD] Failed to remove replayed message %lu from SD card", msg->message_id);
    }

    // Rate limiting delay between messages to prevent Azure IoT Hub throttling
    // Azure has limits on messages per second - 500ms minimum recommended
    vTaskDelay(pdMS_TO_TICKS(SD_REPLAY_DELAY_BETWEEN_MESSAGES_MS));

    // Final connection check after delay
    if (!mqtt_connected) {
        ESP_LOGW(TAG, "[SD] MQTT disconnected during replay delay - stopping");
        sd_replay_should_stop = true;
    }
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

            // Subscribe to Device Twin desired properties updates
            esp_mqtt_client_subscribe(mqtt_client, DEVICE_TWIN_DESIRED_TOPIC, 1);
            ESP_LOGI(TAG, "[TWIN] Subscribed to Device Twin desired properties: %s", DEVICE_TWIN_DESIRED_TOPIC);

            // Subscribe to Device Twin response topic (for GET requests)
            esp_mqtt_client_subscribe(mqtt_client, DEVICE_TWIN_RES_TOPIC, 1);
            ESP_LOGI(TAG, "[TWIN] Subscribed to Device Twin responses: %s", DEVICE_TWIN_RES_TOPIC);

            // Report current device properties to Azure
            report_device_twin_properties();

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
            // Note: Don't increment total_telemetry_sent here - it's done in send_telemetry()
            // This event fires for ALL publishes including Device Twin reports
            ESP_LOGI(TAG, "[OK] TELEMETRY PUBLISHED SUCCESSFULLY! msg_id=%d", event->msg_id);
            break;
            
        case MQTT_EVENT_DATA:
            ESP_LOGI(TAG, "[MSG] MQTT MESSAGE RECEIVED:");
            printf("TOPIC=%.*s\r\n", event->topic_len, event->topic);
            printf("DATA=%.*s\r\n", event->data_len, event->data);

            // Check if this is a Device Twin desired properties update
            if (event->topic_len > 0 && strncmp(event->topic, "$iothub/twin/PATCH/properties/desired", 37) == 0) {
                ESP_LOGI(TAG, "[TWIN] Device Twin desired properties update received");
                handle_device_twin_desired_properties(event->data, event->data_len);
                break;
            }

            // Check if this is a Device Twin response (for GET requests)
            if (event->topic_len > 0 && strncmp(event->topic, "$iothub/twin/res/", 17) == 0) {
                ESP_LOGI(TAG, "[TWIN] Device Twin response received");
                // Extract status code from topic: $iothub/twin/res/{status}/?$rid={request_id}
                int status_code = 0;
                if (sscanf(event->topic + 17, "%d", &status_code) == 1) {
                    if (status_code >= 200 && status_code < 300) {
                        ESP_LOGI(TAG, "[TWIN] Device Twin operation successful (status: %d)", status_code);
                    } else {
                        ESP_LOGW(TAG, "[TWIN] Device Twin operation failed (status: %d)", status_code);
                    }
                }
                break;
            }

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
                        // Support both "command" and "cmd" field names
                        cJSON *command = cJSON_GetObjectItem(root, "command");
                        if (!command) {
                            command = cJSON_GetObjectItem(root, "cmd");
                        }

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
                                    // Use web_config_start_server_only() to start AP+STA mode
                                    // This creates SoftAP (ModbusIoT-Config) for both WiFi and SIM modes
                                    web_config_start_server_only();
                                } else {
                                    web_config_stop();
                                }
                            }
                            else if (strcmp(cmd, "add_sensor") == 0) {
                                system_config_t *cfg = get_system_config();
                                if (cfg->sensor_count < 10) {
                                    cJSON *sensor = cJSON_GetObjectItem(root, "sensor");
                                    if (sensor) {
                                        int idx = cfg->sensor_count;

                                        // Initialize sensor with defaults
                                        memset(&cfg->sensors[idx], 0, sizeof(sensor_config_t));
                                        cfg->sensors[idx].enabled = true;
                                        cfg->sensors[idx].slave_id = 1;
                                        cfg->sensors[idx].baud_rate = 9600;
                                        cfg->sensors[idx].quantity = 2;
                                        cfg->sensors[idx].scale_factor = 1.0f;
                                        strncpy(cfg->sensors[idx].register_type, "HOLDING", 15);
                                        strncpy(cfg->sensors[idx].data_type, "FLOAT32", 31);
                                        strncpy(cfg->sensors[idx].byte_order, "ABCD", 15);
                                        strncpy(cfg->sensors[idx].parity, "none", 7);
                                        strncpy(cfg->sensors[idx].sensor_type, "Flow-Meter", 15);

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
                                        if ((item = cJSON_GetObjectItem(sensor, "register_type")))
                                            strncpy(cfg->sensors[idx].register_type, item->valuestring, 15);
                                        if ((item = cJSON_GetObjectItem(sensor, "byte_order")))
                                            strncpy(cfg->sensors[idx].byte_order, item->valuestring, 15);
                                        if ((item = cJSON_GetObjectItem(sensor, "parity")))
                                            strncpy(cfg->sensors[idx].parity, item->valuestring, 7);
                                        if ((item = cJSON_GetObjectItem(sensor, "sensor_type")))
                                            strncpy(cfg->sensors[idx].sensor_type, item->valuestring, 15);
                                        if ((item = cJSON_GetObjectItem(sensor, "scale_factor")))
                                            cfg->sensors[idx].scale_factor = (float)item->valuedouble;
                                        if ((item = cJSON_GetObjectItem(sensor, "baud_rate")))
                                            cfg->sensors[idx].baud_rate = item->valueint;
                                        if ((item = cJSON_GetObjectItem(sensor, "description")))
                                            strncpy(cfg->sensors[idx].description, item->valuestring, 63);
                                        if ((item = cJSON_GetObjectItem(sensor, "enabled")))
                                            cfg->sensors[idx].enabled = cJSON_IsTrue(item);

                                        // Apply sensor type presets (auto-configures register address, quantity, etc.)
                                        // Presets only apply to values not explicitly set in JSON
                                        apply_sensor_type_presets(&cfg->sensors[idx]);

                                        cfg->sensor_count++;
                                        config_save_to_nvs(cfg);

                                        ESP_LOGI(TAG, "[C2D] ✅ Sensor added: %s (index: %d, total: %d)",
                                                 cfg->sensors[idx].name, idx, cfg->sensor_count);
                                        ESP_LOGI(TAG, "[C2D]    Slave: %d, Addr: %d, Type: %s, Data: %s",
                                                 cfg->sensors[idx].slave_id, cfg->sensors[idx].register_address,
                                                 cfg->sensors[idx].register_type, cfg->sensors[idx].data_type);
                                    } else {
                                        ESP_LOGW(TAG, "[C2D] Missing 'sensor' object in JSON");
                                    }
                                } else {
                                    ESP_LOGW(TAG, "[C2D] Cannot add sensor - limit reached (10 max)");
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
                                            if ((item = cJSON_GetObjectItem(updates, "unit_id")))
                                                strncpy(cfg->sensors[idx].unit_id, item->valuestring, 15);
                                            if ((item = cJSON_GetObjectItem(updates, "slave_id")))
                                                cfg->sensors[idx].slave_id = item->valueint;
                                            if ((item = cJSON_GetObjectItem(updates, "register_address")))
                                                cfg->sensors[idx].register_address = item->valueint;
                                            if ((item = cJSON_GetObjectItem(updates, "quantity")))
                                                cfg->sensors[idx].quantity = item->valueint;
                                            if ((item = cJSON_GetObjectItem(updates, "data_type")))
                                                strncpy(cfg->sensors[idx].data_type, item->valuestring, 31);
                                            if ((item = cJSON_GetObjectItem(updates, "register_type")))
                                                strncpy(cfg->sensors[idx].register_type, item->valuestring, 15);
                                            if ((item = cJSON_GetObjectItem(updates, "byte_order")))
                                                strncpy(cfg->sensors[idx].byte_order, item->valuestring, 15);
                                            if ((item = cJSON_GetObjectItem(updates, "parity")))
                                                strncpy(cfg->sensors[idx].parity, item->valuestring, 7);
                                            if ((item = cJSON_GetObjectItem(updates, "sensor_type")))
                                                strncpy(cfg->sensors[idx].sensor_type, item->valuestring, 15);
                                            if ((item = cJSON_GetObjectItem(updates, "scale_factor")))
                                                cfg->sensors[idx].scale_factor = (float)item->valuedouble;
                                            if ((item = cJSON_GetObjectItem(updates, "baud_rate")))
                                                cfg->sensors[idx].baud_rate = item->valueint;
                                            if ((item = cJSON_GetObjectItem(updates, "description")))
                                                strncpy(cfg->sensors[idx].description, item->valuestring, 63);

                                            config_save_to_nvs(cfg);
                                            ESP_LOGI(TAG, "[C2D] ✅ Sensor %d updated: %s", idx, cfg->sensors[idx].name);
                                        }
                                    } else {
                                        ESP_LOGW(TAG, "[C2D] Invalid sensor index: %d (max: %d)", idx, cfg->sensor_count - 1);
                                    }
                                }
                            }
                            else if (strcmp(cmd, "list_sensors") == 0) {
                                system_config_t *cfg = get_system_config();
                                ESP_LOGI(TAG, "[C2D] ═══════════════════════════════════════════");
                                ESP_LOGI(TAG, "[C2D] SENSOR LIST (Total: %d)", cfg->sensor_count);
                                ESP_LOGI(TAG, "[C2D] ═══════════════════════════════════════════");
                                for (int i = 0; i < cfg->sensor_count; i++) {
                                    ESP_LOGI(TAG, "[C2D] [%d] %s (%s)", i, cfg->sensors[i].name,
                                             cfg->sensors[i].enabled ? "ENABLED" : "DISABLED");
                                    ESP_LOGI(TAG, "[C2D]     Slave: %d, Addr: %d, Qty: %d",
                                             cfg->sensors[i].slave_id, cfg->sensors[i].register_address,
                                             cfg->sensors[i].quantity);
                                    ESP_LOGI(TAG, "[C2D]     Type: %s, Data: %s, Order: %s",
                                             cfg->sensors[i].register_type, cfg->sensors[i].data_type,
                                             cfg->sensors[i].byte_order);
                                    ESP_LOGI(TAG, "[C2D]     Baud: %d, Scale: %.4f",
                                             cfg->sensors[i].baud_rate, cfg->sensors[i].scale_factor);
                                }
                                if (cfg->sensor_count == 0) {
                                    ESP_LOGI(TAG, "[C2D] No sensors configured");
                                }
                                ESP_LOGI(TAG, "[C2D] ═══════════════════════════════════════════");
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

                                    // Stop web server and WiFi in SIM mode to free memory and avoid routing conflicts
                                    system_config_t *cfg = get_system_config();
                                    if (web_server_running) {
                                        ESP_LOGW(TAG, "[C2D] Stopping web server for OTA...");
                                        web_config_stop();
                                        web_server_running = false;
                                    }
                                    if (cfg && cfg->network_mode == NETWORK_MODE_SIM && wifi_initialized_for_sim_mode) {
                                        ESP_LOGI(TAG, "[C2D] SIM mode - stopping WiFi for OTA...");
                                        esp_wifi_stop();
                                        esp_wifi_deinit();
                                        wifi_initialized_for_sim_mode = false;
                                    }
                                    vTaskDelay(pdMS_TO_TICKS(500));

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
                            // ==================== NEW COMMANDS ====================
                            else if (strcmp(cmd, "ping") == 0) {
                                // Simple health check - responds with pong
                                ESP_LOGI(TAG, "[C2D] PING received - device is alive!");
                            }
                            else if (strcmp(cmd, "get_config") == 0) {
                                // Return current configuration summary
                                system_config_t *cfg = get_system_config();
                                ESP_LOGI(TAG, "[C2D] === CURRENT CONFIGURATION ===");
                                ESP_LOGI(TAG, "[C2D] Telemetry interval: %d sec", cfg->telemetry_interval);
                                ESP_LOGI(TAG, "[C2D] Modbus retries: %d (delay: %d ms)", cfg->modbus_retry_count, cfg->modbus_retry_delay);
                                ESP_LOGI(TAG, "[C2D] Batch telemetry: %s", cfg->batch_telemetry ? "enabled" : "disabled");
                                ESP_LOGI(TAG, "[C2D] Sensor count: %d", cfg->sensor_count);
                                ESP_LOGI(TAG, "[C2D] Network mode: %s", cfg->network_mode == NETWORK_MODE_SIM ? "SIM" : "WiFi");
                                // Report via Device Twin
                                report_device_twin_properties();
                            }
                            else if (strcmp(cmd, "get_heap") == 0) {
                                // Memory status
                                ESP_LOGI(TAG, "[C2D] === HEAP MEMORY STATUS ===");
                                ESP_LOGI(TAG, "[C2D] Free heap: %lu bytes", (unsigned long)esp_get_free_heap_size());
                                ESP_LOGI(TAG, "[C2D] Min free heap: %lu bytes", (unsigned long)esp_get_minimum_free_heap_size());
                                ESP_LOGI(TAG, "[C2D] Largest free block: %lu bytes", (unsigned long)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
                            }
                            else if (strcmp(cmd, "get_network") == 0) {
                                // Network status
                                system_config_t *cfg = get_system_config();
                                ESP_LOGI(TAG, "[C2D] === NETWORK STATUS ===");
                                ESP_LOGI(TAG, "[C2D] Mode: %s", cfg->network_mode == NETWORK_MODE_SIM ? "4G/LTE (SIM)" : "WiFi");
                                ESP_LOGI(TAG, "[C2D] MQTT connected: %s", mqtt_connected ? "YES" : "NO");
                                ESP_LOGI(TAG, "[C2D] Total telemetry sent: %lu", (unsigned long)total_telemetry_sent);
                                ESP_LOGI(TAG, "[C2D] MQTT reconnects: %lu", (unsigned long)mqtt_reconnect_count);
                                if (cfg->network_mode == NETWORK_MODE_WIFI) {
                                    ESP_LOGI(TAG, "[C2D] WiFi SSID: %s", cfg->wifi_ssid);
                                }
                            }
                            else if (strcmp(cmd, "get_sensors") == 0) {
                                // List all configured sensors
                                system_config_t *cfg = get_system_config();
                                ESP_LOGI(TAG, "[C2D] === CONFIGURED SENSORS (%d) ===", cfg->sensor_count);
                                for (int i = 0; i < cfg->sensor_count; i++) {
                                    sensor_config_t *s = &cfg->sensors[i];
                                    if (s->enabled) {
                                        ESP_LOGI(TAG, "[C2D] [%d] %s (ID:%d, Reg:%d, Type:%s)",
                                                 i, s->name, s->slave_id, s->register_address, s->sensor_type);
                                    }
                                }
                            }
                            else if (strcmp(cmd, "set_modbus_retry") == 0) {
                                // Set Modbus retry settings
                                cJSON *count = cJSON_GetObjectItem(root, "count");
                                cJSON *delay = cJSON_GetObjectItem(root, "delay");
                                system_config_t *cfg = get_system_config();
                                bool changed = false;
                                if (count && cJSON_IsNumber(count)) {
                                    int new_count = count->valueint;
                                    if (new_count >= 0 && new_count <= 3) {
                                        cfg->modbus_retry_count = new_count;
                                        changed = true;
                                        ESP_LOGI(TAG, "[C2D] Modbus retry count set to %d", new_count);
                                    }
                                }
                                if (delay && cJSON_IsNumber(delay)) {
                                    int new_delay = delay->valueint;
                                    if (new_delay >= 10 && new_delay <= 500) {
                                        cfg->modbus_retry_delay = new_delay;
                                        changed = true;
                                        ESP_LOGI(TAG, "[C2D] Modbus retry delay set to %d ms", new_delay);
                                    }
                                }
                                if (changed) {
                                    config_save_to_nvs(cfg);
                                    report_device_twin_properties();
                                }
                            }
                            else if (strcmp(cmd, "set_batch_mode") == 0) {
                                // Enable/disable batch telemetry
                                cJSON *enabled = cJSON_GetObjectItem(root, "enabled");
                                if (enabled && cJSON_IsBool(enabled)) {
                                    system_config_t *cfg = get_system_config();
                                    cfg->batch_telemetry = cJSON_IsTrue(enabled);
                                    config_save_to_nvs(cfg);
                                    ESP_LOGI(TAG, "[C2D] Batch telemetry %s", cfg->batch_telemetry ? "enabled" : "disabled");
                                    report_device_twin_properties();
                                }
                            }
                            else if (strcmp(cmd, "sync_time") == 0) {
                                // Trigger NTP time sync
                                ESP_LOGI(TAG, "[C2D] Triggering NTP time sync...");
                                esp_sntp_restart();
                                ESP_LOGI(TAG, "[C2D] NTP sync initiated");
                            }
                            else if (strcmp(cmd, "read_sensor") == 0) {
                                // Read a specific sensor immediately
                                cJSON *sensor_idx = cJSON_GetObjectItem(root, "index");
                                if (sensor_idx && cJSON_IsNumber(sensor_idx)) {
                                    int idx = sensor_idx->valueint;
                                    system_config_t *cfg = get_system_config();
                                    if (idx >= 0 && idx < cfg->sensor_count && cfg->sensors[idx].enabled) {
                                        ESP_LOGI(TAG, "[C2D] Reading sensor %d: %s", idx, cfg->sensors[idx].name);
                                        sensor_reading_t reading;
                                        esp_err_t result = sensor_read_single(&cfg->sensors[idx], &reading);
                                        if (result == ESP_OK && reading.valid) {
                                            ESP_LOGI(TAG, "[C2D] Sensor %s = %.4f",
                                                     cfg->sensors[idx].name, reading.value);
                                        } else {
                                            ESP_LOGW(TAG, "[C2D] Sensor read failed for %s", cfg->sensors[idx].name);
                                        }
                                    } else {
                                        ESP_LOGW(TAG, "[C2D] Invalid sensor index: %d", idx);
                                    }
                                }
                            }
                            else if (strcmp(cmd, "reset_stats") == 0) {
                                // Reset telemetry statistics
                                total_telemetry_sent = 0;
                                mqtt_reconnect_count = 0;
                                telemetry_failure_count = 0;
                                ESP_LOGI(TAG, "[C2D] Statistics reset to zero");
                            }
                            else if (strcmp(cmd, "led_test") == 0) {
                                // Test all LEDs
                                ESP_LOGI(TAG, "[C2D] Testing LEDs...");
                                gpio_set_level(MQTT_LED_GPIO_PIN, 1);
                                gpio_set_level(WEBSERVER_LED_GPIO_PIN, 1);
                                gpio_set_level(SENSOR_LED_GPIO_PIN, 1);
                                vTaskDelay(pdMS_TO_TICKS(2000));
                                gpio_set_level(MQTT_LED_GPIO_PIN, 0);
                                gpio_set_level(WEBSERVER_LED_GPIO_PIN, 0);
                                gpio_set_level(SENSOR_LED_GPIO_PIN, 0);
                                ESP_LOGI(TAG, "[C2D] LED test complete");
                            }
                            else if (strcmp(cmd, "factory_reset") == 0) {
                                // Factory reset (requires confirmation key)
                                cJSON *confirm = cJSON_GetObjectItem(root, "confirm");
                                if (confirm && cJSON_IsString(confirm) && strcmp(confirm->valuestring, "CONFIRM_RESET") == 0) {
                                    ESP_LOGW(TAG, "[C2D] FACTORY RESET INITIATED!");
                                    config_reset_to_defaults();
                                    ESP_LOGW(TAG, "[C2D] Configuration reset. Restarting in 3 seconds...");
                                    vTaskDelay(pdMS_TO_TICKS(3000));
                                    esp_restart();
                                } else {
                                    ESP_LOGW(TAG, "[C2D] Factory reset requires confirm: \"CONFIRM_RESET\"");
                                }
                            }
                            else if (strcmp(cmd, "help") == 0) {
                                // List available commands
                                ESP_LOGI(TAG, "[C2D] === AVAILABLE COMMANDS ===");
                                ESP_LOGI(TAG, "[C2D] ping - Health check");
                                ESP_LOGI(TAG, "[C2D] restart - Restart device");
                                ESP_LOGI(TAG, "[C2D] get_status - Get device status");
                                ESP_LOGI(TAG, "[C2D] get_config - Get current configuration");
                                ESP_LOGI(TAG, "[C2D] get_heap - Get memory status");
                                ESP_LOGI(TAG, "[C2D] get_network - Get network status");
                                ESP_LOGI(TAG, "[C2D] get_sensors - List configured sensors");
                                ESP_LOGI(TAG, "[C2D] set_telemetry_interval {interval} - Set interval (30-3600)");
                                ESP_LOGI(TAG, "[C2D] set_modbus_retry {count, delay} - Set retry settings");
                                ESP_LOGI(TAG, "[C2D] set_batch_mode {enabled} - Enable/disable batch mode");
                                ESP_LOGI(TAG, "[C2D] read_sensor {index} - Read specific sensor");
                                ESP_LOGI(TAG, "[C2D] sync_time - Trigger NTP sync");
                                ESP_LOGI(TAG, "[C2D] reset_stats - Reset statistics");
                                ESP_LOGI(TAG, "[C2D] led_test - Test all LEDs");
                                ESP_LOGI(TAG, "[C2D] toggle_webserver - Toggle web server");
                                ESP_LOGI(TAG, "[C2D] factory_reset {confirm} - Reset to defaults");
                                ESP_LOGI(TAG, "[C2D] OTA: ota_update, ota_status, ota_cancel, ota_confirm, ota_reboot");
                            }
                            // ==================== END NEW COMMANDS ====================
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
    esp_err_t ret = sensor_read_all_configured(readings, 10, &actual_count);
    
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
            ESP_LOGI(TAG, "[BATCH] Sending sensors with simple flat JSON format");

            // Get current timestamp for all sensors
            time_t now;
            struct tm timeinfo;
            char timestamp[32];
            time(&now);
            gmtime_r(&now, &timeinfo);
            strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%SZ", &timeinfo);

            // Send each sensor as a separate flat JSON message
            // Format: {"unit_id":"ZEST001","type":"FLOW","consumption":"50.000","created_on":"2025-12-20T07:11:16Z"}
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

                    // Determine value key and type based on sensor type
                    const char* value_key = "value";
                    const char* type_value = "SENSOR";
                    if (strcasecmp(matching_sensor->sensor_type, "Level") == 0 ||
                        strcasecmp(matching_sensor->sensor_type, "Radar Level") == 0 ||
                        strcasecmp(matching_sensor->sensor_type, "Panda_Level") == 0) {
                        value_key = "level_filled";
                        type_value = "LEVEL";
                    } else if (strcasecmp(matching_sensor->sensor_type, "Flow-Meter") == 0 ||
                               strcasecmp(matching_sensor->sensor_type, "ZEST") == 0 ||
                               strcasecmp(matching_sensor->sensor_type, "Panda_EMF") == 0 ||
                               strcasecmp(matching_sensor->sensor_type, "Panda_USM") == 0 ||
                               strcasecmp(matching_sensor->sensor_type, "Dailian_EMF") == 0 ||
                               strcasecmp(matching_sensor->sensor_type, "Clampon") == 0) {
                        value_key = "consumption";
                        type_value = "FLOW";
                    } else if (strcasecmp(matching_sensor->sensor_type, "RAINGAUGE") == 0) {
                        value_key = "raingauge";
                        type_value = "RAINGAUGE";
                    } else if (strcasecmp(matching_sensor->sensor_type, "BOREWELL") == 0) {
                        value_key = "borewell";
                        type_value = "BOREWELL";
                    } else if (strcasecmp(matching_sensor->sensor_type, "ENERGY") == 0) {
                        value_key = "ene_con_hex";
                        type_value = "ENERGY";
                    } else if (strcasecmp(matching_sensor->sensor_type, "QUALITY") == 0) {
                        value_key = "value";
                        type_value = "QUALITY";
                    }

                    // Build simple flat JSON format
                    // Format: {"unit_id":"ZEST001","type":"FLOW","consumption":"50.000","created_on":"2025-12-20T07:11:16Z"}
                    char sensor_payload[512];
                    snprintf(sensor_payload, sizeof(sensor_payload),
                        "{\"unit_id\":\"%s\",\"type\":\"%s\",\"%s\":\"%.3f\",\"created_on\":\"%s\"}",
                        matching_sensor->unit_id, type_value, value_key, readings[i].value, timestamp);

                    // Send via MQTT if connected
                    if (mqtt_connected && mqtt_client != NULL) {
                        char sensor_topic[256];
                        snprintf(sensor_topic, sizeof(sensor_topic),
                                 "devices/%s/messages/events/", config->azure_device_id);

                        int msg_id = esp_mqtt_client_publish(
                            mqtt_client,
                            sensor_topic,
                            sensor_payload,
                            strlen(sensor_payload),
                            0,  // QoS 0
                            0   // No retain
                        );

                        if (msg_id >= 0) {
                            valid_sensors++;
                            ESP_LOGI(TAG, "[MQTT] Sent sensor %s: %s (msg_id=%d)",
                                     matching_sensor->unit_id, sensor_payload, msg_id);
                        } else {
                            ESP_LOGW(TAG, "[WARN] Failed to publish sensor %s", matching_sensor->unit_id);
                        }
                    }

                    // Store last payload for logging
                    strncpy(payload, sensor_payload, payload_size - 1);
                    payload[payload_size - 1] = '\0';
                }
            }

            ESP_LOGI(TAG, "[OK] Sent %d sensors with flat JSON format", valid_sensors);
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

__attribute__((unused)) static esp_err_t read_configured_sensors_data(void) {
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

// =============================================================================
// DEVICE TWIN - Handle desired properties from Azure IoT Hub
// =============================================================================

// Handle incoming Device Twin desired properties
static void handle_device_twin_desired_properties(const char *data, int data_len) {
    if (!data || data_len <= 0) {
        ESP_LOGW(TAG, "[TWIN] Empty desired properties received");
        return;
    }

    // Parse JSON payload
    char *json_str = (char *)malloc(data_len + 1);
    if (!json_str) {
        ESP_LOGE(TAG, "[TWIN] Failed to allocate memory for JSON parsing");
        return;
    }
    memcpy(json_str, data, data_len);
    json_str[data_len] = '\0';

    ESP_LOGI(TAG, "[TWIN] Parsing desired properties: %s", json_str);

    cJSON *root = cJSON_Parse(json_str);
    if (!root) {
        ESP_LOGE(TAG, "[TWIN] Failed to parse JSON: %s", json_str);
        free(json_str);
        return;
    }

    system_config_t *config = get_system_config();
    bool config_changed = false;

    // Handle $version (Azure sends this with each update)
    cJSON *version = cJSON_GetObjectItem(root, "$version");
    if (version && cJSON_IsNumber(version)) {
        int new_version = version->valueint;
        if (new_version <= config->device_twin_version) {
            ESP_LOGI(TAG, "[TWIN] Version %d already applied (current: %d), skipping",
                     new_version, config->device_twin_version);
            cJSON_Delete(root);
            free(json_str);
            return;
        }
        config->device_twin_version = new_version;
        ESP_LOGI(TAG, "[TWIN] Applying version %d", new_version);
    }

    // Process telemetry_interval
    cJSON *interval = cJSON_GetObjectItem(root, "telemetry_interval");
    if (interval && cJSON_IsNumber(interval)) {
        int new_interval = interval->valueint;
        if (new_interval >= 30 && new_interval <= 3600) {
            if (config->telemetry_interval != new_interval) {
                config->telemetry_interval = new_interval;
                config_changed = true;
                ESP_LOGI(TAG, "[TWIN] telemetry_interval updated to %d seconds", new_interval);
            }
        } else {
            ESP_LOGW(TAG, "[TWIN] Invalid telemetry_interval: %d (must be 30-3600)", new_interval);
        }
    }

    // Process modbus_retry_count
    cJSON *retry_count = cJSON_GetObjectItem(root, "modbus_retry_count");
    if (retry_count && cJSON_IsNumber(retry_count)) {
        int new_count = retry_count->valueint;
        if (new_count >= 0 && new_count <= 3) {
            if (config->modbus_retry_count != new_count) {
                config->modbus_retry_count = new_count;
                config_changed = true;
                ESP_LOGI(TAG, "[TWIN] modbus_retry_count updated to %d", new_count);
            }
        } else {
            ESP_LOGW(TAG, "[TWIN] Invalid modbus_retry_count: %d (must be 0-3)", new_count);
        }
    }

    // Process modbus_retry_delay
    cJSON *retry_delay = cJSON_GetObjectItem(root, "modbus_retry_delay");
    if (retry_delay && cJSON_IsNumber(retry_delay)) {
        int new_delay = retry_delay->valueint;
        if (new_delay >= 10 && new_delay <= 500) {
            if (config->modbus_retry_delay != new_delay) {
                config->modbus_retry_delay = new_delay;
                config_changed = true;
                ESP_LOGI(TAG, "[TWIN] modbus_retry_delay updated to %d ms", new_delay);
            }
        } else {
            ESP_LOGW(TAG, "[TWIN] Invalid modbus_retry_delay: %d (must be 10-500)", new_delay);
        }
    }

    // Process batch_telemetry
    cJSON *batch = cJSON_GetObjectItem(root, "batch_telemetry");
    if (batch && cJSON_IsBool(batch)) {
        bool new_batch = cJSON_IsTrue(batch);
        if (config->batch_telemetry != new_batch) {
            config->batch_telemetry = new_batch;
            config_changed = true;
            ESP_LOGI(TAG, "[TWIN] batch_telemetry updated to %s", new_batch ? "true" : "false");
        }
    }

    // Process web_server_enabled - toggle web server remotely
    // But don't start if OTA update will be triggered (need memory for OTA)
    cJSON *web_server = cJSON_GetObjectItem(root, "web_server_enabled");
    if (web_server && cJSON_IsBool(web_server)) {
        bool should_enable = cJSON_IsTrue(web_server);

        // Check if OTA will be triggered in this same update
        cJSON *ota_url_check = cJSON_GetObjectItem(root, "ota_url");
        cJSON *ota_enable_check = cJSON_GetObjectItem(root, "ota_enabled");
        bool ota_will_trigger = ota_url_check && cJSON_IsString(ota_url_check) &&
                                strlen(ota_url_check->valuestring) > 10 &&
                                strcmp(ota_url_check->valuestring, ota_url) != 0 &&  // New URL
                                (!ota_enable_check || cJSON_IsTrue(ota_enable_check));  // OTA enabled

        if (should_enable && ota_will_trigger) {
            ESP_LOGW(TAG, "[TWIN] Skipping web server start - OTA update will be triggered");
        } else if (should_enable != web_server_running) {
            if (should_enable) {
                ESP_LOGI(TAG, "[TWIN] Starting web server...");
                esp_err_t ret = web_config_start_server_only();
                if (ret == ESP_OK) {
                    web_server_running = true;
                    ESP_LOGI(TAG, "[TWIN] Web server STARTED");
                } else {
                    ESP_LOGE(TAG, "[TWIN] Failed to start web server: %s", esp_err_to_name(ret));
                }
            } else {
                ESP_LOGI(TAG, "[TWIN] Stopping web server...");
                web_config_stop();
                web_server_running = false;
                ESP_LOGI(TAG, "[TWIN] Web server STOPPED");
            }
            config_changed = true;
        }
    }

    // Process maintenance_mode - pause all telemetry when true
    cJSON *maint_mode = cJSON_GetObjectItem(root, "maintenance_mode");
    if (maint_mode && cJSON_IsBool(maint_mode)) {
        bool new_mode = cJSON_IsTrue(maint_mode);
        if (maintenance_mode != new_mode) {
            maintenance_mode = new_mode;
            config_changed = true;
            if (maintenance_mode) {
                ESP_LOGW(TAG, "[TWIN] MAINTENANCE MODE ENABLED - Telemetry paused");
            } else {
                ESP_LOGI(TAG, "[TWIN] Maintenance mode disabled - Telemetry resumed");
            }
        }
    }

    // Process reboot_device - one-shot trigger for remote reboot
    cJSON *reboot = cJSON_GetObjectItem(root, "reboot_device");
    if (reboot && cJSON_IsBool(reboot) && cJSON_IsTrue(reboot)) {
        ESP_LOGW(TAG, "[TWIN] REMOTE REBOOT REQUESTED");
        ESP_LOGI(TAG, "[TWIN] Device will reboot in 3 seconds...");

        // Report the reboot acknowledgment before rebooting
        cJSON *ack = cJSON_CreateObject();
        if (ack) {
            cJSON_AddBoolToObject(ack, "reboot_device", false);  // Reset to false
            cJSON_AddStringToObject(ack, "reboot_status", "rebooting");
            char *ack_str = cJSON_PrintUnformatted(ack);
            if (ack_str) {
                device_twin_request_id++;
                snprintf(device_twin_reported_topic, sizeof(device_twin_reported_topic),
                         "$iothub/twin/PATCH/properties/reported/?$rid=%d", device_twin_request_id);
                esp_mqtt_client_publish(mqtt_client, device_twin_reported_topic, ack_str, strlen(ack_str), 1, 0);
                free(ack_str);
            }
            cJSON_Delete(ack);
        }

        vTaskDelay(pdMS_TO_TICKS(3000));  // Wait 3 seconds for message to be sent
        esp_restart();
    }

    // Process ota_enabled - allow/block OTA updates
    cJSON *ota_enable = cJSON_GetObjectItem(root, "ota_enabled");
    if (ota_enable && cJSON_IsBool(ota_enable)) {
        bool new_ota_enabled = cJSON_IsTrue(ota_enable);
        if (ota_enabled != new_ota_enabled) {
            ota_enabled = new_ota_enabled;
            config_changed = true;
            ESP_LOGI(TAG, "[TWIN] ota_enabled updated to %s", ota_enabled ? "true" : "false");
        }
    }

    // Process ota_url - remote OTA firmware URL
    cJSON *ota_url_json = cJSON_GetObjectItem(root, "ota_url");
    if (ota_url_json && cJSON_IsString(ota_url_json)) {
        const char *new_url = ota_url_json->valuestring;
        if (new_url && strlen(new_url) < sizeof(ota_url)) {
            if (strcmp(ota_url, new_url) != 0) {
                strncpy(ota_url, new_url, sizeof(ota_url) - 1);
                ota_url[sizeof(ota_url) - 1] = '\0';
                config_changed = true;
                ESP_LOGI(TAG, "[TWIN] ota_url updated to: %s", ota_url);

                // If OTA is enabled and URL is set, trigger OTA update
                if (ota_enabled && strlen(ota_url) > 10) {
                    ESP_LOGI(TAG, "[TWIN] OTA update triggered from Device Twin");
                    ESP_LOGI(TAG, "[TWIN] Starting OTA from: %s", ota_url);

                    // Check heap and stop web server if needed - OTA requires ~50KB for HTTPS/TLS
                    size_t free_heap = esp_get_free_heap_size();
                    ESP_LOGI(TAG, "[TWIN] Current free heap: %u bytes", free_heap);

                    if (free_heap < 60000 || web_server_running) {
                        if (web_server_running) {
                            ESP_LOGW(TAG, "[TWIN] Stopping web server to free memory for OTA...");
                            web_config_stop();
                            web_server_running = false;
                        }
                        // In SIM mode, also stop WiFi completely to avoid interface conflicts
                        if (config->network_mode == NETWORK_MODE_SIM && wifi_initialized_for_sim_mode) {
                            ESP_LOGI(TAG, "[TWIN] SIM mode - stopping WiFi to avoid routing conflicts...");
                            esp_wifi_stop();
                            esp_wifi_deinit();
                            wifi_initialized_for_sim_mode = false;
                            ESP_LOGI(TAG, "[TWIN] WiFi stopped and deinitialized for OTA");
                        }
                        // Wait for memory to be freed
                        vTaskDelay(pdMS_TO_TICKS(1000));
                        free_heap = esp_get_free_heap_size();
                        ESP_LOGI(TAG, "[TWIN] After cleanup - Free heap: %u bytes", free_heap);
                    }

                    // Final heap check before starting OTA
                    if (free_heap < 40000) {
                        ESP_LOGE(TAG, "[TWIN] Not enough memory for OTA! Need 40KB, have %u", free_heap);
                        // Report failure
                        cJSON *fail_json = cJSON_CreateObject();
                        if (fail_json) {
                            cJSON_AddStringToObject(fail_json, "ota_status", "failed");
                            cJSON_AddStringToObject(fail_json, "ota_error", "Insufficient memory");
                            char *fail_str = cJSON_PrintUnformatted(fail_json);
                            if (fail_str) {
                                device_twin_request_id++;
                                snprintf(device_twin_reported_topic, sizeof(device_twin_reported_topic),
                                         "$iothub/twin/PATCH/properties/reported/?$rid=%d", device_twin_request_id);
                                esp_mqtt_client_publish(mqtt_client, device_twin_reported_topic, fail_str, strlen(fail_str), 1, 0);
                                free(fail_str);
                            }
                            cJSON_Delete(fail_json);
                        }
                        // Skip OTA, continue with rest of processing
                        goto skip_ota;
                    }

                    // Report OTA starting status
                    cJSON *ota_status_json = cJSON_CreateObject();
                    if (ota_status_json) {
                        cJSON_AddStringToObject(ota_status_json, "ota_status", "downloading");
                        char *status_str = cJSON_PrintUnformatted(ota_status_json);
                        if (status_str) {
                            device_twin_request_id++;
                            snprintf(device_twin_reported_topic, sizeof(device_twin_reported_topic),
                                     "$iothub/twin/PATCH/properties/reported/?$rid=%d", device_twin_request_id);
                            esp_mqtt_client_publish(mqtt_client, device_twin_reported_topic, status_str, strlen(status_str), 1, 0);
                            free(status_str);
                        }
                        cJSON_Delete(ota_status_json);
                    }

                    // Start OTA update (runs in background, reboots on success)
                    esp_err_t ota_result = ota_start_update(ota_url, "remote");
                    if (ota_result != ESP_OK) {
                        ESP_LOGE(TAG, "[TWIN] OTA update failed to start: %s", esp_err_to_name(ota_result));
                        // Report OTA failure
                        cJSON *ota_fail = cJSON_CreateObject();
                        if (ota_fail) {
                            cJSON_AddStringToObject(ota_fail, "ota_status", "failed");
                            cJSON_AddStringToObject(ota_fail, "ota_error", esp_err_to_name(ota_result));
                            char *fail_str = cJSON_PrintUnformatted(ota_fail);
                            if (fail_str) {
                                device_twin_request_id++;
                                snprintf(device_twin_reported_topic, sizeof(device_twin_reported_topic),
                                         "$iothub/twin/PATCH/properties/reported/?$rid=%d", device_twin_request_id);
                                esp_mqtt_client_publish(mqtt_client, device_twin_reported_topic, fail_str, strlen(fail_str), 1, 0);
                                free(fail_str);
                            }
                            cJSON_Delete(ota_fail);
                        }
                    }
                }
skip_ota:       // Label for skipping OTA when memory is insufficient
                (void)0;  // Empty statement after label
            }
        } else {
            ESP_LOGW(TAG, "[TWIN] ota_url too long or empty");
        }
    }

    // Process sensors array - remote sensor configuration via Device Twin
    cJSON *sensors_array = cJSON_GetObjectItem(root, "sensors");
    if (sensors_array && cJSON_IsArray(sensors_array)) {
        int sensor_count = cJSON_GetArraySize(sensors_array);
        if (sensor_count > 10) {
            ESP_LOGW(TAG, "[TWIN] Sensor count %d exceeds maximum (10), limiting", sensor_count);
            sensor_count = 10;
        }

        ESP_LOGI(TAG, "[TWIN] Processing %d sensors from Device Twin", sensor_count);

        // Clear existing sensors first
        memset(config->sensors, 0, sizeof(config->sensors));
        config->sensor_count = 0;

        for (int i = 0; i < sensor_count; i++) {
            cJSON *sensor_obj = cJSON_GetArrayItem(sensors_array, i);
            if (!sensor_obj || !cJSON_IsObject(sensor_obj)) {
                ESP_LOGW(TAG, "[TWIN] Sensor %d is not a valid object, skipping", i);
                continue;
            }

            sensor_config_t *sensor = &config->sensors[config->sensor_count];
            memset(sensor, 0, sizeof(sensor_config_t));

            // Parse enabled (default: true)
            cJSON *enabled = cJSON_GetObjectItem(sensor_obj, "enabled");
            sensor->enabled = (!enabled || cJSON_IsTrue(enabled));

            // Parse required string fields
            cJSON *name = cJSON_GetObjectItem(sensor_obj, "name");
            if (name && cJSON_IsString(name)) {
                strncpy(sensor->name, name->valuestring, sizeof(sensor->name) - 1);
            } else {
                snprintf(sensor->name, sizeof(sensor->name), "Sensor_%d", i + 1);
            }

            cJSON *unit_id = cJSON_GetObjectItem(sensor_obj, "unit_id");
            if (unit_id && cJSON_IsString(unit_id)) {
                strncpy(sensor->unit_id, unit_id->valuestring, sizeof(sensor->unit_id) - 1);
            }

            // Parse Modbus configuration
            cJSON *slave_id = cJSON_GetObjectItem(sensor_obj, "slave_id");
            sensor->slave_id = (slave_id && cJSON_IsNumber(slave_id)) ? slave_id->valueint : 1;

            cJSON *baud_rate = cJSON_GetObjectItem(sensor_obj, "baud_rate");
            sensor->baud_rate = (baud_rate && cJSON_IsNumber(baud_rate)) ? baud_rate->valueint : 9600;

            cJSON *parity = cJSON_GetObjectItem(sensor_obj, "parity");
            if (parity && cJSON_IsString(parity)) {
                strncpy(sensor->parity, parity->valuestring, sizeof(sensor->parity) - 1);
            } else {
                strcpy(sensor->parity, "none");
            }

            cJSON *register_address = cJSON_GetObjectItem(sensor_obj, "register_address");
            sensor->register_address = (register_address && cJSON_IsNumber(register_address)) ? register_address->valueint : 0;

            cJSON *quantity = cJSON_GetObjectItem(sensor_obj, "quantity");
            sensor->quantity = (quantity && cJSON_IsNumber(quantity)) ? quantity->valueint : 1;

            cJSON *data_type = cJSON_GetObjectItem(sensor_obj, "data_type");
            if (data_type && cJSON_IsString(data_type)) {
                strncpy(sensor->data_type, data_type->valuestring, sizeof(sensor->data_type) - 1);
            } else {
                strcpy(sensor->data_type, "UINT16");
            }

            cJSON *register_type = cJSON_GetObjectItem(sensor_obj, "register_type");
            if (register_type && cJSON_IsString(register_type)) {
                strncpy(sensor->register_type, register_type->valuestring, sizeof(sensor->register_type) - 1);
            } else {
                strcpy(sensor->register_type, "HOLDING");
            }

            cJSON *scale_factor = cJSON_GetObjectItem(sensor_obj, "scale_factor");
            sensor->scale_factor = (scale_factor && cJSON_IsNumber(scale_factor)) ? (float)scale_factor->valuedouble : 1.0f;

            cJSON *byte_order = cJSON_GetObjectItem(sensor_obj, "byte_order");
            if (byte_order && cJSON_IsString(byte_order)) {
                strncpy(sensor->byte_order, byte_order->valuestring, sizeof(sensor->byte_order) - 1);
            } else {
                strcpy(sensor->byte_order, "BIG_ENDIAN");
            }

            cJSON *description = cJSON_GetObjectItem(sensor_obj, "description");
            if (description && cJSON_IsString(description)) {
                strncpy(sensor->description, description->valuestring, sizeof(sensor->description) - 1);
            }

            // Parse sensor type and type-specific fields
            cJSON *sensor_type = cJSON_GetObjectItem(sensor_obj, "sensor_type");
            if (sensor_type && cJSON_IsString(sensor_type)) {
                strncpy(sensor->sensor_type, sensor_type->valuestring, sizeof(sensor->sensor_type) - 1);
            } else {
                strcpy(sensor->sensor_type, "Flow-Meter");
            }

            cJSON *sensor_height = cJSON_GetObjectItem(sensor_obj, "sensor_height");
            sensor->sensor_height = (sensor_height && cJSON_IsNumber(sensor_height)) ? (float)sensor_height->valuedouble : 0.0f;

            cJSON *max_water_level = cJSON_GetObjectItem(sensor_obj, "max_water_level");
            sensor->max_water_level = (max_water_level && cJSON_IsNumber(max_water_level)) ? (float)max_water_level->valuedouble : 0.0f;

            cJSON *meter_type = cJSON_GetObjectItem(sensor_obj, "meter_type");
            if (meter_type && cJSON_IsString(meter_type)) {
                strncpy(sensor->meter_type, meter_type->valuestring, sizeof(sensor->meter_type) - 1);
            }

            // Parse sub-sensors for water quality sensors
            cJSON *sub_sensors = cJSON_GetObjectItem(sensor_obj, "sub_sensors");
            if (sub_sensors && cJSON_IsArray(sub_sensors)) {
                int sub_count = cJSON_GetArraySize(sub_sensors);
                if (sub_count > 8) sub_count = 8;
                sensor->sub_sensor_count = 0;

                for (int j = 0; j < sub_count; j++) {
                    cJSON *sub_obj = cJSON_GetArrayItem(sub_sensors, j);
                    if (!sub_obj || !cJSON_IsObject(sub_obj)) continue;

                    sub_sensor_t *sub = &sensor->sub_sensors[sensor->sub_sensor_count];
                    memset(sub, 0, sizeof(sub_sensor_t));

                    cJSON *sub_enabled = cJSON_GetObjectItem(sub_obj, "enabled");
                    sub->enabled = (!sub_enabled || cJSON_IsTrue(sub_enabled));

                    cJSON *param_name = cJSON_GetObjectItem(sub_obj, "parameter_name");
                    if (param_name && cJSON_IsString(param_name)) {
                        strncpy(sub->parameter_name, param_name->valuestring, sizeof(sub->parameter_name) - 1);
                    }

                    cJSON *json_key = cJSON_GetObjectItem(sub_obj, "json_key");
                    if (json_key && cJSON_IsString(json_key)) {
                        strncpy(sub->json_key, json_key->valuestring, sizeof(sub->json_key) - 1);
                    }

                    cJSON *sub_slave = cJSON_GetObjectItem(sub_obj, "slave_id");
                    sub->slave_id = (sub_slave && cJSON_IsNumber(sub_slave)) ? sub_slave->valueint : sensor->slave_id;

                    cJSON *sub_reg = cJSON_GetObjectItem(sub_obj, "register_address");
                    sub->register_address = (sub_reg && cJSON_IsNumber(sub_reg)) ? sub_reg->valueint : 0;

                    cJSON *sub_qty = cJSON_GetObjectItem(sub_obj, "quantity");
                    sub->quantity = (sub_qty && cJSON_IsNumber(sub_qty)) ? sub_qty->valueint : 1;

                    cJSON *sub_dtype = cJSON_GetObjectItem(sub_obj, "data_type");
                    if (sub_dtype && cJSON_IsString(sub_dtype)) {
                        strncpy(sub->data_type, sub_dtype->valuestring, sizeof(sub->data_type) - 1);
                    } else {
                        strcpy(sub->data_type, "FLOAT32");
                    }

                    cJSON *sub_regtype = cJSON_GetObjectItem(sub_obj, "register_type");
                    if (sub_regtype && cJSON_IsString(sub_regtype)) {
                        strncpy(sub->register_type, sub_regtype->valuestring, sizeof(sub->register_type) - 1);
                    } else {
                        strcpy(sub->register_type, "HOLDING");
                    }

                    cJSON *sub_scale = cJSON_GetObjectItem(sub_obj, "scale_factor");
                    sub->scale_factor = (sub_scale && cJSON_IsNumber(sub_scale)) ? (float)sub_scale->valuedouble : 1.0f;

                    cJSON *sub_byte = cJSON_GetObjectItem(sub_obj, "byte_order");
                    if (sub_byte && cJSON_IsString(sub_byte)) {
                        strncpy(sub->byte_order, sub_byte->valuestring, sizeof(sub->byte_order) - 1);
                    } else {
                        strcpy(sub->byte_order, "BIG_ENDIAN");
                    }

                    cJSON *sub_units = cJSON_GetObjectItem(sub_obj, "units");
                    if (sub_units && cJSON_IsString(sub_units)) {
                        strncpy(sub->units, sub_units->valuestring, sizeof(sub->units) - 1);
                    }

                    sensor->sub_sensor_count++;
                }
            }

            // Parse calculation parameters
            cJSON *calc_obj = cJSON_GetObjectItem(sensor_obj, "calculation");
            if (calc_obj && cJSON_IsObject(calc_obj)) {
                calculation_params_t *calc = &sensor->calculation;
                memset(calc, 0, sizeof(calculation_params_t));

                cJSON *calc_type = cJSON_GetObjectItem(calc_obj, "calc_type");
                if (calc_type && cJSON_IsNumber(calc_type)) {
                    calc->calc_type = (calculation_type_t)calc_type->valueint;
                }

                // Scale/offset params
                cJSON *scale = cJSON_GetObjectItem(calc_obj, "scale");
                calc->scale = (scale && cJSON_IsNumber(scale)) ? (float)scale->valuedouble : 1.0f;

                cJSON *offset = cJSON_GetObjectItem(calc_obj, "offset");
                calc->offset = (offset && cJSON_IsNumber(offset)) ? (float)offset->valuedouble : 0.0f;

                // Combine register params
                cJSON *high_off = cJSON_GetObjectItem(calc_obj, "high_register_offset");
                calc->high_register_offset = (high_off && cJSON_IsNumber(high_off)) ? high_off->valueint : 0;

                cJSON *low_off = cJSON_GetObjectItem(calc_obj, "low_register_offset");
                calc->low_register_offset = (low_off && cJSON_IsNumber(low_off)) ? low_off->valueint : 1;

                cJSON *combine_mult = cJSON_GetObjectItem(calc_obj, "combine_multiplier");
                calc->combine_multiplier = (combine_mult && cJSON_IsNumber(combine_mult)) ? (float)combine_mult->valuedouble : 100.0f;

                // Level percentage params
                cJSON *tank_empty = cJSON_GetObjectItem(calc_obj, "tank_empty_value");
                calc->tank_empty_value = (tank_empty && cJSON_IsNumber(tank_empty)) ? (float)tank_empty->valuedouble : 0.0f;

                cJSON *tank_full = cJSON_GetObjectItem(calc_obj, "tank_full_value");
                calc->tank_full_value = (tank_full && cJSON_IsNumber(tank_full)) ? (float)tank_full->valuedouble : 100.0f;

                cJSON *invert = cJSON_GetObjectItem(calc_obj, "invert_level");
                calc->invert_level = (invert && cJSON_IsTrue(invert));

                // Tank dimensions
                cJSON *diameter = cJSON_GetObjectItem(calc_obj, "tank_diameter");
                calc->tank_diameter = (diameter && cJSON_IsNumber(diameter)) ? (float)diameter->valuedouble : 0.0f;

                cJSON *length = cJSON_GetObjectItem(calc_obj, "tank_length");
                calc->tank_length = (length && cJSON_IsNumber(length)) ? (float)length->valuedouble : 0.0f;

                cJSON *width = cJSON_GetObjectItem(calc_obj, "tank_width");
                calc->tank_width = (width && cJSON_IsNumber(width)) ? (float)width->valuedouble : 0.0f;

                cJSON *height = cJSON_GetObjectItem(calc_obj, "tank_height");
                calc->tank_height = (height && cJSON_IsNumber(height)) ? (float)height->valuedouble : 0.0f;

                cJSON *vol_unit = cJSON_GetObjectItem(calc_obj, "volume_unit");
                calc->volume_unit = (vol_unit && cJSON_IsNumber(vol_unit)) ? vol_unit->valueint : 0;

                // Secondary sensor for difference calc
                cJSON *sec_idx = cJSON_GetObjectItem(calc_obj, "secondary_sensor_index");
                calc->secondary_sensor_index = (sec_idx && cJSON_IsNumber(sec_idx)) ? sec_idx->valueint : 0;

                // Pulse params
                cJSON *ppu = cJSON_GetObjectItem(calc_obj, "pulses_per_unit");
                calc->pulses_per_unit = (ppu && cJSON_IsNumber(ppu)) ? (float)ppu->valuedouble : 1.0f;

                // Linear interpolation params
                cJSON *in_min = cJSON_GetObjectItem(calc_obj, "input_min");
                calc->input_min = (in_min && cJSON_IsNumber(in_min)) ? (float)in_min->valuedouble : 0.0f;

                cJSON *in_max = cJSON_GetObjectItem(calc_obj, "input_max");
                calc->input_max = (in_max && cJSON_IsNumber(in_max)) ? (float)in_max->valuedouble : 100.0f;

                cJSON *out_min = cJSON_GetObjectItem(calc_obj, "output_min");
                calc->output_min = (out_min && cJSON_IsNumber(out_min)) ? (float)out_min->valuedouble : 0.0f;

                cJSON *out_max = cJSON_GetObjectItem(calc_obj, "output_max");
                calc->output_max = (out_max && cJSON_IsNumber(out_max)) ? (float)out_max->valuedouble : 100.0f;

                // Polynomial params
                cJSON *poly_a = cJSON_GetObjectItem(calc_obj, "poly_a");
                calc->poly_a = (poly_a && cJSON_IsNumber(poly_a)) ? (float)poly_a->valuedouble : 0.0f;

                cJSON *poly_b = cJSON_GetObjectItem(calc_obj, "poly_b");
                calc->poly_b = (poly_b && cJSON_IsNumber(poly_b)) ? (float)poly_b->valuedouble : 1.0f;

                cJSON *poly_c = cJSON_GetObjectItem(calc_obj, "poly_c");
                calc->poly_c = (poly_c && cJSON_IsNumber(poly_c)) ? (float)poly_c->valuedouble : 0.0f;

                // Output unit
                cJSON *out_unit = cJSON_GetObjectItem(calc_obj, "output_unit");
                if (out_unit && cJSON_IsString(out_unit)) {
                    strncpy(calc->output_unit, out_unit->valuestring, sizeof(calc->output_unit) - 1);
                }

                cJSON *decimals = cJSON_GetObjectItem(calc_obj, "decimal_places");
                calc->decimal_places = (decimals && cJSON_IsNumber(decimals)) ? decimals->valueint : 2;
            }

            // Apply sensor type presets (auto-configures register address, quantity, etc.)
            // This sets ZEST sensors to register 4121, quantity 4, etc.
            // Presets only apply to values not explicitly set in JSON
            apply_sensor_type_presets(sensor);

            config->sensor_count++;
            ESP_LOGI(TAG, "[TWIN] Sensor %d configured: %s (slave=%d, reg=%d, type=%s, sensor_type=%s)",
                     config->sensor_count, sensor->name, sensor->slave_id,
                     sensor->register_address, sensor->data_type, sensor->sensor_type);
        }

        config_changed = true;
        ESP_LOGI(TAG, "[TWIN] Total %d sensors configured from Device Twin", config->sensor_count);
    }

    // Save configuration to NVS if changed
    if (config_changed) {
        esp_err_t save_result = config_save_to_nvs(config);
        if (save_result == ESP_OK) {
            ESP_LOGI(TAG, "[TWIN] Configuration saved to NVS");
        } else {
            ESP_LOGE(TAG, "[TWIN] Failed to save config: %s", esp_err_to_name(save_result));
        }

        // Report the applied changes back to Azure
        report_device_twin_properties();
    }

    cJSON_Delete(root);
    free(json_str);
}

// Report current device properties to Azure IoT Hub (reported properties)
static void report_device_twin_properties(void) {
    if (!mqtt_connected || !mqtt_client) {
        ESP_LOGW(TAG, "[TWIN] Cannot report properties - MQTT not connected");
        return;
    }

    system_config_t *config = get_system_config();

    // Build reported properties JSON
    cJSON *reported = cJSON_CreateObject();
    if (!reported) {
        ESP_LOGE(TAG, "[TWIN] Failed to create JSON object");
        return;
    }

    // Add current configuration values
    cJSON_AddNumberToObject(reported, "telemetry_interval", config->telemetry_interval);
    cJSON_AddNumberToObject(reported, "modbus_retry_count", config->modbus_retry_count);
    cJSON_AddNumberToObject(reported, "modbus_retry_delay", config->modbus_retry_delay);
    cJSON_AddBoolToObject(reported, "batch_telemetry", config->batch_telemetry);
    cJSON_AddNumberToObject(reported, "sensor_count", config->sensor_count);

    // Add firmware version and device info
    cJSON_AddStringToObject(reported, "firmware_version", "1.0.0");
    cJSON_AddStringToObject(reported, "device_id", config->azure_device_id);
    cJSON_AddNumberToObject(reported, "last_boot_time", (double)esp_timer_get_time() / 1000000.0);

    // Add network mode
    cJSON_AddStringToObject(reported, "network_mode",
        config->network_mode == NETWORK_MODE_SIM ? "SIM" : "WiFi");

    // Add web server status
    cJSON_AddBoolToObject(reported, "web_server_enabled", web_server_running);

    // Add remote control properties
    cJSON_AddBoolToObject(reported, "maintenance_mode", maintenance_mode);
    cJSON_AddBoolToObject(reported, "ota_enabled", ota_enabled);
    if (strlen(ota_url) > 0) {
        cJSON_AddStringToObject(reported, "ota_url", ota_url);
    }
    cJSON_AddStringToObject(reported, "ota_status", "idle");  // idle, downloading, success, failed

    // Add system health info
    cJSON_AddNumberToObject(reported, "free_heap", esp_get_free_heap_size());
    cJSON_AddNumberToObject(reported, "uptime_sec", (double)(esp_timer_get_time() - system_uptime_start) / 1000000.0);

    // Add configured sensors summary
    if (config->sensor_count > 0) {
        cJSON *sensors_arr = cJSON_CreateArray();
        if (sensors_arr) {
            for (int i = 0; i < config->sensor_count && i < 10; i++) {
                cJSON *sensor_info = cJSON_CreateObject();
                if (sensor_info) {
                    cJSON_AddStringToObject(sensor_info, "name", config->sensors[i].name);
                    cJSON_AddStringToObject(sensor_info, "unit_id", config->sensors[i].unit_id);
                    cJSON_AddNumberToObject(sensor_info, "slave_id", config->sensors[i].slave_id);
                    cJSON_AddStringToObject(sensor_info, "type", config->sensors[i].sensor_type);
                    cJSON_AddBoolToObject(sensor_info, "enabled", config->sensors[i].enabled);
                    cJSON_AddItemToArray(sensors_arr, sensor_info);
                }
            }
            cJSON_AddItemToObject(reported, "sensors", sensors_arr);
        }
    }

    char *json_str = cJSON_PrintUnformatted(reported);
    if (!json_str) {
        ESP_LOGE(TAG, "[TWIN] Failed to serialize JSON");
        cJSON_Delete(reported);
        return;
    }

    // Build the topic: $iothub/twin/PATCH/properties/reported/?$rid={request_id}
    device_twin_request_id++;
    snprintf(device_twin_reported_topic, sizeof(device_twin_reported_topic),
             "$iothub/twin/PATCH/properties/reported/?$rid=%d", device_twin_request_id);

    int msg_id = esp_mqtt_client_publish(mqtt_client, device_twin_reported_topic,
                                          json_str, strlen(json_str), 1, 0);

    if (msg_id >= 0) {
        ESP_LOGI(TAG, "[TWIN] Reported properties published (msg_id=%d, rid=%d)",
                 msg_id, device_twin_request_id);
        ESP_LOGI(TAG, "[TWIN] Payload: %s", json_str);
    } else {
        ESP_LOGE(TAG, "[TWIN] Failed to publish reported properties");
    }

    free(json_str);
    cJSON_Delete(reported);
}

// OTA status callback - reports OTA status changes to Azure Device Twin
static void ota_status_callback(ota_status_t status, const char* message)
{
    if (!mqtt_connected || !mqtt_client) {
        ESP_LOGW(TAG, "[OTA] Cannot report status - MQTT not connected");
        return;
    }

    ESP_LOGI(TAG, "[OTA] Status changed: %s - %s", ota_status_to_string(status), message ? message : "");

    // Build OTA status JSON
    cJSON *ota_json = cJSON_CreateObject();
    if (!ota_json) {
        ESP_LOGE(TAG, "[OTA] Failed to create JSON object");
        return;
    }

    cJSON_AddStringToObject(ota_json, "ota_status", ota_status_to_string(status));
    if (message && strlen(message) > 0) {
        cJSON_AddStringToObject(ota_json, "ota_message", message);
    }

    // Add progress info for downloading/installing states
    if (status == OTA_STATUS_DOWNLOADING || status == OTA_STATUS_INSTALLING) {
        ota_info_t* info = ota_get_info();
        if (info) {
            cJSON_AddNumberToObject(ota_json, "ota_progress", info->progress);
            cJSON_AddNumberToObject(ota_json, "ota_bytes_downloaded", info->bytes_downloaded);
            cJSON_AddNumberToObject(ota_json, "ota_total_bytes", info->total_bytes);
        }
    }

    char *json_str = cJSON_PrintUnformatted(ota_json);
    if (!json_str) {
        ESP_LOGE(TAG, "[OTA] Failed to serialize JSON");
        cJSON_Delete(ota_json);
        return;
    }

    // Publish to Device Twin reported properties
    device_twin_request_id++;
    snprintf(device_twin_reported_topic, sizeof(device_twin_reported_topic),
             "$iothub/twin/PATCH/properties/reported/?$rid=%d", device_twin_request_id);

    int msg_id = esp_mqtt_client_publish(mqtt_client, device_twin_reported_topic,
                                          json_str, strlen(json_str), 1, 0);

    if (msg_id >= 0) {
        ESP_LOGI(TAG, "[OTA] Status reported to Azure (msg_id=%d)", msg_id);
    } else {
        ESP_LOGE(TAG, "[OTA] Failed to report status to Azure");
    }

    free(json_str);
    cJSON_Delete(ota_json);
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
        // CRITICAL: Do NOT refresh SAS token if OTA is in progress (prevents MQTT restart during OTA)
        if (mqtt_initialized && is_network_connected() && sas_token_needs_refresh() && !ota_is_in_progress()) {
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
        // Skip reconnection warnings during OTA to reduce noise
        if (mqtt_initialized && !mqtt_connected && !ota_is_in_progress()) {
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

        // Check for maintenance mode - skip telemetry but keep task alive
        if (maintenance_mode) {
            static bool maintenance_logged = false;
            if (!maintenance_logged) {
                ESP_LOGW(TAG, "[DATA] Maintenance mode active - telemetry paused");
                maintenance_logged = true;
            }
            vTaskDelay(pdMS_TO_TICKS(5000));  // Check every 5 seconds
            if (!maintenance_mode) {
                ESP_LOGI(TAG, "[DATA] Maintenance mode ended - resuming telemetry");
                maintenance_logged = false;
            }
            continue;
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

    // IMPORTANT: Replay ALL cached offline messages FIRST before sending live data
    // This ensures chronological order - older cached data must be sent before newer live data
    // Otherwise cloud analytics will use live data as reference and cached data becomes useless
    // Uses rate limiting from iot_configs.h to prevent Azure IoT Hub throttling/disconnection
    if (config->sd_config.enabled) {
        uint32_t pending_count = 0;
        sd_card_get_pending_count(&pending_count);

        if (pending_count > 0) {
            uint32_t total_cached = pending_count;
            ESP_LOGI(TAG, "[SD] 📤 Found %lu cached messages - sending ALL before live data", total_cached);
            ESP_LOGI(TAG, "[SD] 📊 Rate limiting: %dms between messages, %d per batch, %dms between batches",
                     SD_REPLAY_DELAY_BETWEEN_MESSAGES_MS, SD_REPLAY_MAX_MESSAGES_PER_BATCH,
                     SD_REPLAY_DELAY_BETWEEN_BATCHES_MS);

            // Keep replaying until ALL cached messages are sent
            uint32_t batches_sent = 0;
            while (pending_count > 0 && mqtt_connected) {
                batches_sent++;
                ESP_LOGI(TAG, "[SD] 📤 Sending batch %lu... (%lu messages remaining)", batches_sent, pending_count);

                // Reset replay control variables for this batch
                sd_replay_should_stop = false;
                sd_replay_messages_sent = 0;
                sd_replay_last_msg_id = 0;

                esp_err_t replay_ret = sd_card_replay_messages(replay_message_callback);

                // Check if MQTT disconnected during replay
                if (sd_replay_should_stop && !mqtt_connected) {
                    ESP_LOGW(TAG, "[SD] ⚠️ MQTT disconnected during replay - will retry when connection restored");
                    break;  // Exit loop, retry on next telemetry cycle
                }

                if (replay_ret != ESP_OK) {
                    ESP_LOGW(TAG, "[SD] ⚠️ Batch replay failed: %s - will retry on next telemetry cycle", esp_err_to_name(replay_ret));
                    break;  // Stop on error to avoid infinite loop, retry next cycle
                }

                // Update pending count after batch
                uint32_t prev_count = pending_count;
                sd_card_get_pending_count(&pending_count);

                // Safety check: if count didn't decrease, something is wrong
                if (pending_count >= prev_count && pending_count > 0) {
                    ESP_LOGW(TAG, "[SD] ⚠️ Message count not decreasing, stopping to prevent infinite loop");
                    break;
                }

                ESP_LOGI(TAG, "[SD] ✅ Batch %lu complete: sent %lu messages, %lu remaining",
                         batches_sent, sd_replay_messages_sent, pending_count);

                // Delay between batches to let Azure IoT Hub recover (uses config value)
                if (pending_count > 0 && mqtt_connected) {
                    ESP_LOGI(TAG, "[SD] ⏳ Waiting %dms before next batch...", SD_REPLAY_DELAY_BETWEEN_BATCHES_MS);
                    vTaskDelay(pdMS_TO_TICKS(SD_REPLAY_DELAY_BETWEEN_BATCHES_MS));

                    // Check connection again after delay
                    if (!mqtt_connected) {
                        ESP_LOGW(TAG, "[SD] ⚠️ MQTT disconnected during batch delay - will retry later");
                        break;
                    }
                }
            }

            if (pending_count == 0) {
                ESP_LOGI(TAG, "[SD] ✅ ALL %lu cached messages sent in %lu batches - now sending live data", total_cached, batches_sent);
            } else if (!mqtt_connected) {
                ESP_LOGW(TAG, "[SD] ⚠️ %lu messages still pending (MQTT disconnected) - will continue when connected", pending_count);
            } else {
                ESP_LOGW(TAG, "[SD] ⚠️ %lu messages still pending - will continue on next cycle", pending_count);
            }

            // Delay before sending live data (let connection stabilize)
            if (mqtt_connected) {
                vTaskDelay(pdMS_TO_TICKS(1000));
            }
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

        // Try to reconnect MQTT if disconnected (but not during OTA)
        if (!mqtt_connected && mqtt_client != NULL && !ota_is_in_progress()) {
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
        // Set up OTA status callback for Azure Device Twin reporting
        ota_set_status_callback(ota_status_callback);
        ESP_LOGI(TAG, "[OTA] Status callback registered for Azure reporting");
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