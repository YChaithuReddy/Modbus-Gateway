/**
 * @file ota_update.c
 * @brief OTA firmware update implementation for ESP32 Modbus IoT Gateway
 */

#include "ota_update.h"
#include "iot_configs.h"
#include "web_config.h"
#include "a7670c_ppp.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_https_ota.h"
#include "esp_http_client.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_crt_bundle.h"
#include "esp_netif.h"

static const char *TAG = "OTA_UPDATE";

// OTA state
static ota_info_t ota_info = {0};
static SemaphoreHandle_t ota_mutex = NULL;
static TaskHandle_t ota_task_handle = NULL;
static ota_progress_cb_t progress_callback = NULL;
static ota_status_cb_t status_callback = NULL;
static volatile bool ota_cancel_requested = false;

// For chunked web upload
static esp_ota_handle_t ota_handle = 0;
static const esp_partition_t *ota_partition = NULL;

// NVS keys
#define NVS_OTA_NAMESPACE "ota"
#define NVS_KEY_BOOT_COUNT "boot_cnt"

// Forward declarations
static void ota_download_task(void *pvParameter);
static void notify_status_change(ota_status_t status, const char* message);

// Storage for redirect URL captured from event handler
// GitHub CDN URLs can be very long (includes JWT tokens), need large buffer
static char redirect_location[2048] = {0};

// HTTP event handler to capture Location header during redirects
static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    switch (evt->event_id) {
        case HTTP_EVENT_ON_HEADER:
            // Check if this is the Location header
            if (strcasecmp(evt->header_key, "Location") == 0 ||
                strcasecmp(evt->header_key, "location") == 0) {
                size_t url_len = strlen(evt->header_value);
                if (url_len >= sizeof(redirect_location)) {
                    ESP_LOGW(TAG, "Location header too long (%d bytes), truncating", url_len);
                    url_len = sizeof(redirect_location) - 1;
                }
                strncpy(redirect_location, evt->header_value, sizeof(redirect_location) - 1);
                redirect_location[sizeof(redirect_location) - 1] = '\0';
                ESP_LOGI(TAG, "Captured Location header (%d bytes)", strlen(redirect_location));
            }
            break;
        default:
            break;
    }
    return ESP_OK;
}

// Helper to notify status changes
static void notify_status_change(ota_status_t status, const char* message)
{
    if (status_callback) {
        status_callback(status, message ? message : "");
    }
}

esp_err_t ota_init(void)
{
    ESP_LOGI(TAG, "Initializing OTA module...");

    // Create mutex for thread-safe access
    if (ota_mutex == NULL) {
        ota_mutex = xSemaphoreCreateMutex();
        if (ota_mutex == NULL) {
            ESP_LOGE(TAG, "Failed to create OTA mutex");
            return ESP_FAIL;
        }
    }

    // Set current version
    strncpy(ota_info.current_version, FW_VERSION_STRING, sizeof(ota_info.current_version) - 1);
    ota_info.status = OTA_STATUS_IDLE;
    ota_info.progress = 0;
    ota_info.error_msg[0] = '\0';

    // Check if we're running after a rollback
    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_ota_img_states_t ota_state;

    if (esp_ota_get_state_partition(running, &ota_state) == ESP_OK) {
        if (ota_state == ESP_OTA_IMG_PENDING_VERIFY) {
            ESP_LOGW(TAG, "Running in pending verify state - firmware needs validation");
            ota_info.is_rollback = false;
        } else if (ota_state == ESP_OTA_IMG_ABORTED) {
            ESP_LOGW(TAG, "Previous firmware was aborted - this is a ROLLBACK boot");
            ota_info.is_rollback = true;
            ota_info.status = OTA_STATUS_ROLLBACK;
        }
    }

    // Load boot count from NVS
    nvs_handle_t nvs;
    if (nvs_open(NVS_OTA_NAMESPACE, NVS_READWRITE, &nvs) == ESP_OK) {
        uint8_t boot_count = 0;
        nvs_get_u8(nvs, NVS_KEY_BOOT_COUNT, &boot_count);
        ota_info.boot_count = boot_count;

        // Increment boot count (will be reset when marked valid)
        boot_count++;
        nvs_set_u8(nvs, NVS_KEY_BOOT_COUNT, boot_count);
        nvs_commit(nvs);
        nvs_close(nvs);

        ESP_LOGI(TAG, "Boot count: %d", boot_count);
    }

    // Log partition info
    ESP_LOGI(TAG, "Running partition: %s @ 0x%lx", running->label, running->address);
    ESP_LOGI(TAG, "Firmware version: %s", ota_info.current_version);

    if (ota_info.is_rollback) {
        ESP_LOGW(TAG, "*** ROLLBACK DETECTED - Previous firmware failed ***");
        snprintf(ota_info.error_msg, sizeof(ota_info.error_msg), "Rollback from failed update");
    }

    return ESP_OK;
}

esp_err_t ota_start_update(const char* firmware_url, const char* version)
{
    if (firmware_url == NULL || strlen(firmware_url) == 0) {
        ESP_LOGE(TAG, "Invalid firmware URL");
        return ESP_ERR_INVALID_ARG;
    }

    xSemaphoreTake(ota_mutex, portMAX_DELAY);

    // Check if update already in progress
    if (ota_info.status == OTA_STATUS_DOWNLOADING ||
        ota_info.status == OTA_STATUS_INSTALLING) {
        ESP_LOGW(TAG, "OTA already in progress");
        xSemaphoreGive(ota_mutex);
        return ESP_ERR_INVALID_STATE;
    }

    // Reset state
    ota_info.status = OTA_STATUS_CHECKING;
    ota_info.progress = 0;
    ota_info.bytes_downloaded = 0;
    ota_info.total_bytes = 0;
    ota_info.error_msg[0] = '\0';
    ota_cancel_requested = false;

    // Store URL and version
    strncpy(ota_info.update_url, firmware_url, sizeof(ota_info.update_url) - 1);
    if (version) {
        strncpy(ota_info.new_version, version, sizeof(ota_info.new_version) - 1);
    } else {
        strcpy(ota_info.new_version, "unknown");
    }

    xSemaphoreGive(ota_mutex);

    ESP_LOGI(TAG, "Starting OTA update from: %s", firmware_url);
    ESP_LOGI(TAG, "New version: %s", ota_info.new_version);

    // Create OTA download task
    BaseType_t ret = xTaskCreate(
        ota_download_task,
        "ota_task",
        8192,
        NULL,
        5,
        &ota_task_handle
    );

    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create OTA task");
        ota_info.status = OTA_STATUS_FAILED;
        snprintf(ota_info.error_msg, sizeof(ota_info.error_msg), "Failed to create task");
        return ESP_FAIL;
    }

    return ESP_OK;
}

static void ota_download_task(void *pvParameter)
{
    ESP_LOGI(TAG, "OTA download task started");
    esp_err_t err;
    esp_http_client_handle_t client = NULL;
    char *download_buffer = NULL;
    const int DOWNLOAD_BUFFER_SIZE = 4096;
    bool ota_started = false;
    int last_logged_progress = -10;

    xSemaphoreTake(ota_mutex, portMAX_DELAY);
    ota_info.status = OTA_STATUS_DOWNLOADING;
    xSemaphoreGive(ota_mutex);
    notify_status_change(OTA_STATUS_DOWNLOADING, "Starting download");

    // Allocate download buffer
    download_buffer = malloc(DOWNLOAD_BUFFER_SIZE);
    if (!download_buffer) {
        ESP_LOGE(TAG, "Failed to allocate download buffer");
        xSemaphoreTake(ota_mutex, portMAX_DELAY);
        ota_info.status = OTA_STATUS_FAILED;
        snprintf(ota_info.error_msg, sizeof(ota_info.error_msg), "Memory allocation failed");
        xSemaphoreGive(ota_mutex);
        goto cleanup;
    }

    // Current URL for redirect following
    char *current_url = strdup(ota_info.update_url);
    if (!current_url) {
        ESP_LOGE(TAG, "Failed to allocate URL buffer");
        xSemaphoreTake(ota_mutex, portMAX_DELAY);
        ota_info.status = OTA_STATUS_FAILED;
        snprintf(ota_info.error_msg, sizeof(ota_info.error_msg), "Memory allocation failed");
        xSemaphoreGive(ota_mutex);
        goto cleanup;
    }

    // Handle redirects by recreating client for each redirect (more reliable than reusing)
    int redirect_count = 0;
    const int MAX_REDIRECTS = 10;
    int status_code = 0;
    int content_length = 0;

    // Check if we're in SIM mode and need to use PPP interface
    system_config_t *sys_config = get_system_config();
    bool use_ppp = (sys_config != NULL && sys_config->network_mode == NETWORK_MODE_SIM);

    if (use_ppp) {
        esp_netif_t *ppp_netif = a7670c_ppp_get_netif();
        if (ppp_netif != NULL) {
            // Set PPP as the default network interface for routing
            esp_netif_set_default_netif(ppp_netif);
            ESP_LOGI(TAG, "SIM mode detected - set PPP as default network interface");
        } else {
            ESP_LOGW(TAG, "SIM mode but PPP netif not available - using default routing");
        }
    }

    while (redirect_count < MAX_REDIRECTS) {
        // Clear redirect location from previous iteration
        redirect_location[0] = '\0';

        // Check if URL is from GitHub (for logging purposes)
        bool is_github_url = (strstr(current_url, "github.com") != NULL) ||
                             (strstr(current_url, "githubusercontent.com") != NULL) ||
                             (strstr(current_url, "github-releases") != NULL) ||
                             (strstr(current_url, "objects.githubusercontent.com") != NULL);

        ESP_LOGI(TAG, "Connecting to: %s", current_url);
        if (is_github_url) {
            ESP_LOGW(TAG, "GitHub/CDN URL - cert verification skipped via sdkconfig");
        }

        // Configure HTTP client for this URL
        // Use event handler to capture Location header during redirects
        esp_http_client_config_t http_config = {
            .url = current_url,
            .timeout_ms = OTA_RECV_TIMEOUT_MS,
            .keep_alive_enable = false,           // Don't keep alive for redirects
            .buffer_size = 4096,                  // Larger buffer for HTTP headers
            .buffer_size_tx = 1024,
            .skip_cert_common_name_check = true,  // Allow redirects to different domains
            .crt_bundle_attach = esp_crt_bundle_attach,  // Always attach - sdkconfig skips verification
            .event_handler = http_event_handler,  // Capture Location header via events
        };

        // Create HTTP client
        client = esp_http_client_init(&http_config);
        if (!client) {
            ESP_LOGE(TAG, "Failed to create HTTP client");
            xSemaphoreTake(ota_mutex, portMAX_DELAY);
            ota_info.status = OTA_STATUS_FAILED;
            snprintf(ota_info.error_msg, sizeof(ota_info.error_msg), "HTTP client init failed");
            xSemaphoreGive(ota_mutex);
            free(current_url);
            goto cleanup;
        }

        // Set User-Agent header (required by GitHub)
        esp_http_client_set_header(client, "User-Agent", "ESP32-OTA/1.0");
        esp_http_client_set_header(client, "Accept", "*/*");

        // Open HTTP connection
        err = esp_http_client_open(client, 0);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to open HTTP connection: %s", esp_err_to_name(err));
            xSemaphoreTake(ota_mutex, portMAX_DELAY);
            ota_info.status = OTA_STATUS_FAILED;
            snprintf(ota_info.error_msg, sizeof(ota_info.error_msg), "Connection failed: %s", esp_err_to_name(err));
            xSemaphoreGive(ota_mutex);
            free(current_url);
            goto cleanup;
        }

        // Fetch headers
        content_length = esp_http_client_fetch_headers(client);
        status_code = esp_http_client_get_status_code(client);

        ESP_LOGI(TAG, "HTTP Status: %d, Content-Length: %d", status_code, content_length);

        // Check for redirect (301, 302, 303, 307, 308)
        if (status_code == 301 || status_code == 302 || status_code == 303 ||
            status_code == 307 || status_code == 308) {

            redirect_count++;
            ESP_LOGI(TAG, "Redirect %d detected (HTTP %d)", redirect_count, status_code);

            // The Location header was captured by the event handler
            if (redirect_location[0] == '\0') {
                ESP_LOGE(TAG, "No Location header captured by event handler");
                esp_http_client_close(client);
                esp_http_client_cleanup(client);
                client = NULL;
                xSemaphoreTake(ota_mutex, portMAX_DELAY);
                ota_info.status = OTA_STATUS_FAILED;
                snprintf(ota_info.error_msg, sizeof(ota_info.error_msg), "Redirect: no Location header");
                xSemaphoreGive(ota_mutex);
                free(current_url);
                goto cleanup;
            }

            ESP_LOGI(TAG, "Redirecting to: %s", redirect_location);

            // Copy the redirect URL before cleaning up
            char *new_url = strdup(redirect_location);

            // Clean up current client
            esp_http_client_close(client);
            esp_http_client_cleanup(client);
            client = NULL;

            if (!new_url) {
                ESP_LOGE(TAG, "Failed to allocate redirect URL");
                xSemaphoreTake(ota_mutex, portMAX_DELAY);
                ota_info.status = OTA_STATUS_FAILED;
                snprintf(ota_info.error_msg, sizeof(ota_info.error_msg), "Memory allocation failed");
                xSemaphoreGive(ota_mutex);
                free(current_url);
                goto cleanup;
            }

            // Update current URL for next iteration
            free(current_url);
            current_url = new_url;

            continue;
        }

        // Not a redirect, break out of loop
        break;
    }

    // Free URL buffer - no longer needed
    free(current_url);
    current_url = NULL;

    if (redirect_count >= MAX_REDIRECTS) {
        ESP_LOGE(TAG, "Too many redirects");
        xSemaphoreTake(ota_mutex, portMAX_DELAY);
        ota_info.status = OTA_STATUS_FAILED;
        snprintf(ota_info.error_msg, sizeof(ota_info.error_msg), "Too many redirects");
        xSemaphoreGive(ota_mutex);
        goto cleanup;
    }

    if (status_code != 200) {
        ESP_LOGE(TAG, "HTTP error: %d", status_code);
        xSemaphoreTake(ota_mutex, portMAX_DELAY);
        ota_info.status = OTA_STATUS_FAILED;
        snprintf(ota_info.error_msg, sizeof(ota_info.error_msg), "HTTP error: %d", status_code);
        xSemaphoreGive(ota_mutex);
        goto cleanup;
    }

    if (content_length > 0) {
        xSemaphoreTake(ota_mutex, portMAX_DELAY);
        ota_info.total_bytes = content_length;
        xSemaphoreGive(ota_mutex);
        ESP_LOGI(TAG, "Firmware size: %d bytes", content_length);
    }

    // Get OTA partition (same as web upload)
    ota_partition = esp_ota_get_next_update_partition(NULL);
    if (ota_partition == NULL) {
        ESP_LOGE(TAG, "No OTA partition available");
        xSemaphoreTake(ota_mutex, portMAX_DELAY);
        ota_info.status = OTA_STATUS_FAILED;
        snprintf(ota_info.error_msg, sizeof(ota_info.error_msg), "No OTA partition");
        xSemaphoreGive(ota_mutex);
        goto cleanup;
    }

    ESP_LOGI(TAG, "Writing to partition: %s @ 0x%lx", ota_partition->label, ota_partition->address);

    // Start OTA (same as web upload)
    err = esp_ota_begin(ota_partition, OTA_SIZE_UNKNOWN, &ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_begin failed: %s", esp_err_to_name(err));
        xSemaphoreTake(ota_mutex, portMAX_DELAY);
        ota_info.status = OTA_STATUS_FAILED;
        snprintf(ota_info.error_msg, sizeof(ota_info.error_msg), "OTA begin failed: %s", esp_err_to_name(err));
        xSemaphoreGive(ota_mutex);
        goto cleanup;
    }
    ota_started = true;

    xSemaphoreTake(ota_mutex, portMAX_DELAY);
    ota_info.status = OTA_STATUS_INSTALLING;
    ota_info.bytes_downloaded = 0;
    xSemaphoreGive(ota_mutex);
    notify_status_change(OTA_STATUS_INSTALLING, "Writing firmware to flash");

    // Download and write firmware (same approach as web upload)
    int bytes_read = 0;
    int total_read = 0;

    while ((bytes_read = esp_http_client_read(client, download_buffer, DOWNLOAD_BUFFER_SIZE)) > 0) {
        // Check for cancel request
        if (ota_cancel_requested) {
            ESP_LOGW(TAG, "OTA cancelled by user");
            xSemaphoreTake(ota_mutex, portMAX_DELAY);
            ota_info.status = OTA_STATUS_IDLE;
            snprintf(ota_info.error_msg, sizeof(ota_info.error_msg), "Cancelled by user");
            xSemaphoreGive(ota_mutex);
            goto cleanup;
        }

        // Write to flash (same as web upload)
        err = esp_ota_write(ota_handle, download_buffer, bytes_read);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "esp_ota_write failed: %s", esp_err_to_name(err));
            xSemaphoreTake(ota_mutex, portMAX_DELAY);
            ota_info.status = OTA_STATUS_FAILED;
            snprintf(ota_info.error_msg, sizeof(ota_info.error_msg), "Write failed: %s", esp_err_to_name(err));
            xSemaphoreGive(ota_mutex);
            goto cleanup;
        }

        total_read += bytes_read;

        // Update progress
        xSemaphoreTake(ota_mutex, portMAX_DELAY);
        ota_info.bytes_downloaded = total_read;
        if (ota_info.total_bytes > 0) {
            ota_info.progress = (total_read * 100) / ota_info.total_bytes;
        }
        xSemaphoreGive(ota_mutex);

        // Call progress callback
        if (progress_callback) {
            progress_callback(ota_info.progress, ota_info.bytes_downloaded, ota_info.total_bytes);
        }

        // Log progress every 10%
        if (ota_info.progress >= last_logged_progress + 10) {
            ESP_LOGI(TAG, "Download progress: %d%% (%d/%lu bytes)",
                     ota_info.progress, total_read, ota_info.total_bytes);
            last_logged_progress = ota_info.progress;
        }
    }

    if (bytes_read < 0) {
        ESP_LOGE(TAG, "HTTP read error");
        xSemaphoreTake(ota_mutex, portMAX_DELAY);
        ota_info.status = OTA_STATUS_FAILED;
        snprintf(ota_info.error_msg, sizeof(ota_info.error_msg), "Download failed");
        xSemaphoreGive(ota_mutex);
        goto cleanup;
    }

    ESP_LOGI(TAG, "Download complete: %d bytes", total_read);

    // Finish OTA (same as web upload - this validates the firmware)
    err = esp_ota_end(ota_handle);
    ota_handle = 0;
    ota_started = false;

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_end failed: %s", esp_err_to_name(err));
        xSemaphoreTake(ota_mutex, portMAX_DELAY);
        ota_info.status = OTA_STATUS_FAILED;
        snprintf(ota_info.error_msg, sizeof(ota_info.error_msg), "Validation failed: %s", esp_err_to_name(err));
        xSemaphoreGive(ota_mutex);
        goto cleanup;
    }

    // Set boot partition
    err = esp_ota_set_boot_partition(ota_partition);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_set_boot_partition failed: %s", esp_err_to_name(err));
        xSemaphoreTake(ota_mutex, portMAX_DELAY);
        ota_info.status = OTA_STATUS_FAILED;
        snprintf(ota_info.error_msg, sizeof(ota_info.error_msg), "Set boot failed: %s", esp_err_to_name(err));
        xSemaphoreGive(ota_mutex);
        goto cleanup;
    }

    // Success!
    xSemaphoreTake(ota_mutex, portMAX_DELAY);
    ota_info.status = OTA_STATUS_PENDING_REBOOT;
    ota_info.progress = 100;
    strncpy(ota_info.current_version, ota_info.new_version, sizeof(ota_info.current_version) - 1);
    xSemaphoreGive(ota_mutex);
    notify_status_change(OTA_STATUS_PENDING_REBOOT, "Update successful, rebooting...");

    ESP_LOGI(TAG, "✅ OTA update successful! Firmware downloaded and verified.");
    ESP_LOGI(TAG, "Rebooting in 5 seconds to apply update...");

    // Clean up before reboot
    if (client) {
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        client = NULL;
    }
    if (download_buffer) {
        free(download_buffer);
        download_buffer = NULL;
    }

    // Wait 5 seconds for status to be reported, then reboot
    vTaskDelay(pdMS_TO_TICKS(5000));
    ESP_LOGI(TAG, "Rebooting now...");
    esp_restart();
    // Code below will never execute after reboot

cleanup:
    // Notify failure if status is FAILED
    if (ota_info.status == OTA_STATUS_FAILED) {
        notify_status_change(OTA_STATUS_FAILED, ota_info.error_msg);
    }

    if (ota_started && ota_handle) {
        esp_ota_abort(ota_handle);
        ota_handle = 0;
    }
    if (client) {
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
    }
    if (download_buffer) {
        free(download_buffer);
    }
    ota_task_handle = NULL;
    vTaskDelete(NULL);
}

esp_err_t ota_write_chunk(const uint8_t* data, size_t len, bool is_first, bool is_last)
{
    esp_err_t err;

    if (is_first) {
        // Start new OTA session
        ota_partition = esp_ota_get_next_update_partition(NULL);
        if (ota_partition == NULL) {
            ESP_LOGE(TAG, "No OTA partition available");
            return ESP_FAIL;
        }

        ESP_LOGI(TAG, "Writing to partition: %s @ 0x%lx", ota_partition->label, ota_partition->address);

        err = esp_ota_begin(ota_partition, OTA_SIZE_UNKNOWN, &ota_handle);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "esp_ota_begin failed: %s", esp_err_to_name(err));
            return err;
        }

        xSemaphoreTake(ota_mutex, portMAX_DELAY);
        ota_info.status = OTA_STATUS_INSTALLING;
        ota_info.progress = 0;
        ota_info.bytes_downloaded = 0;
        xSemaphoreGive(ota_mutex);
    }

    if (data && len > 0) {
        err = esp_ota_write(ota_handle, data, len);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "esp_ota_write failed: %s", esp_err_to_name(err));
            esp_ota_abort(ota_handle);
            ota_handle = 0;
            return err;
        }

        xSemaphoreTake(ota_mutex, portMAX_DELAY);
        ota_info.bytes_downloaded += len;
        xSemaphoreGive(ota_mutex);
    }

    if (is_last) {
        err = esp_ota_end(ota_handle);
        ota_handle = 0;

        if (err != ESP_OK) {
            ESP_LOGE(TAG, "esp_ota_end failed: %s", esp_err_to_name(err));
            xSemaphoreTake(ota_mutex, portMAX_DELAY);
            ota_info.status = OTA_STATUS_FAILED;
            snprintf(ota_info.error_msg, sizeof(ota_info.error_msg), "Validation failed");
            xSemaphoreGive(ota_mutex);
            return err;
        }

        err = esp_ota_set_boot_partition(ota_partition);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "esp_ota_set_boot_partition failed: %s", esp_err_to_name(err));
            xSemaphoreTake(ota_mutex, portMAX_DELAY);
            ota_info.status = OTA_STATUS_FAILED;
            snprintf(ota_info.error_msg, sizeof(ota_info.error_msg), "Set boot partition failed");
            xSemaphoreGive(ota_mutex);
            return err;
        }

        xSemaphoreTake(ota_mutex, portMAX_DELAY);
        ota_info.status = OTA_STATUS_PENDING_REBOOT;
        ota_info.progress = 100;
        xSemaphoreGive(ota_mutex);

        ESP_LOGI(TAG, "Web upload OTA complete! Reboot to apply.");
    }

    return ESP_OK;
}

esp_err_t ota_cancel_update(void)
{
    if (ota_info.status != OTA_STATUS_DOWNLOADING) {
        return ESP_ERR_INVALID_STATE;
    }

    ota_cancel_requested = true;
    return ESP_OK;
}

ota_info_t* ota_get_info(void)
{
    return &ota_info;
}

void ota_mark_valid(void)
{
    ESP_LOGI(TAG, "Marking current firmware as valid");

    esp_err_t err = esp_ota_mark_app_valid_cancel_rollback();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to mark app valid: %s (may already be marked)", esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "Firmware marked as valid - rollback disabled");
    }

    // Reset boot count
    nvs_handle_t nvs;
    if (nvs_open(NVS_OTA_NAMESPACE, NVS_READWRITE, &nvs) == ESP_OK) {
        nvs_set_u8(nvs, NVS_KEY_BOOT_COUNT, 0);
        nvs_commit(nvs);
        nvs_close(nvs);
    }

    ota_info.boot_count = 0;
    ota_info.is_rollback = false;

    if (ota_info.status == OTA_STATUS_ROLLBACK) {
        ota_info.status = OTA_STATUS_IDLE;
    }
}

bool ota_is_rollback(void)
{
    return ota_info.is_rollback;
}

const char* ota_get_version(void)
{
    return ota_info.current_version;
}

void ota_set_progress_callback(ota_progress_cb_t callback)
{
    progress_callback = callback;
}

void ota_set_status_callback(ota_status_cb_t callback)
{
    status_callback = callback;
}

const char* ota_status_to_string(ota_status_t status)
{
    switch (status) {
        case OTA_STATUS_IDLE:           return "idle";
        case OTA_STATUS_CHECKING:       return "checking";
        case OTA_STATUS_DOWNLOADING:    return "downloading";
        case OTA_STATUS_VERIFYING:      return "verifying";
        case OTA_STATUS_INSTALLING:     return "installing";
        case OTA_STATUS_PENDING_REBOOT: return "pending_reboot";
        case OTA_STATUS_SUCCESS:        return "success";
        case OTA_STATUS_FAILED:         return "failed";
        case OTA_STATUS_ROLLBACK:       return "rollback";
        default:                        return "unknown";
    }
}

void ota_reboot(void)
{
    if (ota_info.status == OTA_STATUS_PENDING_REBOOT) {
        ESP_LOGI(TAG, "Rebooting to apply OTA update...");
        vTaskDelay(pdMS_TO_TICKS(1000));
        esp_restart();
    } else {
        ESP_LOGW(TAG, "No pending update to apply");
    }
}
