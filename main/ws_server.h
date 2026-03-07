// ws_server.h - WebSocket server for real-time sensor data push
#ifndef WS_SERVER_H
#define WS_SERVER_H

#include "esp_err.h"
#include "esp_http_server.h"
#include "sensor_manager.h"

// Max concurrent WebSocket clients
#define WS_MAX_CLIENTS 2

// Initialize WebSocket support and register handler on existing HTTP server
esp_err_t ws_init(httpd_handle_t server);

// Broadcast sensor readings to all connected WebSocket clients
// Call this from telemetry_task after each sensor read cycle
void ws_broadcast_sensor_data(const sensor_reading_t *readings, int count);

// Broadcast system status (heap, uptime, MQTT state)
void ws_broadcast_system_status(void);

// Get number of connected WebSocket clients
int ws_get_client_count(void);

#endif // WS_SERVER_H
