// ws_server.c - WebSocket server for real-time sensor data push
// Pushes sensor readings and system status to connected web clients

#include "ws_server.h"
#include "auth.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "cJSON.h"
#include <string.h>

static const char *TAG = "WS_SERVER";

// External declarations for system status
extern volatile bool mqtt_connected;
extern uint32_t total_telemetry_sent;

// Track connected WebSocket client file descriptors
static int ws_client_fds[WS_MAX_CLIENTS] = {-1, -1};
static httpd_handle_t ws_server_handle = NULL;

// WebSocket handler - manages connect/disconnect
static esp_err_t ws_handler(httpd_req_t *req) {
    if (req->method == HTTP_GET) {
        // New WebSocket connection - check auth via cookie
        if (!auth_check_request(req)) {
            ESP_LOGW(TAG, "Unauthenticated WebSocket connection rejected");
            httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, "Auth required");
            return ESP_FAIL;
        }

        int fd = httpd_req_to_sockfd(req);
        ESP_LOGI(TAG, "WebSocket connected: fd=%d", fd);

        // Store the fd
        for (int i = 0; i < WS_MAX_CLIENTS; i++) {
            if (ws_client_fds[i] == -1) {
                ws_client_fds[i] = fd;
                ESP_LOGI(TAG, "WebSocket client stored in slot %d", i);
                return ESP_OK;
            }
        }

        // No free slots - reject
        ESP_LOGW(TAG, "Max WebSocket clients reached, rejecting fd=%d", fd);
        return ESP_FAIL;
    }

    // Handle incoming WebSocket frames
    httpd_ws_frame_t ws_pkt;
    memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
    ws_pkt.type = HTTPD_WS_TYPE_TEXT;

    // Receive the frame
    esp_err_t ret = httpd_ws_recv_frame(req, &ws_pkt, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WS recv frame len failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // For now, we only broadcast - ignore client messages
    // Could add subscribe/unsubscribe commands here later

    if (ws_pkt.type == HTTPD_WS_TYPE_CLOSE) {
        int fd = httpd_req_to_sockfd(req);
        ESP_LOGI(TAG, "WebSocket closed: fd=%d", fd);
        for (int i = 0; i < WS_MAX_CLIENTS; i++) {
            if (ws_client_fds[i] == fd) {
                ws_client_fds[i] = -1;
                break;
            }
        }
    }

    return ESP_OK;
}

esp_err_t ws_init(httpd_handle_t server) {
    if (!server) return ESP_ERR_INVALID_ARG;

    ws_server_handle = server;

    httpd_uri_t ws_uri = {
        .uri = "/ws",
        .method = HTTP_GET,
        .handler = ws_handler,
        .user_ctx = NULL,
        .is_websocket = true,
        .handle_ws_control_frames = true
    };

    esp_err_t ret = httpd_register_uri_handler(server, &ws_uri);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "WebSocket handler registered at /ws");
    } else {
        ESP_LOGE(TAG, "Failed to register WebSocket handler: %s", esp_err_to_name(ret));
    }

    // Initialize client slots
    for (int i = 0; i < WS_MAX_CLIENTS; i++) {
        ws_client_fds[i] = -1;
    }

    return ret;
}

// Send data to a single client, remove if disconnected
static void ws_send_to_client(int slot, const char *data, size_t len) {
    if (ws_client_fds[slot] == -1 || !ws_server_handle) return;

    httpd_ws_frame_t ws_pkt = {
        .payload = (uint8_t *)data,
        .len = len,
        .type = HTTPD_WS_TYPE_TEXT
    };

    esp_err_t ret = httpd_ws_send_frame_async(ws_server_handle, ws_client_fds[slot], &ws_pkt);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "WS send failed for fd=%d: %s - removing client", ws_client_fds[slot], esp_err_to_name(ret));
        ws_client_fds[slot] = -1;
    }
}

void ws_broadcast_sensor_data(const sensor_reading_t *readings, int count) {
    if (!readings || count <= 0 || !ws_server_handle) return;

    // Skip if no clients connected
    bool has_clients = false;
    for (int i = 0; i < WS_MAX_CLIENTS; i++) {
        if (ws_client_fds[i] != -1) { has_clients = true; break; }
    }
    if (!has_clients) return;

    // Build JSON payload
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "type", "sensors");
    cJSON_AddNumberToObject(root, "timestamp", (double)(esp_timer_get_time() / 1000000));

    cJSON *sensors = cJSON_CreateArray();
    for (int i = 0; i < count; i++) {
        if (!readings[i].valid) continue;

        cJSON *sensor = cJSON_CreateObject();
        cJSON_AddStringToObject(sensor, "unit_id", readings[i].unit_id);
        cJSON_AddStringToObject(sensor, "name", readings[i].sensor_name);
        cJSON_AddNumberToObject(sensor, "value", readings[i].value);
        cJSON_AddStringToObject(sensor, "source", readings[i].data_source);
        cJSON_AddItemToArray(sensors, sensor);
    }
    cJSON_AddItemToObject(root, "sensors", sensors);

    char *json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    if (json_str) {
        size_t len = strlen(json_str);
        for (int i = 0; i < WS_MAX_CLIENTS; i++) {
            ws_send_to_client(i, json_str, len);
        }
        free(json_str);
    }
}

void ws_broadcast_system_status(void) {
    if (!ws_server_handle) return;

    bool has_clients = false;
    for (int i = 0; i < WS_MAX_CLIENTS; i++) {
        if (ws_client_fds[i] != -1) { has_clients = true; break; }
    }
    if (!has_clients) return;

    char buf[256];
    snprintf(buf, sizeof(buf),
        "{\"type\":\"status\",\"heap\":%lu,\"mqtt\":%s,\"telemetry_sent\":%lu,\"uptime\":%lld}",
        (unsigned long)esp_get_free_heap_size(),
        mqtt_connected ? "true" : "false",
        (unsigned long)total_telemetry_sent,
        (long long)(esp_timer_get_time() / 1000000));

    size_t len = strlen(buf);
    for (int i = 0; i < WS_MAX_CLIENTS; i++) {
        ws_send_to_client(i, buf, len);
    }
}

int ws_get_client_count(void) {
    int count = 0;
    for (int i = 0; i < WS_MAX_CLIENTS; i++) {
        if (ws_client_fds[i] != -1) count++;
    }
    return count;
}
