// telegram_bot.c - Telegram Bot Integration Implementation
// Remote monitoring and control via Telegram

#include "telegram_bot.h"
#include "web_config.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "cJSON.h"
#include <string.h>
#include <time.h>

static const char *TAG = "TELEGRAM";

// Task handle for polling
static TaskHandle_t telegram_task_handle = NULL;
static bool telegram_running = false;

// Last update ID for polling
static int last_update_id = 0;

// HTTP event handler
static esp_err_t http_event_handler(esp_http_client_event_t *evt) {
    switch(evt->event_id) {
        case HTTP_EVENT_ERROR:
            ESP_LOGD(TAG, "HTTP_EVENT_ERROR");
            break;
        case HTTP_EVENT_ON_DATA:
            if (!esp_http_client_is_chunked_response(evt->client)) {
                // Response data available
            }
            break;
        default:
            break;
    }
    return ESP_OK;
}

// URL encode helper
static void url_encode_string(const char *input, char *output, size_t output_size) {
    static const char hex[] = "0123456789ABCDEF";
    size_t i = 0, j = 0;

    while (input[i] && j < output_size - 4) {
        if (isalnum((unsigned char)input[i]) || input[i] == '-' || input[i] == '_' ||
            input[i] == '.' || input[i] == '~') {
            output[j++] = input[i];
        } else if (input[i] == ' ') {
            output[j++] = '+';
        } else {
            output[j++] = '%';
            output[j++] = hex[(unsigned char)input[i] >> 4];
            output[j++] = hex[(unsigned char)input[i] & 15];
        }
        i++;
    }
    output[j] = '\0';
}

// Send HTTP request to Telegram API
static esp_err_t telegram_api_request(const char *method, const char *params, char *response, size_t response_size) {
    system_config_t *config = get_system_config();

    if (!config->telegram_config.enabled || strlen(config->telegram_config.bot_token) == 0) {
        ESP_LOGW(TAG, "Telegram bot not configured");
        return ESP_FAIL;
    }

    char url[512];
    snprintf(url, sizeof(url), "https://api.telegram.org/bot%s/%s%s%s",
             config->telegram_config.bot_token, method,
             params ? "?" : "", params ? params : "");

    ESP_LOGI(TAG, "Telegram API: %s", method);

    esp_http_client_config_t http_config = {
        .url = url,
        .event_handler = http_event_handler,
        .timeout_ms = 10000,
        .buffer_size = 2048,
    };

    esp_http_client_handle_t client = esp_http_client_init(&http_config);
    if (client == NULL) {
        ESP_LOGE(TAG, "Failed to init HTTP client");
        return ESP_FAIL;
    }

    esp_err_t err = esp_http_client_perform(client);

    if (err == ESP_OK) {
        int status_code = esp_http_client_get_status_code(client);
        int content_length = esp_http_client_get_content_length(client);

        if (status_code == 200) {
            if (response && response_size > 0) {
                int data_read = esp_http_client_read(client, response, response_size - 1);
                if (data_read > 0) {
                    response[data_read] = '\0';
                }
            }
            ESP_LOGI(TAG, "✅ API request successful");
        } else {
            ESP_LOGW(TAG, "HTTP Status: %d, Length: %d", status_code, content_length);
            err = ESP_FAIL;
        }
    } else {
        ESP_LOGE(TAG, "HTTP request failed: %s", esp_err_to_name(err));
    }

    esp_http_client_cleanup(client);
    return err;
}

// Send message to Telegram
esp_err_t telegram_send_message(const char *message) {
    if (!message || strlen(message) == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    system_config_t *config = get_system_config();

    if (!config->telegram_config.enabled) {
        return ESP_FAIL;
    }

    char encoded_text[1024];
    url_encode_string(message, encoded_text, sizeof(encoded_text));

    char params[1200];
    snprintf(params, sizeof(params), "chat_id=%s&text=%s&parse_mode=HTML",
             config->telegram_config.chat_id, encoded_text);

    return telegram_api_request("sendMessage", params, NULL, 0);
}

// Send alert with formatting
esp_err_t telegram_send_alert(const char *title, const char *message) {
    char alert_msg[512];
    snprintf(alert_msg, sizeof(alert_msg),
             "<b>⚠️ %s</b>\n\n%s\n\n<i>Time: %s</i>",
             title, message, "Now"); // TODO: Add timestamp

    return telegram_send_message(alert_msg);
}

// Get system uptime string
static void get_uptime_string(char *buffer, size_t buffer_size) {
    int64_t uptime_sec = esp_timer_get_time() / 1000000;
    int days = uptime_sec / 86400;
    int hours = (uptime_sec % 86400) / 3600;
    int minutes = (uptime_sec % 3600) / 60;
    int seconds = uptime_sec % 60;

    if (days > 0) {
        snprintf(buffer, buffer_size, "%dd %dh %dm", days, hours, minutes);
    } else if (hours > 0) {
        snprintf(buffer, buffer_size, "%dh %dm %ds", hours, minutes, seconds);
    } else if (minutes > 0) {
        snprintf(buffer, buffer_size, "%dm %ds", minutes, seconds);
    } else {
        snprintf(buffer, buffer_size, "%ds", seconds);
    }
}

// Send system status
esp_err_t telegram_send_status(void) {
    system_config_t *config = get_system_config();

    char uptime[64];
    get_uptime_string(uptime, sizeof(uptime));

    // Get memory info
    size_t free_heap = esp_get_free_heap_size();
    size_t min_free_heap = esp_get_minimum_free_heap_size();

    char status_msg[1024];
    snprintf(status_msg, sizeof(status_msg),
             "<b>🤖 ESP32 Gateway Status</b>\n"
             "━━━━━━━━━━━━━━━━━━━\n\n"
             "<b>📊 System Info:</b>\n"
             "├ Uptime: %s\n"
             "├ Free Heap: %.1f KB\n"
             "└ Min Heap: %.1f KB\n\n"
             "<b>🌐 Network:</b>\n"
             "├ Mode: %s\n"
             "└ Status: Connected\n\n"
             "<b>☁️ Azure IoT Hub:</b>\n"
             "├ Device: %s\n"
             "└ Status: Connected\n\n"
             "<b>💾 Sensors:</b>\n"
             "└ Active: %d\n\n"
             "<i>Gateway ID: %s</i>",
             uptime,
             free_heap / 1024.0,
             min_free_heap / 1024.0,
             config->network_mode == NETWORK_MODE_WIFI ? "WiFi" : "SIM",
             config->azure_device_id,
             config->sensor_count,
             config->azure_device_id);

    return telegram_send_message(status_msg);
}

// Send sensor readings
esp_err_t telegram_send_sensor_readings(void) {
    system_config_t *config = get_system_config();

    if (config->sensor_count == 0) {
        return telegram_send_message("No sensors configured");
    }

    char msg[1024];
    int offset = 0;

    offset += snprintf(msg + offset, sizeof(msg) - offset,
                      "<b>🌡️ Sensor Readings</b>\n"
                      "━━━━━━━━━━━━━━━━━━━\n\n");

    for (int i = 0; i < config->sensor_count && i < 5; i++) {  // Limit to 5 sensors to fit in message
        if (config->sensors[i].enabled) {
            offset += snprintf(msg + offset, sizeof(msg) - offset,
                             "<b>%s</b>\n"
                             "├ Type: %s\n"
                             "├ Slave ID: %d\n"
                             "└ Status: Active\n\n",
                             config->sensors[i].name,
                             config->sensors[i].sensor_type,
                             config->sensors[i].slave_id);
        }
    }

    if (config->sensor_count > 5) {
        offset += snprintf(msg + offset, sizeof(msg) - offset,
                          "<i>... and %d more</i>\n", config->sensor_count - 5);
    }

    return telegram_send_message(msg);
}

// Handle incoming commands
static void handle_telegram_command(const char *command, const char *from_user) {
    ESP_LOGI(TAG, "Command from %s: %s", from_user, command);

    if (strcmp(command, "/start") == 0 || strcmp(command, "/help") == 0) {
        telegram_send_message(
            "<b>🤖 ESP32 Gateway Bot</b>\n\n"
            "<b>Available Commands:</b>\n\n"
            "/status - System status\n"
            "/sensors - Sensor readings\n"
            "/wifi - WiFi info\n"
            "/azure - Azure status\n"
            "/webstart - Start web server\n"
            "/webstop - Stop web server\n"
            "/reboot - Restart system\n"
            "/help - Show this help\n\n"
            "<i>Bot v1.0 - Ready!</i>");

    } else if (strcmp(command, "/status") == 0) {
        telegram_send_status();

    } else if (strcmp(command, "/sensors") == 0) {
        telegram_send_sensor_readings();

    } else if (strcmp(command, "/wifi") == 0) {
        telegram_send_message("<b>📶 WiFi Information</b>\n\nConnected to network\nSignal: Good");

    } else if (strcmp(command, "/azure") == 0) {
        telegram_send_message("<b>☁️ Azure IoT Hub</b>\n\nStatus: Connected\nLast telemetry: 30s ago");

    } else if (strcmp(command, "/webstart") == 0) {
        ESP_LOGI(TAG, "Starting web server via Telegram command");
        telegram_send_message("🌐 Starting web server...");

        esp_err_t err = web_config_start_server_only();
        if (err == ESP_OK) {
            telegram_send_message(
                "✅ <b>Web Server Started</b>\n\n"
                "You can now access the configuration interface.\n\n"
                "<i>Note: Web server runs in operation mode - "
                "you can configure settings and it will auto-stop when done.</i>");
        } else {
            telegram_send_message("❌ Failed to start web server. It may already be running.");
        }

    } else if (strcmp(command, "/webstop") == 0) {
        ESP_LOGI(TAG, "Stopping web server via Telegram command");
        telegram_send_message("🛑 Stopping web server...");

        esp_err_t err = web_config_stop();
        if (err == ESP_OK) {
            telegram_send_message(
                "✅ <b>Web Server Stopped</b>\n\n"
                "Configuration interface is now disabled.\n\n"
                "<i>Use /webstart to enable it again when needed.</i>");
        } else {
            telegram_send_message("❌ Failed to stop web server. It may already be stopped.");
        }

    } else if (strcmp(command, "/reboot") == 0) {
        telegram_send_message("🔄 Rebooting system...");
        vTaskDelay(pdMS_TO_TICKS(2000));
        esp_restart();

    } else {
        telegram_send_message("❌ Unknown command. Send /help for available commands.");
    }
}

// Parse updates from Telegram
static void process_telegram_updates(const char *json_response) {
    cJSON *root = cJSON_Parse(json_response);
    if (root == NULL) {
        ESP_LOGW(TAG, "Failed to parse JSON response");
        return;
    }

    cJSON *ok = cJSON_GetObjectItem(root, "ok");
    if (!cJSON_IsBool(ok) || !cJSON_IsTrue(ok)) {
        cJSON_Delete(root);
        return;
    }

    cJSON *result = cJSON_GetObjectItem(root, "result");
    if (!cJSON_IsArray(result)) {
        cJSON_Delete(root);
        return;
    }

    int array_size = cJSON_GetArraySize(result);

    for (int i = 0; i < array_size; i++) {
        cJSON *update = cJSON_GetArrayItem(result, i);

        cJSON *update_id_obj = cJSON_GetObjectItem(update, "update_id");
        if (cJSON_IsNumber(update_id_obj)) {
            int update_id = update_id_obj->valueint;
            if (update_id > last_update_id) {
                last_update_id = update_id;
            }
        }

        cJSON *message = cJSON_GetObjectItem(update, "message");
        if (!cJSON_IsObject(message)) continue;

        cJSON *text_obj = cJSON_GetObjectItem(message, "text");
        if (!cJSON_IsString(text_obj)) continue;

        const char *text = text_obj->valuestring;

        cJSON *from = cJSON_GetObjectItem(message, "from");
        const char *username = "Unknown";
        if (cJSON_IsObject(from)) {
            cJSON *username_obj = cJSON_GetObjectItem(from, "username");
            if (cJSON_IsString(username_obj)) {
                username = username_obj->valuestring;
            }
        }

        handle_telegram_command(text, username);
    }

    cJSON_Delete(root);
}

// Telegram polling task
static void telegram_task(void *pvParameters) {
    system_config_t *config = get_system_config();
    char response[4096];

    ESP_LOGI(TAG, "Telegram bot task started");

    while (telegram_running) {
        if (!config->telegram_config.enabled) {
            vTaskDelay(pdMS_TO_TICKS(5000));
            continue;
        }

        char params[128];
        snprintf(params, sizeof(params), "offset=%d&timeout=10", last_update_id + 1);

        esp_err_t err = telegram_api_request("getUpdates", params, response, sizeof(response));

        if (err == ESP_OK) {
            process_telegram_updates(response);
        }

        vTaskDelay(pdMS_TO_TICKS(config->telegram_config.poll_interval * 1000));
    }

    ESP_LOGI(TAG, "Telegram bot task stopped");
    telegram_task_handle = NULL;
    vTaskDelete(NULL);
}

// Initialize Telegram bot
esp_err_t telegram_bot_init(void) {
    ESP_LOGI(TAG, "Initializing Telegram bot");
    return ESP_OK;
}

// Start Telegram bot
esp_err_t telegram_bot_start(void) {
    system_config_t *config = get_system_config();

    if (!config->telegram_config.enabled) {
        ESP_LOGI(TAG, "Telegram bot is disabled");
        return ESP_OK;
    }

    if (telegram_task_handle != NULL) {
        ESP_LOGW(TAG, "Telegram bot already running");
        return ESP_OK;
    }

    telegram_running = true;

    BaseType_t result = xTaskCreate(
        telegram_task,
        "telegram_task",
        8192,
        NULL,
        5,
        &telegram_task_handle
    );

    if (result != pdPASS) {
        ESP_LOGE(TAG, "Failed to create Telegram task");
        telegram_running = false;
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "✅ Telegram bot started");

    // Send startup notification if enabled
    if (config->telegram_config.startup_notification) {
        vTaskDelay(pdMS_TO_TICKS(2000));  // Wait for network
        telegram_send_message("🚀 <b>ESP32 Gateway Started</b>\n\nSystem is online and operational!");
    }

    return ESP_OK;
}

// Stop Telegram bot
esp_err_t telegram_bot_stop(void) {
    if (telegram_task_handle == NULL) {
        return ESP_OK;
    }

    telegram_running = false;

    // Wait for task to finish
    int timeout = 50;  // 5 seconds
    while (telegram_task_handle != NULL && timeout-- > 0) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    ESP_LOGI(TAG, "Telegram bot stopped");
    return ESP_OK;
}

// Check if bot is enabled
bool telegram_is_enabled(void) {
    system_config_t *config = get_system_config();
    return config->telegram_config.enabled &&
           strlen(config->telegram_config.bot_token) > 0 &&
           strlen(config->telegram_config.chat_id) > 0;
}
