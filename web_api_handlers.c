// web_api_handlers.c - HTTP API endpoint handlers for Network Mode configuration
// Add these functions to web_config.c

#include "web_config.h"
#include "network_manager.h"
#include "sd_card_logger.h"
#include "ds3231_rtc.h"
#include "a7670c_ppp.h"
#include "esp_log.h"
#include "cJSON.h"
#include <string.h>

static const char *TAG = "WEB_API";

// ============================================================================
// 1. Network Mode API
// ============================================================================

// GET /api/network/status - Get current network status
static esp_err_t api_network_status_handler(httpd_req_t *req)
{
    cJSON *root = cJSON_CreateObject();

    // Get network stats
    network_stats_t stats = {0};
    bool connected = network_manager_is_connected();

    if (connected) {
        network_manager_get_stats(&stats);
    }

    system_config_t *config = get_system_config();

    cJSON_AddStringToObject(root, "mode",
        config->network_mode == NETWORK_MODE_WIFI ? "wifi" : "sim");
    cJSON_AddBoolToObject(root, "connected", connected);
    cJSON_AddStringToObject(root, "network_type", stats.network_type);
    cJSON_AddNumberToObject(root, "signal_strength", stats.signal_strength);

    // Determine quality
    const char *quality = "Unknown";
    if (connected) {
        if (stats.signal_strength >= -60) quality = "Excellent";
        else if (stats.signal_strength >= -70) quality = "Good";
        else if (stats.signal_strength >= -80) quality = "Fair";
        else quality = "Poor";
    }
    cJSON_AddStringToObject(root, "quality", quality);

    char *json_str = cJSON_Print(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, json_str);

    free(json_str);
    cJSON_Delete(root);
    return ESP_OK;
}

// POST /api/network/mode - Set network mode (WiFi or SIM)
static esp_err_t api_network_mode_handler(httpd_req_t *req)
{
    char content[100];
    int ret = httpd_req_recv(req, content, sizeof(content) - 1);
    if (ret <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid request");
        return ESP_FAIL;
    }
    content[ret] = '\0';

    cJSON *root = cJSON_Parse(content);
    if (!root) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }

    cJSON *mode_json = cJSON_GetObjectItem(root, "mode");
    if (!mode_json) {
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing 'mode' field");
        return ESP_FAIL;
    }

    const char *mode = mode_json->valuestring;
    system_config_t *config = get_system_config();

    if (strcmp(mode, "wifi") == 0) {
        config->network_mode = NETWORK_MODE_WIFI;
    } else if (strcmp(mode, "sim") == 0) {
        config->network_mode = NETWORK_MODE_SIM;
    } else {
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid mode");
        return ESP_FAIL;
    }

    // Save to NVS
    config_save_to_nvs(config);

    cJSON_Delete(root);

    // Send success response
    cJSON *response = cJSON_CreateObject();
    cJSON_AddBoolToObject(response, "success", true);
    cJSON_AddStringToObject(response, "message", "Network mode updated. Reboot required.");

    char *json_str = cJSON_Print(response);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, json_str);

    free(json_str);
    cJSON_Delete(response);
    return ESP_OK;
}

// ============================================================================
// 2. WiFi API
// ============================================================================

// POST /api/network/wifi - Save WiFi configuration
static esp_err_t api_wifi_config_handler(httpd_req_t *req)
{
    char content[200];
    int ret = httpd_req_recv(req, content, sizeof(content) - 1);
    if (ret <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid request");
        return ESP_FAIL;
    }
    content[ret] = '\0';

    // Parse form data
    char ssid[32] = {0};
    char password[64] = {0};

    httpd_query_key_value(content, "wifi_ssid", ssid, sizeof(ssid));
    httpd_query_key_value(content, "wifi_password", password, sizeof(password));

    if (strlen(ssid) == 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "SSID required");
        return ESP_FAIL;
    }

    system_config_t *config = get_system_config();
    strncpy(config->wifi_ssid, ssid, sizeof(config->wifi_ssid) - 1);
    strncpy(config->wifi_password, password, sizeof(config->wifi_password) - 1);

    config_save_to_nvs(config);

    cJSON *response = cJSON_CreateObject();
    cJSON_AddBoolToObject(response, "success", true);
    cJSON_AddStringToObject(response, "message", "WiFi configuration saved");

    char *json_str = cJSON_Print(response);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, json_str);

    free(json_str);
    cJSON_Delete(response);
    return ESP_OK;
}

// POST /api/network/wifi/test - Test WiFi connection
static esp_err_t api_wifi_test_handler(httpd_req_t *req)
{
    // Note: This is a placeholder. Actual implementation would need to
    // attempt connection without disrupting current network

    cJSON *response = cJSON_CreateObject();
    cJSON_AddBoolToObject(response, "success", true);
    cJSON_AddStringToObject(response, "message", "WiFi test not yet implemented");

    char *json_str = cJSON_Print(response);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, json_str);

    free(json_str);
    cJSON_Delete(response);
    return ESP_OK;
}

// ============================================================================
// 3. SIM Module API
// ============================================================================

// POST /api/network/sim - Save SIM configuration
static esp_err_t api_sim_config_handler(httpd_req_t *req)
{
    char content[300];
    int ret = httpd_req_recv(req, content, sizeof(content) - 1);
    if (ret <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid request");
        return ESP_FAIL;
    }
    content[ret] = '\0';

    system_config_t *config = get_system_config();

    // Parse form data
    char apn[64] = {0};
    char apn_user[32] = {0};
    char apn_pass[32] = {0};

    httpd_query_key_value(content, "sim_apn", apn, sizeof(apn));
    httpd_query_key_value(content, "sim_user", apn_user, sizeof(apn_user));
    httpd_query_key_value(content, "sim_pass", apn_pass, sizeof(apn_pass));

    if (strlen(apn) == 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "APN required");
        return ESP_FAIL;
    }

    // Update SIM config
    config->sim_config.enabled = true;
    strncpy(config->sim_config.apn, apn, sizeof(config->sim_config.apn) - 1);
    strncpy(config->sim_config.apn_user, apn_user, sizeof(config->sim_config.apn_user) - 1);
    strncpy(config->sim_config.apn_pass, apn_pass, sizeof(config->sim_config.apn_pass) - 1);

    config_save_to_nvs(config);

    cJSON *response = cJSON_CreateObject();
    cJSON_AddBoolToObject(response, "success", true);
    cJSON_AddStringToObject(response, "message", "SIM configuration saved");

    char *json_str = cJSON_Print(response);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, json_str);

    free(json_str);
    cJSON_Delete(response);
    return ESP_OK;
}

// POST /api/network/sim/test - Test SIM connection and get signal strength
static esp_err_t api_sim_test_handler(httpd_req_t *req)
{
    cJSON *response = cJSON_CreateObject();

    // Get signal strength using AT+CSQ
    signal_strength_t signal;
    esp_err_t ret = a7670c_get_signal_strength(&signal);

    if (ret == ESP_OK) {
        cJSON_AddBoolToObject(response, "success", true);
        cJSON_AddStringToObject(response, "message", "SIM signal detected");
        cJSON_AddNumberToObject(response, "csq", signal.csq);
        cJSON_AddNumberToObject(response, "rssi_dbm", signal.rssi_dbm);

        // Get operator name
        char operator_name[32] = {0};
        ret = a7670c_get_operator(operator_name, sizeof(operator_name));
        if (ret == ESP_OK) {
            cJSON_AddStringToObject(response, "operator", operator_name);
        }
    } else {
        cJSON_AddBoolToObject(response, "success", false);
        cJSON_AddStringToObject(response, "message", "Failed to get SIM signal");
    }

    char *json_str = cJSON_Print(response);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, json_str);

    free(json_str);
    cJSON_Delete(response);
    return ESP_OK;
}

// ============================================================================
// 4. SD Card API
// ============================================================================

// POST /api/sd/config - Save SD card configuration
static esp_err_t api_sd_config_handler(httpd_req_t *req)
{
    char content[200];
    int ret = httpd_req_recv(req, content, sizeof(content) - 1);
    if (ret <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid request");
        return ESP_FAIL;
    }
    content[ret] = '\0';

    system_config_t *config = get_system_config();

    // Check if SD enabled checkbox was checked
    char enabled_str[10] = {0};
    httpd_query_key_value(content, "sd_enabled", enabled_str, sizeof(enabled_str));
    config->sd_config.enabled = (strcmp(enabled_str, "on") == 0 || strcmp(enabled_str, "1") == 0);

    config_save_to_nvs(config);

    cJSON *response = cJSON_CreateObject();
    cJSON_AddBoolToObject(response, "success", true);
    cJSON_AddStringToObject(response, "message",
        config->sd_config.enabled ? "SD card enabled" : "SD card disabled");

    char *json_str = cJSON_Print(response);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, json_str);

    free(json_str);
    cJSON_Delete(response);
    return ESP_OK;
}

// GET /api/sd/status - Get SD card status
static esp_err_t api_sd_status_handler(httpd_req_t *req)
{
    cJSON *response = cJSON_CreateObject();

    system_config_t *config = get_system_config();

    if (config->sd_config.enabled) {
        // Get SD card stats
        uint32_t total_kb = 0, free_kb = 0;
        esp_err_t ret = sd_card_get_space(&total_kb, &free_kb);

        cJSON_AddBoolToObject(response, "mounted", ret == ESP_OK);
        cJSON_AddNumberToObject(response, "free_space_mb", free_kb / 1024);
        cJSON_AddNumberToObject(response, "total_space_mb", total_kb / 1024);

        // Get cached message count
        int cached_count = sd_card_get_cached_count();
        cJSON_AddNumberToObject(response, "cached_messages", cached_count);
    } else {
        cJSON_AddBoolToObject(response, "mounted", false);
        cJSON_AddStringToObject(response, "status", "SD card disabled");
    }

    char *json_str = cJSON_Print(response);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, json_str);

    free(json_str);
    cJSON_Delete(response);
    return ESP_OK;
}

// POST /api/sd/clear - Clear cached messages
static esp_err_t api_sd_clear_handler(httpd_req_t *req)
{
    cJSON *response = cJSON_CreateObject();

    esp_err_t ret = sd_card_clear_cache();

    if (ret == ESP_OK) {
        cJSON_AddBoolToObject(response, "success", true);
        cJSON_AddStringToObject(response, "message", "SD card cache cleared");
    } else {
        cJSON_AddBoolToObject(response, "success", false);
        cJSON_AddStringToObject(response, "message", "Failed to clear cache");
    }

    char *json_str = cJSON_Print(response);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, json_str);

    free(json_str);
    cJSON_Delete(response);
    return ESP_OK;
}

// ============================================================================
// 5. RTC API
// ============================================================================

// POST /api/rtc/config - Save RTC configuration
static esp_err_t api_rtc_config_handler(httpd_req_t *req)
{
    char content[200];
    int ret = httpd_req_recv(req, content, sizeof(content) - 1);
    if (ret <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid request");
        return ESP_FAIL;
    }
    content[ret] = '\0';

    system_config_t *config = get_system_config();

    // Check if RTC enabled checkbox was checked
    char enabled_str[10] = {0};
    httpd_query_key_value(content, "rtc_enabled", enabled_str, sizeof(enabled_str));
    config->rtc_config.enabled = (strcmp(enabled_str, "on") == 0 || strcmp(enabled_str, "1") == 0);

    config_save_to_nvs(config);

    cJSON *response = cJSON_CreateObject();
    cJSON_AddBoolToObject(response, "success", true);
    cJSON_AddStringToObject(response, "message",
        config->rtc_config.enabled ? "RTC enabled" : "RTC disabled");

    char *json_str = cJSON_Print(response);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, json_str);

    free(json_str);
    cJSON_Delete(response);
    return ESP_OK;
}

// POST /api/rtc/sync - Sync RTC with NTP
static esp_err_t api_rtc_sync_handler(httpd_req_t *req)
{
    cJSON *response = cJSON_CreateObject();

    esp_err_t ret = ds3231_update_from_system_time();

    if (ret == ESP_OK) {
        cJSON_AddBoolToObject(response, "success", true);
        cJSON_AddStringToObject(response, "message", "RTC synced with NTP");

        // Get current time
        struct tm timeinfo;
        ds3231_get_time(&timeinfo);
        char time_str[32];
        strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", &timeinfo);
        cJSON_AddStringToObject(response, "time", time_str);
    } else {
        cJSON_AddBoolToObject(response, "success", false);
        cJSON_AddStringToObject(response, "message", "Failed to sync RTC");
    }

    char *json_str = cJSON_Print(response);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, json_str);

    free(json_str);
    cJSON_Delete(response);
    return ESP_OK;
}

// ============================================================================
// 6. System Control API
// ============================================================================

// POST /api/system/reboot_operation - Reboot to operation mode
static esp_err_t api_reboot_operation_handler(httpd_req_t *req)
{
    cJSON *response = cJSON_CreateObject();
    cJSON_AddBoolToObject(response, "success", true);
    cJSON_AddStringToObject(response, "message", "Rebooting to operation mode...");

    char *json_str = cJSON_Print(response);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, json_str);

    free(json_str);
    cJSON_Delete(response);

    // Wait a bit for response to be sent
    vTaskDelay(1000 / portTICK_PERIOD_MS);

    // Restart ESP32
    esp_restart();

    return ESP_OK;
}

// POST /api/system/reboot - Reboot device
static esp_err_t api_reboot_handler(httpd_req_t *req)
{
    cJSON *response = cJSON_CreateObject();
    cJSON_AddBoolToObject(response, "success", true);
    cJSON_AddStringToObject(response, "message", "Rebooting device...");

    char *json_str = cJSON_Print(response);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, json_str);

    free(json_str);
    cJSON_Delete(response);

    // Wait a bit for response to be sent
    vTaskDelay(1000 / portTICK_PERIOD_MS);

    // Restart ESP32
    esp_restart();

    return ESP_OK;
}

// ============================================================================
// URI Handler Registration
// Add these to your httpd_uri_t array in web_config.c
// ============================================================================

/*
httpd_uri_t uri_api_network_status = {
    .uri       = "/api/network/status",
    .method    = HTTP_GET,
    .handler   = api_network_status_handler,
    .user_ctx  = NULL
};

httpd_uri_t uri_api_network_mode = {
    .uri       = "/api/network/mode",
    .method    = HTTP_POST,
    .handler   = api_network_mode_handler,
    .user_ctx  = NULL
};

httpd_uri_t uri_api_wifi_config = {
    .uri       = "/api/network/wifi",
    .method    = HTTP_POST,
    .handler   = api_wifi_config_handler,
    .user_ctx  = NULL
};

httpd_uri_t uri_api_wifi_test = {
    .uri       = "/api/network/wifi/test",
    .method    = HTTP_POST,
    .handler   = api_wifi_test_handler,
    .user_ctx  = NULL
};

httpd_uri_t uri_api_sim_config = {
    .uri       = "/api/network/sim",
    .method    = HTTP_POST,
    .handler   = api_sim_config_handler,
    .user_ctx  = NULL
};

httpd_uri_t uri_api_sim_test = {
    .uri       = "/api/network/sim/test",
    .method    = HTTP_POST,
    .handler   = api_sim_test_handler,
    .user_ctx  = NULL
};

httpd_uri_t uri_api_sd_config = {
    .uri       = "/api/sd/config",
    .method    = HTTP_POST,
    .handler   = api_sd_config_handler,
    .user_ctx  = NULL
};

httpd_uri_t uri_api_sd_status = {
    .uri       = "/api/sd/status",
    .method    = HTTP_GET,
    .handler   = api_sd_status_handler,
    .user_ctx  = NULL
};

httpd_uri_t uri_api_sd_clear = {
    .uri       = "/api/sd/clear",
    .method    = HTTP_POST,
    .handler   = api_sd_clear_handler,
    .user_ctx  = NULL
};

httpd_uri_t uri_api_rtc_config = {
    .uri       = "/api/rtc/config",
    .method    = HTTP_POST,
    .handler   = api_rtc_config_handler,
    .user_ctx  = NULL
};

httpd_uri_t uri_api_rtc_sync = {
    .uri       = "/api/rtc/sync",
    .method    = HTTP_POST,
    .handler   = api_rtc_sync_handler,
    .user_ctx  = NULL
};

httpd_uri_t uri_api_reboot_operation = {
    .uri       = "/api/system/reboot_operation",
    .method    = HTTP_POST,
    .handler   = api_reboot_operation_handler,
    .user_ctx  = NULL
};

httpd_uri_t uri_api_reboot = {
    .uri       = "/api/system/reboot",
    .method    = HTTP_POST,
    .handler   = api_reboot_handler,
    .user_ctx  = NULL
};

// Register all handlers:
httpd_register_uri_handler(server, &uri_api_network_status);
httpd_register_uri_handler(server, &uri_api_network_mode);
httpd_register_uri_handler(server, &uri_api_wifi_config);
httpd_register_uri_handler(server, &uri_api_wifi_test);
httpd_register_uri_handler(server, &uri_api_sim_config);
httpd_register_uri_handler(server, &uri_api_sim_test);
httpd_register_uri_handler(server, &uri_api_sd_config);
httpd_register_uri_handler(server, &uri_api_sd_status);
httpd_register_uri_handler(server, &uri_api_sd_clear);
httpd_register_uri_handler(server, &uri_api_rtc_config);
httpd_register_uri_handler(server, &uri_api_rtc_sync);
httpd_register_uri_handler(server, &uri_api_reboot_operation);
httpd_register_uri_handler(server, &uri_api_reboot);
*/
