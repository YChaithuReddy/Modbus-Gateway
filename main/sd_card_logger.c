#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/unistd.h>
#include <dirent.h>
#include <errno.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "driver/sdspi_host.h"
#include "driver/spi_common.h"
#include "sd_card_logger.h"
#include "iot_configs.h"

static const char *TAG = "SD_CARD";

// Hardware pin configuration (VSPI native pins)
#define SD_CARD_MOSI GPIO_NUM_23
#define SD_CARD_MISO GPIO_NUM_19
#define SD_CARD_CLK GPIO_NUM_5
#define SD_CARD_CS GPIO_NUM_15
#define SD_CARD_SPI_HOST SPI3_HOST

// SD Card state
static bool sd_initialized = false;
static bool sd_available = false;
static sdmmc_card_t *card = NULL;
static uint32_t message_id_counter = 0;
static const char* mount_point = "/sdcard";
// Use 8.3 short filenames for maximum FAT compatibility
static const char* pending_messages_file = "/sdcard/msgs.txt";  // Even shorter
static const char* temp_messages_file = "/sdcard/tmp.txt";  // Even shorter

// Error tracking for recovery
static uint32_t sd_error_count = 0;
static int64_t last_error_time = 0;
static int64_t last_recovery_attempt = 0;

// RAM buffer for messages when SD card fails
typedef struct {
    bool valid;
    char timestamp[32];
    char topic[128];
    char payload[512];
} ram_buffer_message_t;

static ram_buffer_message_t ram_buffer[SD_CARD_RAM_BUFFER_SIZE];
static uint32_t ram_buffer_count = 0;
static uint32_t ram_buffer_write_index = 0;

// Forward declarations
static esp_err_t sd_card_add_to_ram_buffer(const char* topic, const char* payload, const char* timestamp);

// Minimum free space required (in bytes) - 1MB
#define MIN_FREE_SPACE_BYTES (1024 * 1024)

// Maximum message file size (10MB)
#define MAX_MESSAGE_FILE_SIZE (10 * 1024 * 1024)

// Helper function to detect corrupted/binary data in a string
// Returns true if the string contains non-printable or invalid characters
static bool is_corrupted_line(const char* line) {
    if (line == NULL || *line == '\0') {
        return true;
    }

    int corrupt_count = 0;
    int total_chars = 0;

    for (const char* p = line; *p != '\0'; p++) {
        total_chars++;
        unsigned char c = (unsigned char)*p;

        // Valid characters: printable ASCII (32-126), tab (9), newline (10), carriage return (13)
        // Also allow common extended ASCII for UTF-8 compatibility (but not garbage 0x80-0x9F range)
        if (c < 32 && c != 9 && c != 10 && c != 13) {
            corrupt_count++;  // Control characters (except tab, newline, CR)
        } else if (c == 127) {
            corrupt_count++;  // DEL character
        } else if (c >= 0x80 && c <= 0x9F) {
            corrupt_count++;  // Invalid UTF-8 continuation bytes used alone
        } else if (c == 0xFF) {
            corrupt_count++;  // Common SD card corruption pattern
        }
    }

    // If more than 10% of characters are corrupt, consider the line corrupted
    // Or if there are more than 5 corrupt characters
    if (corrupt_count > 5 || (total_chars > 0 && (corrupt_count * 100 / total_chars) > 10)) {
        return true;
    }

    return false;
}

// Helper function to check if a message ID is valid (numeric)
static bool is_valid_message_id(const char* id_str) {
    if (id_str == NULL || *id_str == '\0') {
        return false;
    }

    for (const char* p = id_str; *p != '\0'; p++) {
        if (*p < '0' || *p > '9') {
            return false;  // Non-numeric character found
        }
    }

    return true;
}

// Helper function to validate timestamp format (YYYY-MM-DDTHH:MM:SSZ)
static bool is_valid_timestamp(const char* timestamp) {
    if (timestamp == NULL || strlen(timestamp) < 19) {
        return false;
    }

    // Check basic format: should start with 20XX- (years 2000-2099)
    if (timestamp[0] != '2' || timestamp[1] != '0') {
        return false;
    }

    // Check for year 1970 (invalid RTC)
    if (strncmp(timestamp, "1970-", 5) == 0) {
        return false;
    }

    // Check for year 2000 (invalid RTC)
    if (strncmp(timestamp, "2000-", 5) == 0) {
        return false;
    }

    // Basic format check: YYYY-MM-DDTHH:MM:SS
    if (timestamp[4] != '-' || timestamp[7] != '-' || timestamp[10] != 'T') {
        return false;
    }

    return true;
}

// Initialize SD card with SPI interface
esp_err_t sd_card_init(void) {
    if (sd_initialized) {
        ESP_LOGW(TAG, "SD card already initialized");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "🔧 Initializing SD Card on SPI...");
    ESP_LOGI(TAG, "📍 Pin Configuration:");
    ESP_LOGI(TAG, "   CS:   GPIO %d", SD_CARD_CS);
    ESP_LOGI(TAG, "   MOSI: GPIO %d", SD_CARD_MOSI);
    ESP_LOGI(TAG, "   MISO: GPIO %d", SD_CARD_MISO);
    ESP_LOGI(TAG, "   CLK:  GPIO %d", SD_CARD_CLK);
    ESP_LOGI(TAG, "   Host: SPI%d", SD_CARD_SPI_HOST + 1);

    // Enable internal pull-ups on SPI lines first
    ESP_LOGI(TAG, "🔌 Enabling pull-up resistors...");
    gpio_set_pull_mode(SD_CARD_MISO, GPIO_PULLUP_ONLY);
    gpio_set_pull_mode(SD_CARD_MOSI, GPIO_PULLUP_ONLY);
    gpio_set_pull_mode(SD_CARD_CLK, GPIO_PULLUP_ONLY);
    gpio_set_pull_mode(SD_CARD_CS, GPIO_PULLUP_ONLY);

    // Configure SPI bus
    ESP_LOGI(TAG, "📡 Configuring SPI bus...");
    spi_bus_config_t bus_cfg = {
        .mosi_io_num = SD_CARD_MOSI,
        .miso_io_num = SD_CARD_MISO,
        .sclk_io_num = SD_CARD_CLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4000,
        .flags = SPICOMMON_BUSFLAG_MASTER,
    };

    ESP_LOGI(TAG, "🚀 Initializing SPI bus...");
    esp_err_t ret = spi_bus_initialize(SD_CARD_SPI_HOST, &bus_cfg, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "Failed to initialize SPI bus: %s", esp_err_to_name(ret));
        return ret;
    }

    // CRITICAL: Add delay after SPI initialization (matching Arduino code line 158)
    ESP_LOGI(TAG, "⏳ Waiting for SPI bus to stabilize...");
    vTaskDelay(pdMS_TO_TICKS(1000));  // 1 second delay like Arduino

    // Configure SD host and slot
    ESP_LOGI(TAG, "⚙️ Configuring SD host...");
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = SD_CARD_SPI_HOST;

    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs = SD_CARD_CS;
    slot_config.host_id = SD_CARD_SPI_HOST;

    // Use 1MHz like the working Arduino example (not too slow, not too fast)
    host.max_freq_khz = 1000;  // 1MHz - same as working Arduino code
    ESP_LOGI(TAG, "📶 SPI Frequency: %d kHz (matching working Arduino implementation)", host.max_freq_khz);

    // Mount FAT filesystem
    ESP_LOGI(TAG, "💾 Mounting FAT filesystem...");
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = true,  // Auto-format if needed (like Arduino SD library)
        .max_files = 5,
        .allocation_unit_size = 0  // Use default allocation unit (let FAT decide)
    };

    ESP_LOGI(TAG, "🔍 Attempting to detect and initialize SD card...");
    ESP_LOGI(TAG, "   (This may take a few seconds)");
    ret = esp_vfs_fat_sdspi_mount(mount_point, &host, &slot_config, &mount_config, &card);

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "❌ Failed to initialize SD card: %s (0x%x)", esp_err_to_name(ret), ret);

        // Provide specific troubleshooting advice based on error
        if (ret == ESP_ERR_TIMEOUT || ret == 0x108) {
            ESP_LOGE(TAG, "");
            ESP_LOGE(TAG, "+========================================================+");
            ESP_LOGE(TAG, "|  SD CARD NOT RESPONDING - Check the following:        |");
            ESP_LOGE(TAG, "+========================================================+");
            ESP_LOGE(TAG, "");
            ESP_LOGE(TAG, "1. ✓ Is SD card inserted properly?");
            ESP_LOGE(TAG, "2. ✓ Is SD card formatted as FAT32?");
            ESP_LOGE(TAG, "3. ✓ Check wiring connections:");
            ESP_LOGE(TAG, "     CS:   GPIO %d → SD Card CS pin", SD_CARD_CS);
            ESP_LOGE(TAG, "     MOSI: GPIO %d → SD Card MOSI/DI pin", SD_CARD_MOSI);
            ESP_LOGE(TAG, "     MISO: GPIO %d → SD Card MISO/DO pin", SD_CARD_MISO);
            ESP_LOGE(TAG, "     CLK:  GPIO %d → SD Card CLK/SCK pin", SD_CARD_CLK);
            ESP_LOGE(TAG, "     VCC:  3.3V (NOT 5V!)");
            ESP_LOGE(TAG, "     GND:  GND");
            ESP_LOGE(TAG, "4. ✓ Try a different SD card");
            ESP_LOGE(TAG, "5. ✓ Check if SD card works in computer");
            ESP_LOGE(TAG, "");
            ESP_LOGE(TAG, "💡 System will continue WITHOUT SD card logging");
            ESP_LOGE(TAG, "");
        } else if (ret == ESP_ERR_INVALID_CRC || ret == 0x109) {
            ESP_LOGE(TAG, "");
            ESP_LOGE(TAG, "+========================================================+");
            ESP_LOGE(TAG, "|  SD CARD CRC ERROR - Data Corruption Detected         |");
            ESP_LOGE(TAG, "+========================================================+");
            ESP_LOGE(TAG, "");
            ESP_LOGE(TAG, "The SD card is responding but data is corrupted.");
            ESP_LOGE(TAG, "");
            ESP_LOGE(TAG, "Most likely causes:");
            ESP_LOGE(TAG, "  1. ⚠️ BAD/FAULTY SD CARD - Try a different card!");
            ESP_LOGE(TAG, "  2. ⚠️ Poor wiring - Check for loose connections");
            ESP_LOGE(TAG, "  3. ⚠️ Electrical interference - Keep wires short");
            ESP_LOGE(TAG, "  4. ⚠️ Card not fully inserted");
            ESP_LOGE(TAG, "");
            ESP_LOGE(TAG, "Recommended SD cards:");
            ESP_LOGE(TAG, "  * SanDisk, Samsung, or Kingston brand");
            ESP_LOGE(TAG, "  * 2GB - 16GB size");
            ESP_LOGE(TAG, "  * Class 4 or Class 10");
            ESP_LOGE(TAG, "  * Formatted as FAT32");
            ESP_LOGE(TAG, "");
            ESP_LOGE(TAG, "💡 System will continue WITHOUT SD card logging");
            ESP_LOGE(TAG, "");
        } else if (ret == ESP_FAIL) {
            ESP_LOGE(TAG, "❌ Failed to mount filesystem - card may not be formatted");
        }

        // CRITICAL: Free SPI bus on mount failure to allow recovery attempts
        // Without this, the SPI bus remains allocated and subsequent init attempts fail
        spi_bus_free(SD_CARD_SPI_HOST);
        ESP_LOGI(TAG, "SPI bus freed for future recovery attempts");

        sd_initialized = false;
        sd_available = false;
        return ret;
    }

    sd_initialized = true;
    sd_available = true;

    // Print card info
    ESP_LOGI(TAG, "✅ SD Card initialized successfully");
    ESP_LOGI(TAG, "📋 Card Info:");
    ESP_LOGI(TAG, "   Name: %s", card->cid.name);

    // Determine card type based on capacity
    const char* card_type = "SDSC";
    if (card->is_sdio) {
        card_type = "SDIO";
    } else if (card->is_mmc) {
        card_type = "MMC";
    } else if (card->ocr & (1 << 30)) {  // CCS bit indicates SDHC/SDXC
        card_type = "SDHC/SDXC";
    }

    ESP_LOGI(TAG, "   Type: %s", card_type);
    ESP_LOGI(TAG, "   Speed: %s", (card->csd.tr_speed > 25000000) ? "High Speed" : "Default Speed");
    ESP_LOGI(TAG, "   Size: %lluMB", ((uint64_t) card->csd.capacity) * card->csd.sector_size / (1024 * 1024));

    // Restore message ID counter
    sd_card_restore_message_counter();

    // Write test with proper validation
    ESP_LOGI(TAG, "🧪 Testing SD card write capability...");
    const char* test_file = "/sdcard/test.txt";
    const char* test_content = "SD card test\n";
    const size_t expected_size = strlen(test_content);

    FILE* test_fp = fopen(test_file, "w");
    if (test_fp == NULL) {
        ESP_LOGE(TAG, "❌ Failed to open test file for writing");
        ESP_LOGE(TAG, "   errno: %d (%s)", errno, strerror(errno));
        ESP_LOGE(TAG, "");
        ESP_LOGE(TAG, "💡 TIP: Reformat SD card as FAT32 and try again");
        ESP_LOGE(TAG, "");
        sd_available = false;
        return ESP_FAIL;
    }

    // Write and check return value
    int written = fprintf(test_fp, "%s", test_content);
    if (written < 0) {
        ESP_LOGE(TAG, "❌ fprintf failed: errno %d (%s)", errno, strerror(errno));
        fclose(test_fp);
        unlink(test_file);
        sd_available = false;
        return ESP_FAIL;
    }

    // Flush to ensure data is written to card
    if (fflush(test_fp) != 0) {
        ESP_LOGE(TAG, "❌ fflush failed: errno %d (%s)", errno, strerror(errno));
        fclose(test_fp);
        unlink(test_file);
        sd_available = false;
        return ESP_FAIL;
    }

    // Sync to disk (force physical write)
    fsync(fileno(test_fp));
    fclose(test_fp);

    // Verify file was written with correct size
    struct stat test_st;
    if (stat(test_file, &test_st) != 0) {
        ESP_LOGE(TAG, "❌ Test file was not created!");
        sd_available = false;
        return ESP_FAIL;
    }

    if (test_st.st_size == 0) {
        ESP_LOGE(TAG, "❌ Test file is 0 bytes - write failed!");
        ESP_LOGE(TAG, "   This usually indicates SD card communication issues.");
        ESP_LOGE(TAG, "   Try: 1) Re-seat the SD card  2) Use a different card  3) Check wiring");
        unlink(test_file);
        sd_available = false;
        return ESP_FAIL;
    }

    if ((size_t)test_st.st_size < expected_size) {
        ESP_LOGW(TAG, "⚠️ Test file size mismatch: expected %u, got %ld bytes",
                 expected_size, test_st.st_size);
    }

    ESP_LOGI(TAG, "✅ SD card write test successful! (%ld bytes written)", test_st.st_size);
    unlink(test_file);

    return ESP_OK;
}

// Deinitialize SD card
esp_err_t sd_card_deinit(void) {
    if (!sd_initialized) {
        return ESP_OK;
    }

    esp_err_t ret = esp_vfs_fat_sdcard_unmount(mount_point, card);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to unmount SD card: %s", esp_err_to_name(ret));
        return ret;
    }

    spi_bus_free(SD_CARD_SPI_HOST);

    sd_initialized = false;
    sd_available = false;
    card = NULL;

    ESP_LOGI(TAG, "SD Card deinitialized");
    return ESP_OK;
}

// Check if SD card is available
bool sd_card_is_available(void) {
    return sd_available;
}

// Get SD card status
esp_err_t sd_card_get_status(sd_card_status_t* status) {
    if (status == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    status->initialized = sd_initialized;
    status->card_available = sd_available;

    if (sd_available && card != NULL) {
        status->card_size_mb = ((uint64_t) card->csd.capacity) * card->csd.sector_size / (1024 * 1024);

        // Get free space using FATFS
        FATFS *fs;
        DWORD fre_clust;
        if (f_getfree("0:", &fre_clust, &fs) == FR_OK) {
            uint64_t free_bytes = (uint64_t)fre_clust * fs->csize * fs->ssize;
            status->free_space_mb = free_bytes / (1024 * 1024);
        } else {
            status->free_space_mb = 0;
        }
    } else {
        status->card_size_mb = 0;
        status->free_space_mb = 0;
    }

    return ESP_OK;
}

// Check if enough space is available
esp_err_t sd_card_check_space(uint64_t required_bytes) {
    if (!sd_available) {
        return ESP_ERR_INVALID_STATE;
    }

    FATFS *fs;
    DWORD fre_clust;

    if (f_getfree("0:", &fre_clust, &fs) != FR_OK) {
        ESP_LOGW(TAG, "Failed to get free space");
        return ESP_FAIL;
    }

    uint64_t free_bytes = (uint64_t)fre_clust * fs->csize * fs->ssize;

    if (free_bytes < (required_bytes + MIN_FREE_SPACE_BYTES)) {
        ESP_LOGW(TAG, "⚠️ Insufficient space: %lluKB free, %lluKB required",
                 free_bytes / 1024, (required_bytes + MIN_FREE_SPACE_BYTES) / 1024);
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}

// Delete oldest messages to free up space
static esp_err_t sd_card_cleanup_oldest_messages(uint32_t count_to_delete) {
    if (count_to_delete == 0) return ESP_OK;

    ESP_LOGI(TAG, "🧹 Cleaning up %lu oldest messages to free space...", count_to_delete);

    FILE *file = fopen(pending_messages_file, "r");
    if (file == NULL) {
        return ESP_OK;  // No file means no messages to delete
    }

    // Find the oldest message IDs
    char line[700];
    uint32_t deleted = 0;

    while (fgets(line, sizeof(line), file) != NULL && deleted < count_to_delete) {
        line[strcspn(line, "\r\n")] = 0;
        if (strlen(line) < 10) continue;

        char *id_str = strtok(line, "|");
        if (id_str != NULL) {
            uint32_t msg_id = atoi(id_str);
            fclose(file);
            sd_card_remove_message(msg_id);
            ESP_LOGI(TAG, "🗑️ Deleted old message ID %lu to free space", msg_id);
            deleted++;

            // Reopen file for next iteration
            file = fopen(pending_messages_file, "r");
            if (file == NULL) break;
        }
    }

    if (file != NULL) fclose(file);
    ESP_LOGI(TAG, "✅ Cleaned up %lu messages", deleted);
    return ESP_OK;
}

// Internal function to attempt saving message (single try)
static esp_err_t sd_card_save_message_internal(const char* topic, const char* payload, const char* timestamp) {
    // Quick filesystem health check (verify mount point exists)
    struct stat mount_st;
    if (stat(mount_point, &mount_st) != 0) {
        ESP_LOGE(TAG, "❌ Mount point %s no longer exists!", mount_point);
        return ESP_ERR_INVALID_STATE;
    }

    // Try to open the actual message file
    FILE *file = fopen(pending_messages_file, "a");

    // If append fails with EINVAL, try creating file explicitly first
    if (file == NULL && errno == EINVAL) {
        FILE *create_file = fopen(pending_messages_file, "w");
        if (create_file) {
            fclose(create_file);
            file = fopen(pending_messages_file, "a");
        }
    }

    if (file == NULL) {
        return ESP_FAIL;
    }

    // Increment message ID and write (matching Arduino format)
    message_id_counter++;
    int written = fprintf(file, "%lu|%s|%s|%s\n", message_id_counter, timestamp, topic, payload);
    fflush(file);  // Force write to disk
    fclose(file);

    if (written > 0) {
        return ESP_OK;
    }
    return ESP_FAIL;
}

// Save message to SD card with retry logic and RAM buffer fallback
esp_err_t sd_card_save_message(const char* topic, const char* payload, const char* timestamp) {
    // Validate parameters first
    if (topic == NULL || payload == NULL || timestamp == NULL) {
        ESP_LOGE(TAG, "Invalid message parameters");
        return ESP_ERR_INVALID_ARG;
    }

    if (strlen(topic) > 128 || strlen(payload) > 512) {
        ESP_LOGE(TAG, "Message too large to save");
        return ESP_ERR_INVALID_SIZE;
    }

    // If SD card is not available, use RAM buffer fallback
    if (!sd_available) {
        ESP_LOGW(TAG, "SD card not available - using RAM buffer fallback");
        sd_error_count++;
        last_error_time = esp_timer_get_time() / 1000000;
        return sd_card_add_to_ram_buffer(topic, payload, timestamp);
    }

    // Check available space and cleanup if needed
    uint64_t estimated_msg_size = strlen(topic) + strlen(payload) + 64;
    if (sd_card_check_space(estimated_msg_size) != ESP_OK) {
        ESP_LOGW(TAG, "⚠️ SD card low on space - cleaning up old messages...");
        sd_card_cleanup_oldest_messages(10);

        if (sd_card_check_space(estimated_msg_size) != ESP_OK) {
            ESP_LOGE(TAG, "❌ SD card still full - using RAM buffer");
            return sd_card_add_to_ram_buffer(topic, payload, timestamp);
        }
    }

    // Try to save with retries
    esp_err_t result = ESP_FAIL;
    for (int retry = 0; retry < SD_CARD_MAX_RETRIES; retry++) {
        if (retry > 0) {
            ESP_LOGW(TAG, "🔄 Retry %d/%d for SD card write...", retry + 1, SD_CARD_MAX_RETRIES);
            vTaskDelay(pdMS_TO_TICKS(SD_CARD_RETRY_DELAY_MS * (retry + 1)));  // Increasing delay
        }

        result = sd_card_save_message_internal(topic, payload, timestamp);

        if (result == ESP_OK) {
            ESP_LOGI(TAG, "💾 Message saved to SD card with ID: %lu", message_id_counter);
            sd_error_count = 0;  // Reset error count on success
            return ESP_OK;
        }

        ESP_LOGW(TAG, "⚠️ SD card write attempt %d failed (errno: %d)", retry + 1, errno);
    }

    // All retries failed - try recovery
    ESP_LOGE(TAG, "❌ All SD card write retries failed - attempting recovery...");
    sd_error_count++;
    last_error_time = esp_timer_get_time() / 1000000;

    // Try to unmount and remount
    if (card != NULL) {
        ESP_LOGI(TAG, "Unmounting SD card for recovery...");
        esp_vfs_fat_sdcard_unmount(mount_point, card);
        card = NULL;
    }

    sd_available = false;
    sd_initialized = false;
    vTaskDelay(pdMS_TO_TICKS(500));

    // Attempt reinit
    ESP_LOGI(TAG, "Attempting to reinitialize SD card...");
    if (sd_card_init() == ESP_OK) {
        ESP_LOGI(TAG, "✅ SD card reinitialized - retrying save...");

        // One more try after recovery
        result = sd_card_save_message_internal(topic, payload, timestamp);
        if (result == ESP_OK) {
            ESP_LOGI(TAG, "💾 Message saved after recovery with ID: %lu", message_id_counter);
            sd_error_count = 0;
            return ESP_OK;
        }
    }

    // Recovery failed - use RAM buffer as last resort
    ESP_LOGE(TAG, "❌ SD card recovery failed - saving to RAM buffer");
    return sd_card_add_to_ram_buffer(topic, payload, timestamp);
}

// Get count of pending messages
esp_err_t sd_card_get_pending_count(uint32_t* count) {
    if (count == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!sd_available) {
        *count = 0;
        return ESP_ERR_INVALID_STATE;
    }

    FILE *file = fopen(pending_messages_file, "r");
    if (file == NULL) {
        *count = 0;
        return ESP_OK; // No file means no messages
    }

    uint32_t msg_count = 0;
    char line[700];

    while (fgets(line, sizeof(line), file) != NULL) {
        if (strlen(line) > 1) { // Skip empty lines
            msg_count++;
        }
    }

    fclose(file);
    *count = msg_count;

    return ESP_OK;
}

// Replay pending messages with callback
esp_err_t sd_card_replay_messages(void (*publish_callback)(const pending_message_t* msg)) {
    if (!sd_available) {
        ESP_LOGW(TAG, "SD card not available");
        return ESP_ERR_INVALID_STATE;
    }

    if (publish_callback == NULL) {
        ESP_LOGE(TAG, "Invalid callback function");
        return ESP_ERR_INVALID_ARG;
    }

    FILE *file = fopen(pending_messages_file, "r");
    if (file == NULL) {
        ESP_LOGI(TAG, "No pending messages file found");
        return ESP_OK;
    }

    uint32_t total_messages = 0;
    sd_card_get_pending_count(&total_messages);

    if (total_messages == 0) {
        ESP_LOGI(TAG, "No messages to replay");
        fclose(file);
        return ESP_OK;
    }

    ESP_LOGI(TAG, "📤 Found %lu pending messages to replay", total_messages);

    char line[700];
    char line_backup[700];  // Backup for logging corrupted lines
    uint32_t replayed_count = 0;
    uint32_t deleted_corrupt_count = 0;
    // Use config value for batch limit (defined in iot_configs.h)
    const uint32_t MAX_REPLAY_BATCH = SD_REPLAY_MAX_MESSAGES_PER_BATCH;

    while (fgets(line, sizeof(line), file) != NULL && replayed_count < MAX_REPLAY_BATCH) {
        // Make backup before modifying line (for logging)
        strncpy(line_backup, line, sizeof(line_backup) - 1);
        line_backup[sizeof(line_backup) - 1] = '\0';

        // Remove newline
        line[strcspn(line, "\r\n")] = 0;

        // === CORRUPTION DETECTION: Check for binary/garbage data ===
        if (is_corrupted_line(line)) {
            deleted_corrupt_count++;
            ESP_LOGW(TAG, "🗑️ CORRUPTED LINE DETECTED - auto-deleting (corrupt #%lu)", deleted_corrupt_count);
            ESP_LOGW(TAG, "   First 50 chars: %.50s...", line);

            // We need to rebuild the file without this corrupted line
            // Close current file, create temp without this line, replace original
            fclose(file);

            // Read all lines and rewrite without corrupted ones
            FILE *src = fopen(pending_messages_file, "r");
            FILE *dst = fopen(temp_messages_file, "w");
            if (src && dst) {
                char temp_line[700];
                while (fgets(temp_line, sizeof(temp_line), src) != NULL) {
                    // Only write non-corrupted lines
                    if (!is_corrupted_line(temp_line)) {
                        fputs(temp_line, dst);
                    }
                }
                fclose(src);
                fflush(dst);
                fclose(dst);

                // Replace original with cleaned file
                remove(pending_messages_file);
                rename(temp_messages_file, pending_messages_file);
                ESP_LOGI(TAG, "✅ Corrupted lines removed from SD card");
            } else {
                if (src) fclose(src);
                if (dst) fclose(dst);
                ESP_LOGE(TAG, "❌ Failed to clean corrupted lines");
            }

            // Reopen file and restart
            file = fopen(pending_messages_file, "r");
            if (file == NULL) {
                ESP_LOGI(TAG, "No more pending messages after corruption cleanup");
                return ESP_OK;
            }
            continue;
        }

        if (strlen(line) < 10) {
            continue; // Skip very short lines
        }

        // Parse line: ID|TIMESTAMP|TOPIC|PAYLOAD
        // Use manual parsing to safely handle payload that may contain pipes
        char *id_str = NULL;
        char *timestamp = NULL;
        char *topic = NULL;
        char *payload = NULL;
        int pipe_count = 0;
        char *ptr = line;
        char *field_start = line;

        // Find first 3 pipes and extract fields
        while (*ptr && pipe_count < 3) {
            if (*ptr == '|') {
                *ptr = '\0';  // Null terminate current field
                if (pipe_count == 0) id_str = field_start;
                else if (pipe_count == 1) timestamp = field_start;
                else if (pipe_count == 2) topic = field_start;
                field_start = ptr + 1;
                pipe_count++;
            }
            ptr++;
        }
        // Everything after 3rd pipe is payload (may contain pipes)
        if (pipe_count == 3 && *field_start != '\0') {
            payload = field_start;
        }

        // === VALIDATION: Check for malformed messages and auto-delete ===
        bool should_delete = false;
        const char* delete_reason = NULL;

        if (id_str == NULL || timestamp == NULL || topic == NULL || payload == NULL) {
            should_delete = true;
            delete_reason = "missing fields";
        } else if (!is_valid_message_id(id_str)) {
            should_delete = true;
            delete_reason = "invalid message ID (non-numeric)";
        } else if (!is_valid_timestamp(timestamp)) {
            should_delete = true;
            delete_reason = "invalid timestamp format";
        } else if (*timestamp == '\0') {
            should_delete = true;
            delete_reason = "empty timestamp";
        } else if (strstr(payload, "�") != NULL || is_corrupted_line(payload)) {
            should_delete = true;
            delete_reason = "corrupted payload data";
        }

        if (should_delete) {
            deleted_corrupt_count++;
            if (id_str && is_valid_message_id(id_str)) {
                uint32_t invalid_msg_id = atoi(id_str);
                ESP_LOGW(TAG, "🗑️ Deleting message ID %lu - %s", invalid_msg_id, delete_reason);
                fclose(file);
                sd_card_remove_message(invalid_msg_id);
            } else {
                // Can't get valid ID, need to clean the whole file of corrupt lines
                ESP_LOGW(TAG, "🗑️ Removing malformed line - %s", delete_reason);
                fclose(file);

                // Rewrite file without this line
                FILE *src = fopen(pending_messages_file, "r");
                FILE *dst = fopen(temp_messages_file, "w");
                if (src && dst) {
                    char temp_line[700];
                    bool skipped_one = false;
                    while (fgets(temp_line, sizeof(temp_line), src) != NULL) {
                        // Skip the first matching corrupted line
                        if (!skipped_one && strncmp(temp_line, line_backup, strlen(line_backup) - 1) == 0) {
                            skipped_one = true;
                            continue;
                        }
                        fputs(temp_line, dst);
                    }
                    fclose(src);
                    fflush(dst);
                    fclose(dst);
                    remove(pending_messages_file);
                    rename(temp_messages_file, pending_messages_file);
                } else {
                    if (src) fclose(src);
                    if (dst) fclose(dst);
                }
            }

            // Reopen file and continue
            file = fopen(pending_messages_file, "r");
            if (file == NULL) {
                ESP_LOGI(TAG, "No more pending messages after cleanup (deleted %lu corrupted)", deleted_corrupt_count);
                return ESP_OK;
            }
            continue;
        }

        // Validate topic - delete messages with placeholder device IDs
        if (strstr(topic, "your-device-id") != NULL) {
            uint32_t invalid_msg_id = atoi(id_str);
            ESP_LOGW(TAG, "🗑️ Deleting message ID %s - invalid topic with placeholder device ID", id_str);
            fclose(file);
            sd_card_remove_message(invalid_msg_id);
            file = fopen(pending_messages_file, "r");
            if (file == NULL) {
                ESP_LOGI(TAG, "No more pending messages after cleanup");
                return ESP_OK;
            }
            continue;
        }

        pending_message_t msg;
        memset(&msg, 0, sizeof(msg));  // Initialize to zero for safety
        msg.message_id = atoi(id_str);
        strncpy(msg.timestamp, timestamp, sizeof(msg.timestamp) - 1);
        msg.timestamp[sizeof(msg.timestamp) - 1] = '\0';  // Ensure null termination
        strncpy(msg.topic, topic, sizeof(msg.topic) - 1);
        msg.topic[sizeof(msg.topic) - 1] = '\0';  // Ensure null termination
        strncpy(msg.payload, payload, sizeof(msg.payload) - 1);
        msg.payload[sizeof(msg.payload) - 1] = '\0';  // Ensure null termination

        ESP_LOGI(TAG, "📤 Replaying message ID: %lu from %s", msg.message_id, msg.timestamp);

        // Call publish callback (rate limiting is handled in the callback using iot_configs.h values)
        publish_callback(&msg);
        replayed_count++;

        // Minimal yield to prevent watchdog and allow other tasks to run
        // Rate limiting delays are handled by the callback function
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    fclose(file);

    if (deleted_corrupt_count > 0) {
        ESP_LOGW(TAG, "🧹 Cleaned up %lu corrupted/invalid messages during replay", deleted_corrupt_count);
    }
    ESP_LOGI(TAG, "✅ Replayed %lu messages", replayed_count);
    return ESP_OK;
}

// Remove a specific message by ID
esp_err_t sd_card_remove_message(uint32_t message_id) {
    if (!sd_available || message_id == 0) {
        return ESP_ERR_INVALID_STATE;
    }

    FILE *source_file = fopen(pending_messages_file, "r");
    if (source_file == NULL) {
        ESP_LOGW(TAG, "No pending messages file to clean");
        return ESP_OK;
    }

    FILE *temp_file = fopen(temp_messages_file, "w");
    if (temp_file == NULL) {
        ESP_LOGE(TAG, "Failed to create temp file");
        fclose(source_file);
        return ESP_FAIL;
    }

    bool message_found = false;
    char line[700];

    while (fgets(line, sizeof(line), source_file) != NULL) {
        if (strlen(line) < 10) {
            continue;
        }

        // Extract message ID from line
        uint32_t line_msg_id = atoi(line);

        if (line_msg_id != message_id) {
            fputs(line, temp_file);
        } else {
            message_found = true;
            ESP_LOGI(TAG, "🗑️ Removing published message ID: %lu", message_id);
        }
    }

    fflush(temp_file);  // Ensure all data is written to disk
    fclose(source_file);
    fclose(temp_file);

    if (message_found) {
        // Remove old file
        if (remove(pending_messages_file) != 0) {
            ESP_LOGE(TAG, "❌ Failed to remove old messages file: %s", strerror(errno));
            remove(temp_messages_file);  // Clean up temp file
            return ESP_FAIL;
        }

        // Rename temp file to original
        if (rename(temp_messages_file, pending_messages_file) != 0) {
            ESP_LOGE(TAG, "❌ Failed to rename temp file: %s", strerror(errno));
            return ESP_FAIL;
        }

        ESP_LOGI(TAG, "✅ Successfully removed message ID %lu from SD card", message_id);
    } else {
        ESP_LOGW(TAG, "⚠️ Message ID %lu not found in SD card", message_id);
        remove(temp_messages_file);
    }

    return ESP_OK;
}

// Clear all pending messages
esp_err_t sd_card_clear_all_messages(void) {
    if (!sd_available) {
        return ESP_ERR_INVALID_STATE;
    }

    if (remove(pending_messages_file) == 0) {
        ESP_LOGI(TAG, "✅ All pending messages cleared");
        return ESP_OK;
    } else {
        ESP_LOGW(TAG, "No messages file to clear");
        return ESP_OK; // Not an error if file doesn't exist
    }
}

// Get next message ID
uint32_t sd_card_get_next_message_id(void) {
    return message_id_counter + 1;
}

// Restore message ID counter from existing messages
esp_err_t sd_card_restore_message_counter(void) {
    if (!sd_available) {
        return ESP_ERR_INVALID_STATE;
    }

    FILE *file = fopen(pending_messages_file, "r");
    if (file == NULL) {
        ESP_LOGI(TAG, "No existing messages file - starting with ID counter 0");
        message_id_counter = 0;
        return ESP_OK;
    }

    uint32_t max_id = 0;
    uint32_t message_count = 0;
    char line[700];

    while (fgets(line, sizeof(line), file) != NULL) {
        if (strlen(line) < 10) {
            continue;
        }

        uint32_t msg_id = atoi(line);
        if (msg_id > max_id) {
            max_id = msg_id;
        }
        message_count++;
    }

    fclose(file);

    message_id_counter = max_id;
    ESP_LOGI(TAG, "📋 Restored message ID counter to: %lu", message_id_counter);
    ESP_LOGI(TAG, "📋 Found %lu existing messages on SD card", message_count);

    return ESP_OK;
}

// ============================================================================
// SD Card Recovery and RAM Buffer Functions
// ============================================================================

// Reset error counter (call after successful operation)
void sd_card_reset_error_count(void) {
    sd_error_count = 0;
}

// Check if SD card needs recovery attempt
bool sd_card_needs_recovery(void) {
    if (sd_available) {
        return false;  // SD card is working, no recovery needed
    }

    // Check if enough time has passed since last recovery attempt
    int64_t current_time = esp_timer_get_time() / 1000000;  // Convert to seconds
    if (current_time - last_recovery_attempt < SD_CARD_RECOVERY_INTERVAL_SEC) {
        return false;  // Too soon to retry
    }

    return true;
}

// Attempt to recover failed SD card
esp_err_t sd_card_attempt_recovery(void) {
    int64_t current_time = esp_timer_get_time() / 1000000;
    last_recovery_attempt = current_time;

    ESP_LOGI(TAG, "🔄 Attempting SD card recovery...");

    // If card was previously initialized, try to unmount first
    if (card != NULL) {
        ESP_LOGI(TAG, "   Unmounting previous SD card instance...");
        esp_vfs_fat_sdcard_unmount(mount_point, card);
        card = NULL;
        vTaskDelay(pdMS_TO_TICKS(500));  // Wait for cleanup
    }

    // Reset state
    sd_initialized = false;
    sd_available = false;

    // Wait a bit before trying to reinitialize
    vTaskDelay(pdMS_TO_TICKS(1000));

    // Try to reinitialize
    esp_err_t ret = sd_card_init();

    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "✅ SD card recovery successful!");
        sd_error_count = 0;

        // Try to flush RAM buffer to SD card
        if (ram_buffer_count > 0) {
            ESP_LOGI(TAG, "📤 Flushing %lu messages from RAM buffer to SD card...", ram_buffer_count);
            sd_card_flush_ram_buffer();
        }

        return ESP_OK;
    } else {
        ESP_LOGW(TAG, "❌ SD card recovery failed (error: 0x%x)", ret);
        return ret;
    }
}

// Get count of messages in RAM buffer
uint32_t sd_card_get_ram_buffer_count(void) {
    return ram_buffer_count;
}

// Add message to RAM buffer (fallback when SD fails)
static esp_err_t sd_card_add_to_ram_buffer(const char* topic, const char* payload, const char* timestamp) {
    if (ram_buffer_count >= SD_CARD_RAM_BUFFER_SIZE) {
        // Buffer full - overwrite oldest message (circular buffer)
        ESP_LOGW(TAG, "⚠️ RAM buffer full, overwriting oldest message");
    }

    // Find next slot (circular buffer)
    uint32_t index = ram_buffer_write_index % SD_CARD_RAM_BUFFER_SIZE;

    ram_buffer[index].valid = true;
    strncpy(ram_buffer[index].timestamp, timestamp, sizeof(ram_buffer[index].timestamp) - 1);
    ram_buffer[index].timestamp[sizeof(ram_buffer[index].timestamp) - 1] = '\0';
    strncpy(ram_buffer[index].topic, topic, sizeof(ram_buffer[index].topic) - 1);
    ram_buffer[index].topic[sizeof(ram_buffer[index].topic) - 1] = '\0';
    strncpy(ram_buffer[index].payload, payload, sizeof(ram_buffer[index].payload) - 1);
    ram_buffer[index].payload[sizeof(ram_buffer[index].payload) - 1] = '\0';

    ram_buffer_write_index++;
    if (ram_buffer_count < SD_CARD_RAM_BUFFER_SIZE) {
        ram_buffer_count++;
    }

    ESP_LOGI(TAG, "💾 Message saved to RAM buffer (%lu/%d messages)",
             ram_buffer_count, SD_CARD_RAM_BUFFER_SIZE);

    return ESP_OK;
}

// Flush RAM buffer to SD card
esp_err_t sd_card_flush_ram_buffer(void) {
    if (!sd_available) {
        ESP_LOGW(TAG, "Cannot flush RAM buffer - SD card not available");
        return ESP_ERR_INVALID_STATE;
    }

    if (ram_buffer_count == 0) {
        return ESP_OK;  // Nothing to flush
    }

    ESP_LOGI(TAG, "📤 Flushing %lu messages from RAM buffer to SD card...", ram_buffer_count);

    uint32_t flushed = 0;
    uint32_t failed = 0;

    // Calculate starting index for reading (handle circular buffer)
    uint32_t start_index = 0;
    if (ram_buffer_write_index > SD_CARD_RAM_BUFFER_SIZE) {
        start_index = ram_buffer_write_index % SD_CARD_RAM_BUFFER_SIZE;
    }

    for (uint32_t i = 0; i < ram_buffer_count; i++) {
        uint32_t index = (start_index + i) % SD_CARD_RAM_BUFFER_SIZE;

        if (!ram_buffer[index].valid) {
            continue;
        }

        // Open file for append
        FILE *file = fopen(pending_messages_file, "a");
        if (file == NULL) {
            ESP_LOGE(TAG, "Failed to open file for RAM buffer flush");
            failed++;
            continue;
        }

        message_id_counter++;
        int written = fprintf(file, "%lu|%s|%s|%s\n",
                              message_id_counter,
                              ram_buffer[index].timestamp,
                              ram_buffer[index].topic,
                              ram_buffer[index].payload);
        fflush(file);
        fclose(file);

        if (written > 0) {
            ram_buffer[index].valid = false;
            flushed++;
        } else {
            failed++;
        }

        vTaskDelay(pdMS_TO_TICKS(50));  // Small delay between writes
    }

    // Reset RAM buffer
    ram_buffer_count = 0;
    ram_buffer_write_index = 0;

    ESP_LOGI(TAG, "✅ RAM buffer flush complete: %lu saved, %lu failed", flushed, failed);

    return (failed == 0) ? ESP_OK : ESP_FAIL;
}
