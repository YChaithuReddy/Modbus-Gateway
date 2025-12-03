/**
 * @file ota_update.c
 * @brief OTA firmware update implementation for ESP32 Modbus IoT Gateway
 */

#include "ota_update.h"
#include "iot_configs.h"

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

static const char *TAG = "OTA_UPDATE";

// OTA state
static ota_info_t ota_info = {0};
static SemaphoreHandle_t ota_mutex = NULL;
static TaskHandle_t ota_task_handle = NULL;
static ota_progress_cb_t progress_callback = NULL;
static volatile bool ota_cancel_requested = false;

// For chunked web upload
static esp_ota_handle_t ota_handle = 0;
static const esp_partition_t *ota_partition = NULL;

// NVS keys
#define NVS_OTA_NAMESPACE "ota"
#define NVS_KEY_BOOT_COUNT "boot_cnt"

// Forward declarations
static void ota_download_task(void *pvParameter);

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

    xSemaphoreTake(ota_mutex, portMAX_DELAY);
    ota_info.status = OTA_STATUS_DOWNLOADING;
    xSemaphoreGive(ota_mutex);

    // Configure HTTP client
    esp_http_client_config_t http_config = {
        .url = ota_info.update_url,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = OTA_RECV_TIMEOUT_MS,
        .keep_alive_enable = true,
    };

    // Configure OTA
    esp_https_ota_config_t ota_config = {
        .http_config = &http_config,
    };

    esp_https_ota_handle_t https_ota_handle = NULL;
    esp_err_t err = esp_https_ota_begin(&ota_config, &https_ota_handle);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_https_ota_begin failed: %s", esp_err_to_name(err));
        xSemaphoreTake(ota_mutex, portMAX_DELAY);
        ota_info.status = OTA_STATUS_FAILED;
        snprintf(ota_info.error_msg, sizeof(ota_info.error_msg), "Connection failed: %s", esp_err_to_name(err));
        xSemaphoreGive(ota_mutex);
        goto cleanup;
    }

    // Get image size
    int image_size = esp_https_ota_get_image_size(https_ota_handle);
    if (image_size > 0) {
        xSemaphoreTake(ota_mutex, portMAX_DELAY);
        ota_info.total_bytes = image_size;
        xSemaphoreGive(ota_mutex);
        ESP_LOGI(TAG, "Firmware size: %d bytes", image_size);
    }

    // Download and write firmware
    while (1) {
        // Check for cancel request
        if (ota_cancel_requested) {
            ESP_LOGW(TAG, "OTA cancelled by user");
            esp_https_ota_abort(https_ota_handle);
            xSemaphoreTake(ota_mutex, portMAX_DELAY);
            ota_info.status = OTA_STATUS_IDLE;
            snprintf(ota_info.error_msg, sizeof(ota_info.error_msg), "Cancelled by user");
            xSemaphoreGive(ota_mutex);
            goto cleanup;
        }

        err = esp_https_ota_perform(https_ota_handle);

        if (err == ESP_ERR_HTTPS_OTA_IN_PROGRESS) {
            // Update progress
            int bytes_read = esp_https_ota_get_image_len_read(https_ota_handle);

            xSemaphoreTake(ota_mutex, portMAX_DELAY);
            ota_info.bytes_downloaded = bytes_read;
            if (ota_info.total_bytes > 0) {
                ota_info.progress = (bytes_read * 100) / ota_info.total_bytes;
            }
            xSemaphoreGive(ota_mutex);

            // Call progress callback
            if (progress_callback) {
                progress_callback(ota_info.progress, ota_info.bytes_downloaded, ota_info.total_bytes);
            }

            // Log progress every 10%
            static int last_logged_progress = -10;
            if (ota_info.progress >= last_logged_progress + 10) {
                ESP_LOGI(TAG, "Download progress: %d%% (%lu/%lu bytes)",
                         ota_info.progress, ota_info.bytes_downloaded, ota_info.total_bytes);
                last_logged_progress = ota_info.progress;
            }

            continue;
        }

        if (err == ESP_OK) {
            // Download complete
            break;
        }

        // Error occurred
        ESP_LOGE(TAG, "esp_https_ota_perform failed: %s", esp_err_to_name(err));
        esp_https_ota_abort(https_ota_handle);
        xSemaphoreTake(ota_mutex, portMAX_DELAY);
        ota_info.status = OTA_STATUS_FAILED;
        snprintf(ota_info.error_msg, sizeof(ota_info.error_msg), "Download failed: %s", esp_err_to_name(err));
        xSemaphoreGive(ota_mutex);
        goto cleanup;
    }

    // Verify and finish
    xSemaphoreTake(ota_mutex, portMAX_DELAY);
    ota_info.status = OTA_STATUS_VERIFYING;
    ota_info.progress = 100;
    xSemaphoreGive(ota_mutex);

    ESP_LOGI(TAG, "Download complete, verifying...");

    if (!esp_https_ota_is_complete_data_received(https_ota_handle)) {
        ESP_LOGE(TAG, "Incomplete data received");
        esp_https_ota_abort(https_ota_handle);
        xSemaphoreTake(ota_mutex, portMAX_DELAY);
        ota_info.status = OTA_STATUS_FAILED;
        snprintf(ota_info.error_msg, sizeof(ota_info.error_msg), "Incomplete download");
        xSemaphoreGive(ota_mutex);
        goto cleanup;
    }

    // Finish OTA and set boot partition
    xSemaphoreTake(ota_mutex, portMAX_DELAY);
    ota_info.status = OTA_STATUS_INSTALLING;
    xSemaphoreGive(ota_mutex);

    err = esp_https_ota_finish(https_ota_handle);
    https_ota_handle = NULL;

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "OTA update successful!");
        xSemaphoreTake(ota_mutex, portMAX_DELAY);
        ota_info.status = OTA_STATUS_PENDING_REBOOT;
        strncpy(ota_info.current_version, ota_info.new_version, sizeof(ota_info.current_version) - 1);
        xSemaphoreGive(ota_mutex);

        ESP_LOGI(TAG, "Firmware updated to version: %s", ota_info.new_version);
        ESP_LOGI(TAG, "Please reboot to apply update");
    } else {
        ESP_LOGE(TAG, "esp_https_ota_finish failed: %s", esp_err_to_name(err));
        xSemaphoreTake(ota_mutex, portMAX_DELAY);
        ota_info.status = OTA_STATUS_FAILED;
        snprintf(ota_info.error_msg, sizeof(ota_info.error_msg), "Install failed: %s", esp_err_to_name(err));
        xSemaphoreGive(ota_mutex);
    }

cleanup:
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
