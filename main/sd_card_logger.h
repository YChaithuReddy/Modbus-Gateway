#ifndef SD_CARD_LOGGER_H
#define SD_CARD_LOGGER_H

#include <stdbool.h>
#include "esp_err.h"

// SD Card status
typedef struct {
    bool initialized;
    bool card_available;
    uint64_t card_size_mb;
    uint64_t free_space_mb;
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

#endif // SD_CARD_LOGGER_H
