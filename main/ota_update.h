/**
 * @file ota_update.h
 * @brief OTA (Over-The-Air) firmware update module for ESP32 Modbus IoT Gateway
 *
 * Features:
 * - HTTPS firmware download from URL (GitHub Releases, Azure Blob, etc.)
 * - Automatic rollback on boot failure (3 attempts)
 * - Progress reporting via callbacks
 * - Thread-safe status updates
 */

#ifndef OTA_UPDATE_H
#define OTA_UPDATE_H

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// OTA Status enumeration
typedef enum {
    OTA_STATUS_IDLE = 0,        // No OTA in progress
    OTA_STATUS_CHECKING,        // Checking for updates
    OTA_STATUS_DOWNLOADING,     // Downloading firmware
    OTA_STATUS_VERIFYING,       // Verifying firmware integrity
    OTA_STATUS_INSTALLING,      // Writing to flash
    OTA_STATUS_PENDING_REBOOT,  // Ready to reboot
    OTA_STATUS_SUCCESS,         // Update completed successfully
    OTA_STATUS_FAILED,          // Update failed
    OTA_STATUS_ROLLBACK         // Running after rollback
} ota_status_t;

// OTA Information structure
typedef struct {
    char current_version[16];   // Current firmware version
    char new_version[16];       // New firmware version (if updating)
    char update_url[256];       // URL of firmware being downloaded
    ota_status_t status;        // Current OTA status
    uint8_t progress;           // Download progress 0-100%
    uint32_t bytes_downloaded;  // Bytes downloaded so far
    uint32_t total_bytes;       // Total firmware size
    char error_msg[64];         // Error message if failed
    bool is_rollback;           // True if running after rollback
    uint8_t boot_count;         // Boot attempts since last valid mark
} ota_info_t;

// Progress callback function type
typedef void (*ota_progress_cb_t)(uint8_t progress, uint32_t bytes_downloaded, uint32_t total_bytes);

// Status change callback function type (for reporting to Azure)
typedef void (*ota_status_cb_t)(ota_status_t status, const char* message);

/**
 * @brief Initialize OTA module
 *
 * - Checks if running after rollback
 * - Loads boot count from NVS
 * - Sets initial status
 *
 * @return ESP_OK on success
 */
esp_err_t ota_init(void);

/**
 * @brief Start OTA update from URL
 *
 * Downloads firmware from HTTPS URL and installs to inactive OTA partition.
 * This function returns immediately - update happens in background task.
 *
 * @param firmware_url HTTPS URL of firmware binary
 * @param version Version string of new firmware (for logging)
 * @return ESP_OK if download started, error code otherwise
 */
esp_err_t ota_start_update(const char* firmware_url, const char* version);

/**
 * @brief Start OTA update from binary data (for web upload)
 *
 * Writes firmware data directly to inactive OTA partition.
 * Call multiple times with chunks of data.
 *
 * @param data Firmware data chunk
 * @param len Length of data chunk
 * @param is_first True if this is the first chunk
 * @param is_last True if this is the last chunk
 * @return ESP_OK on success
 */
esp_err_t ota_write_chunk(const uint8_t* data, size_t len, bool is_first, bool is_last);

/**
 * @brief Cancel ongoing OTA update
 *
 * @return ESP_OK if cancelled, ESP_FAIL if no update in progress
 */
esp_err_t ota_cancel_update(void);

/**
 * @brief Get current OTA information
 *
 * @return Pointer to OTA info structure (do not free)
 */
ota_info_t* ota_get_info(void);

/**
 * @brief Mark current firmware as valid
 *
 * Call this after successful boot to prevent rollback.
 * Should be called after all critical systems are verified working.
 */
void ota_mark_valid(void);

/**
 * @brief Check if running after rollback
 *
 * @return true if current boot is after automatic rollback
 */
bool ota_is_rollback(void);

/**
 * @brief Check if OTA update is in progress
 *
 * Use this to suppress MQTT reconnection during OTA downloads.
 *
 * @return true if OTA download is active
 */
bool ota_is_in_progress(void);

/**
 * @brief Get current firmware version string
 *
 * @return Version string (e.g., "1.2.0")
 */
const char* ota_get_version(void);

/**
 * @brief Set progress callback
 *
 * @param callback Function to call with progress updates
 */
void ota_set_progress_callback(ota_progress_cb_t callback);

/**
 * @brief Set status change callback
 *
 * @param callback Function to call when OTA status changes
 */
void ota_set_status_callback(ota_status_cb_t callback);

/**
 * @brief Get status as string
 *
 * @param status OTA status enum
 * @return Human-readable status string
 */
const char* ota_status_to_string(ota_status_t status);

/**
 * @brief Reboot to apply pending update
 *
 * Only call after OTA_STATUS_PENDING_REBOOT
 */
void ota_reboot(void);

#ifdef __cplusplus
}
#endif

#endif // OTA_UPDATE_H
