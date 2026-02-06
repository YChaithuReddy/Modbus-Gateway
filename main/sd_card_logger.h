#ifndef SD_CARD_LOGGER_H
#define SD_CARD_LOGGER_H

#include <stdbool.h>
#include "esp_err.h"

// SD Card recovery configuration
#define SD_CARD_MAX_RETRIES 3              // Max retries for each operation
#define SD_CARD_RETRY_DELAY_MS 100         // Delay between retries
#define SD_CARD_RECOVERY_INTERVAL_SEC 60   // Try to recover failed SD card every 60 seconds
#define SD_CARD_RAM_BUFFER_SIZE 3          // Reduced from 5 to save ~1.3KB (15min offline buffer at 5min interval)

// SD Card status
typedef struct {
    bool initialized;
    bool card_available;
    uint64_t card_size_mb;
    uint64_t free_space_mb;
    uint32_t error_count;           // Track consecutive errors
    int64_t last_error_time;        // Time of last error
    int64_t last_recovery_attempt;  // Time of last recovery attempt
} sd_card_status_t;

// Message structure for pending telemetry
typedef struct {
    uint32_t message_id;
    char timestamp[32];
    char topic[128];
    char payload[512];
} pending_message_t;

// SD Card initialization and management
esp_err_t sd_card_init(void);
esp_err_t sd_card_deinit(void);
bool sd_card_is_available(void);
esp_err_t sd_card_get_status(sd_card_status_t* status);
esp_err_t sd_card_check_space(uint64_t required_bytes);

// Message persistence functions
esp_err_t sd_card_save_message(const char* topic, const char* payload, const char* timestamp);
esp_err_t sd_card_get_pending_count(uint32_t* count);
esp_err_t sd_card_replay_messages(void (*publish_callback)(const pending_message_t* msg));
esp_err_t sd_card_remove_message(uint32_t message_id);
esp_err_t sd_card_clear_all_messages(void);

// Message ID management
uint32_t sd_card_get_next_message_id(void);
esp_err_t sd_card_restore_message_counter(void);

// Recovery and fallback functions
esp_err_t sd_card_attempt_recovery(void);      // Try to recover failed SD card
bool sd_card_needs_recovery(void);              // Check if SD card needs recovery attempt
esp_err_t sd_card_flush_ram_buffer(void);       // Flush RAM buffer to SD card when available
uint32_t sd_card_get_ram_buffer_count(void);    // Get count of messages in RAM buffer
void sd_card_reset_error_count(void);           // Reset error counter after successful operation

#endif // SD_CARD_LOGGER_H
