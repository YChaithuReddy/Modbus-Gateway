// provisioning.h - Device provisioning features
#ifndef PROVISIONING_H
#define PROVISIONING_H

#include "esp_err.h"
#include "esp_http_server.h"

// Register all provisioning HTTP handlers on existing server
esp_err_t provisioning_register_handlers(httpd_handle_t server);

// Parse Azure IoT Hub connection string into config fields
// Format: HostName=hub.azure-devices.net;DeviceId=device1;SharedAccessKey=key==
esp_err_t provisioning_parse_azure_connection_string(const char *conn_str,
    char *hostname, size_t hostname_size,
    char *device_id, size_t device_id_size,
    char *device_key, size_t device_key_size);

#endif // PROVISIONING_H
