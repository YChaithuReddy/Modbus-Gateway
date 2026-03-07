// provisioning.c - Device provisioning: config export/import, Azure quick setup, factory reset
#include "provisioning.h"
#include "web_config.h"
#include "auth.h"
#include "esp_log.h"
#include "esp_system.h"
#include "cJSON.h"
#include "nvs_flash.h"
#include "nvs.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "PROVISION";

// ============================================================================
// Azure Connection String Parser
// ============================================================================
esp_err_t provisioning_parse_azure_connection_string(const char *conn_str,
    char *hostname, size_t hostname_size,
    char *device_id, size_t device_id_size,
    char *device_key, size_t device_key_size) {

    if (!conn_str || !hostname || !device_id || !device_key) return ESP_ERR_INVALID_ARG;

    hostname[0] = device_id[0] = device_key[0] = '\0';

    // Parse "HostName=value;DeviceId=value;SharedAccessKey=value"
    const char *p;

    p = strstr(conn_str, "HostName=");
    if (p) {
        p += 9;
        int i = 0;
        while (*p && *p != ';' && i < (int)hostname_size - 1) hostname[i++] = *p++;
        hostname[i] = '\0';
    }

    p = strstr(conn_str, "DeviceId=");
    if (p) {
        p += 9;
        int i = 0;
        while (*p && *p != ';' && i < (int)device_id_size - 1) device_id[i++] = *p++;
        device_id[i] = '\0';
    }

    p = strstr(conn_str, "SharedAccessKey=");
    if (p) {
        p += 16;
        int i = 0;
        while (*p && *p != ';' && i < (int)device_key_size - 1) device_key[i++] = *p++;
        device_key[i] = '\0';
    }

    if (hostname[0] && device_id[0] && device_key[0]) {
        ESP_LOGI(TAG, "Parsed Azure connection string: Host=%s, Device=%s", hostname, device_id);
        return ESP_OK;
    }

    ESP_LOGE(TAG, "Failed to parse Azure connection string");
    return ESP_ERR_INVALID_ARG;
}

// ============================================================================
// Config Export Handler - GET /api/config/export
// ============================================================================
static esp_err_t config_export_handler(httpd_req_t *req) {
    CHECK_AUTH(req);

    system_config_t *cfg = get_system_config();
    cJSON *root = cJSON_CreateObject();

    // Network
    cJSON_AddNumberToObject(root, "network_mode", cfg->network_mode);
    cJSON_AddStringToObject(root, "wifi_ssid", cfg->wifi_ssid);
    cJSON_AddStringToObject(root, "wifi_password", cfg->wifi_password);

    // Azure
    cJSON_AddStringToObject(root, "azure_hub_fqdn", cfg->azure_hub_fqdn);
    cJSON_AddStringToObject(root, "azure_device_id", cfg->azure_device_id);
    cJSON_AddStringToObject(root, "azure_device_key", cfg->azure_device_key);
    cJSON_AddNumberToObject(root, "telemetry_interval", cfg->telemetry_interval);
    cJSON_AddBoolToObject(root, "batch_telemetry", cfg->batch_telemetry);

    // Modbus
    cJSON_AddNumberToObject(root, "modbus_retry_count", cfg->modbus_retry_count);
    cJSON_AddNumberToObject(root, "modbus_retry_delay", cfg->modbus_retry_delay);

    // Sensors
    cJSON_AddNumberToObject(root, "sensor_count", cfg->sensor_count);
    cJSON *sensors = cJSON_CreateArray();
    for (int i = 0; i < cfg->sensor_count && i < 10; i++) {
        cJSON *s = cJSON_CreateObject();
        cJSON_AddBoolToObject(s, "enabled", cfg->sensors[i].enabled);
        cJSON_AddStringToObject(s, "name", cfg->sensors[i].name);
        cJSON_AddStringToObject(s, "unit_id", cfg->sensors[i].unit_id);
        cJSON_AddNumberToObject(s, "slave_id", cfg->sensors[i].slave_id);
        cJSON_AddNumberToObject(s, "baud_rate", cfg->sensors[i].baud_rate);
        cJSON_AddStringToObject(s, "parity", cfg->sensors[i].parity);
        cJSON_AddNumberToObject(s, "register_address", cfg->sensors[i].register_address);
        cJSON_AddNumberToObject(s, "quantity", cfg->sensors[i].quantity);
        cJSON_AddStringToObject(s, "data_type", cfg->sensors[i].data_type);
        cJSON_AddStringToObject(s, "register_type", cfg->sensors[i].register_type);
        cJSON_AddNumberToObject(s, "scale_factor", cfg->sensors[i].scale_factor);
        cJSON_AddStringToObject(s, "byte_order", cfg->sensors[i].byte_order);
        cJSON_AddStringToObject(s, "description", cfg->sensors[i].description);
        cJSON_AddStringToObject(s, "sensor_type", cfg->sensors[i].sensor_type);
        cJSON_AddNumberToObject(s, "sensor_height", cfg->sensors[i].sensor_height);
        cJSON_AddNumberToObject(s, "max_water_level", cfg->sensors[i].max_water_level);
        cJSON_AddStringToObject(s, "meter_type", cfg->sensors[i].meter_type);
        cJSON_AddItemToArray(sensors, s);
    }
    cJSON_AddItemToObject(root, "sensors", sensors);

    // SIM config
    cJSON *sim = cJSON_CreateObject();
    cJSON_AddBoolToObject(sim, "enabled", cfg->sim_config.enabled);
    cJSON_AddStringToObject(sim, "apn", cfg->sim_config.apn);
    cJSON_AddItemToObject(root, "sim_config", sim);

    // SD card
    cJSON_AddBoolToObject(root, "sd_enabled", cfg->sd_config.enabled);
    cJSON_AddBoolToObject(root, "rtc_enabled", cfg->rtc_config.enabled);

    char *json_str = cJSON_Print(root);
    cJSON_Delete(root);

    if (!json_str) {
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, "{\"error\":\"Out of memory\"}");
        return ESP_OK;
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Content-Disposition", "attachment; filename=\"gateway_config.json\"");
    httpd_resp_sendstr(req, json_str);
    free(json_str);
    return ESP_OK;
}

// ============================================================================
// Config Import Handler - POST /api/config/import
// ============================================================================
static esp_err_t config_import_handler(httpd_req_t *req) {
    CHECK_AUTH(req);

    // Read POST body (max 4KB)
    char *buf = malloc(4096);
    if (!buf) {
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, "{\"success\":false,\"message\":\"Out of memory\"}");
        return ESP_OK;
    }

    int total = 0;
    while (total < 4095) {
        int ret = httpd_req_recv(req, buf + total, 4095 - total);
        if (ret <= 0) break;
        total += ret;
    }
    buf[total] = '\0';

    cJSON *root = cJSON_Parse(buf);
    free(buf);

    if (!root) {
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, "{\"success\":false,\"message\":\"Invalid JSON\"}");
        return ESP_OK;
    }

    system_config_t *cfg = get_system_config();

    // Import fields (only update if present in JSON)
    cJSON *item;

    item = cJSON_GetObjectItem(root, "wifi_ssid");
    if (item && cJSON_IsString(item)) strncpy(cfg->wifi_ssid, item->valuestring, sizeof(cfg->wifi_ssid) - 1);

    item = cJSON_GetObjectItem(root, "wifi_password");
    if (item && cJSON_IsString(item)) strncpy(cfg->wifi_password, item->valuestring, sizeof(cfg->wifi_password) - 1);

    item = cJSON_GetObjectItem(root, "azure_hub_fqdn");
    if (item && cJSON_IsString(item)) strncpy(cfg->azure_hub_fqdn, item->valuestring, sizeof(cfg->azure_hub_fqdn) - 1);

    item = cJSON_GetObjectItem(root, "azure_device_id");
    if (item && cJSON_IsString(item)) strncpy(cfg->azure_device_id, item->valuestring, sizeof(cfg->azure_device_id) - 1);

    item = cJSON_GetObjectItem(root, "azure_device_key");
    if (item && cJSON_IsString(item)) strncpy(cfg->azure_device_key, item->valuestring, sizeof(cfg->azure_device_key) - 1);

    item = cJSON_GetObjectItem(root, "telemetry_interval");
    if (item && cJSON_IsNumber(item)) cfg->telemetry_interval = item->valueint;

    item = cJSON_GetObjectItem(root, "batch_telemetry");
    if (item && cJSON_IsBool(item)) cfg->batch_telemetry = cJSON_IsTrue(item);

    item = cJSON_GetObjectItem(root, "modbus_retry_count");
    if (item && cJSON_IsNumber(item)) cfg->modbus_retry_count = item->valueint;

    item = cJSON_GetObjectItem(root, "modbus_retry_delay");
    if (item && cJSON_IsNumber(item)) cfg->modbus_retry_delay = item->valueint;

    // Import sensors
    cJSON *sensors = cJSON_GetObjectItem(root, "sensors");
    if (sensors && cJSON_IsArray(sensors)) {
        int count = cJSON_GetArraySize(sensors);
        if (count > 10) count = 10;
        cfg->sensor_count = count;

        for (int i = 0; i < count; i++) {
            cJSON *s = cJSON_GetArrayItem(sensors, i);
            if (!s) continue;

            item = cJSON_GetObjectItem(s, "enabled");
            cfg->sensors[i].enabled = item ? cJSON_IsTrue(item) : true;

            item = cJSON_GetObjectItem(s, "name");
            if (item && cJSON_IsString(item)) strncpy(cfg->sensors[i].name, item->valuestring, sizeof(cfg->sensors[i].name) - 1);

            item = cJSON_GetObjectItem(s, "unit_id");
            if (item && cJSON_IsString(item)) strncpy(cfg->sensors[i].unit_id, item->valuestring, sizeof(cfg->sensors[i].unit_id) - 1);

            item = cJSON_GetObjectItem(s, "slave_id");
            if (item && cJSON_IsNumber(item)) cfg->sensors[i].slave_id = item->valueint;

            item = cJSON_GetObjectItem(s, "baud_rate");
            if (item && cJSON_IsNumber(item)) cfg->sensors[i].baud_rate = item->valueint;

            item = cJSON_GetObjectItem(s, "parity");
            if (item && cJSON_IsString(item)) strncpy(cfg->sensors[i].parity, item->valuestring, sizeof(cfg->sensors[i].parity) - 1);

            item = cJSON_GetObjectItem(s, "register_address");
            if (item && cJSON_IsNumber(item)) cfg->sensors[i].register_address = item->valueint;

            item = cJSON_GetObjectItem(s, "quantity");
            if (item && cJSON_IsNumber(item)) cfg->sensors[i].quantity = item->valueint;

            item = cJSON_GetObjectItem(s, "data_type");
            if (item && cJSON_IsString(item)) strncpy(cfg->sensors[i].data_type, item->valuestring, sizeof(cfg->sensors[i].data_type) - 1);

            item = cJSON_GetObjectItem(s, "register_type");
            if (item && cJSON_IsString(item)) strncpy(cfg->sensors[i].register_type, item->valuestring, sizeof(cfg->sensors[i].register_type) - 1);

            item = cJSON_GetObjectItem(s, "scale_factor");
            if (item && cJSON_IsNumber(item)) cfg->sensors[i].scale_factor = (float)item->valuedouble;

            item = cJSON_GetObjectItem(s, "byte_order");
            if (item && cJSON_IsString(item)) strncpy(cfg->sensors[i].byte_order, item->valuestring, sizeof(cfg->sensors[i].byte_order) - 1);

            item = cJSON_GetObjectItem(s, "sensor_type");
            if (item && cJSON_IsString(item)) strncpy(cfg->sensors[i].sensor_type, item->valuestring, sizeof(cfg->sensors[i].sensor_type) - 1);

            item = cJSON_GetObjectItem(s, "sensor_height");
            if (item && cJSON_IsNumber(item)) cfg->sensors[i].sensor_height = (float)item->valuedouble;

            item = cJSON_GetObjectItem(s, "max_water_level");
            if (item && cJSON_IsNumber(item)) cfg->sensors[i].max_water_level = (float)item->valuedouble;

            item = cJSON_GetObjectItem(s, "meter_type");
            if (item && cJSON_IsString(item)) strncpy(cfg->sensors[i].meter_type, item->valuestring, sizeof(cfg->sensors[i].meter_type) - 1);
        }
    }

    cJSON_Delete(root);

    // Save to NVS
    esp_err_t err = config_save_to_nvs(cfg);
    httpd_resp_set_type(req, "application/json");
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Config imported successfully (%d sensors)", cfg->sensor_count);
        httpd_resp_sendstr(req, "{\"success\":true,\"message\":\"Configuration imported. Reboot to apply.\"}");
    } else {
        httpd_resp_sendstr(req, "{\"success\":false,\"message\":\"Failed to save config to NVS\"}");
    }
    return ESP_OK;
}

// ============================================================================
// Azure Quick Setup - POST /api/azure/quick_setup
// ============================================================================
static esp_err_t azure_quick_setup_handler(httpd_req_t *req) {
    CHECK_AUTH(req);

    char buf[512] = {0};
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret <= 0) {
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, "{\"success\":false,\"message\":\"No data\"}");
        return ESP_OK;
    }

    // Parse connection string from form data or raw body
    char conn_str[384] = {0};
    char *p = strstr(buf, "connection_string=");
    if (p) {
        p += 18;
        int i = 0;
        while (*p && *p != '&' && i < 383) {
            if (*p == '%' && p[1] && p[2]) {
                char hex[3] = {p[1], p[2], 0};
                conn_str[i++] = (char)strtol(hex, NULL, 16);
                p += 3;
            } else if (*p == '+') { conn_str[i++] = ' '; p++; }
            else { conn_str[i++] = *p++; }
        }
    } else {
        strncpy(conn_str, buf, sizeof(conn_str) - 1);
    }

    system_config_t *cfg = get_system_config();
    char hostname[128], device_id[32], device_key[128];

    esp_err_t err = provisioning_parse_azure_connection_string(conn_str,
        hostname, sizeof(hostname), device_id, sizeof(device_id), device_key, sizeof(device_key));

    httpd_resp_set_type(req, "application/json");

    if (err == ESP_OK) {
        strncpy(cfg->azure_hub_fqdn, hostname, sizeof(cfg->azure_hub_fqdn) - 1);
        strncpy(cfg->azure_device_id, device_id, sizeof(cfg->azure_device_id) - 1);
        strncpy(cfg->azure_device_key, device_key, sizeof(cfg->azure_device_key) - 1);
        cfg->config_complete = true;

        config_save_to_nvs(cfg);
        ESP_LOGI(TAG, "Azure quick setup: Hub=%s, Device=%s", hostname, device_id);

        char resp[256];
        snprintf(resp, sizeof(resp),
            "{\"success\":true,\"message\":\"Azure configured: %s / %s. Reboot to connect.\"}",
            hostname, device_id);
        httpd_resp_sendstr(req, resp);
    } else {
        httpd_resp_sendstr(req, "{\"success\":false,\"message\":\"Invalid connection string format. Expected: HostName=...;DeviceId=...;SharedAccessKey=...\"}");
    }
    return ESP_OK;
}

// ============================================================================
// Factory Reset Handler - POST /api/factory_reset
// ============================================================================
static esp_err_t factory_reset_handler(httpd_req_t *req) {
    CHECK_AUTH(req);

    char buf[64] = {0};
    httpd_req_recv(req, buf, sizeof(buf) - 1);

    // Require confirmation
    if (!strstr(buf, "confirm=yes")) {
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, "{\"success\":false,\"message\":\"Confirmation required: send confirm=yes\"}");
        return ESP_OK;
    }

    ESP_LOGW(TAG, "FACTORY RESET initiated!");

    // Reset config
    config_reset_to_defaults();

    // Erase NVS namespaces
    nvs_handle_t handle;
    if (nvs_open("config", NVS_READWRITE, &handle) == ESP_OK) {
        nvs_erase_all(handle);
        nvs_commit(handle);
        nvs_close(handle);
    }
    if (nvs_open("auth", NVS_READWRITE, &handle) == ESP_OK) {
        nvs_erase_all(handle);
        nvs_commit(handle);
        nvs_close(handle);
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"success\":true,\"message\":\"Factory reset complete. Rebooting in 3 seconds...\"}");

    // Schedule reboot
    vTaskDelay(pdMS_TO_TICKS(3000));
    esp_restart();

    return ESP_OK; // Never reached
}

// ============================================================================
// Register all provisioning handlers
// ============================================================================
esp_err_t provisioning_register_handlers(httpd_handle_t server) {
    if (!server) return ESP_ERR_INVALID_ARG;

    httpd_uri_t export_uri = {
        .uri = "/api/config/export", .method = HTTP_GET,
        .handler = config_export_handler, .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &export_uri);

    httpd_uri_t import_uri = {
        .uri = "/api/config/import", .method = HTTP_POST,
        .handler = config_import_handler, .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &import_uri);

    httpd_uri_t azure_setup_uri = {
        .uri = "/api/azure/quick_setup", .method = HTTP_POST,
        .handler = azure_quick_setup_handler, .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &azure_setup_uri);

    httpd_uri_t reset_uri = {
        .uri = "/api/factory_reset", .method = HTTP_POST,
        .handler = factory_reset_handler, .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &reset_uri);

    ESP_LOGI(TAG, "Provisioning handlers registered (export/import/azure_setup/factory_reset)");
    return ESP_OK;
}
