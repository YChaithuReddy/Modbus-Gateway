/**
 * @file a7670c_http.c
 * @brief A7670C Modem HTTPS implementation for OTA updates
 *
 * Uses AT+SH* commands (SHHTTP) for secure HTTPS connections.
 * The regular AT+HTTP* commands only support plain HTTP on A7670C.
 */

#include "a7670c_http.h"
#include "a7670c_ppp.h"
#include "web_config.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_ota_ops.h"

static const char *TAG = "A7670C_HTTP";

// External references to modem UART (from a7670c_ppp.c)
extern int a7670c_get_uart_num(void);

// HTTP state
static bool http_initialized = false;
static bool http_connected = false;
static int modem_uart_num = -1;

// UART buffers for AT commands
static uint8_t at_rx_buffer[4096];
static char at_response[4096];

// Current content info
static size_t current_content_length = 0;

// Parse URL into host, port, path
typedef struct {
    char host[256];
    int port;
    char path[1024];
    bool is_https;
} parsed_url_t;

static bool parse_url(const char* url, parsed_url_t* parsed) {
    memset(parsed, 0, sizeof(parsed_url_t));
    parsed->port = 80;  // Default HTTP port

    const char* p = url;

    // Check protocol
    if (strncmp(p, "https://", 8) == 0) {
        parsed->is_https = true;
        parsed->port = 443;
        p += 8;
    } else if (strncmp(p, "http://", 7) == 0) {
        parsed->is_https = false;
        p += 7;
    } else {
        return false;  // Invalid URL
    }

    // Find host end (/ or :)
    const char* host_end = p;
    while (*host_end && *host_end != '/' && *host_end != ':') {
        host_end++;
    }

    size_t host_len = host_end - p;
    if (host_len >= sizeof(parsed->host)) {
        host_len = sizeof(parsed->host) - 1;
    }
    strncpy(parsed->host, p, host_len);
    parsed->host[host_len] = '\0';

    p = host_end;

    // Check for port
    if (*p == ':') {
        p++;
        parsed->port = atoi(p);
        while (*p && *p != '/') p++;
    }

    // Get path (including query string)
    if (*p == '/') {
        strncpy(parsed->path, p, sizeof(parsed->path) - 1);
    } else {
        strcpy(parsed->path, "/");
    }

    return true;
}

// Send AT command and get full response
static esp_err_t send_at_cmd(const char* cmd, const char* expected, char* response, size_t response_size, int timeout_ms)
{
    if (modem_uart_num < 0) {
        ESP_LOGE(TAG, "UART not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    memset(response, 0, response_size);

    ESP_LOGI(TAG, ">>> %s", cmd);

    // Send command with CRLF
    char cmd_with_crlf[512];
    snprintf(cmd_with_crlf, sizeof(cmd_with_crlf), "%s\r\n", cmd);
    uart_write_bytes(modem_uart_num, cmd_with_crlf, strlen(cmd_with_crlf));

    // Wait for response
    int len = 0;
    int total_len = 0;
    int64_t start_time = esp_timer_get_time() / 1000;

    while ((esp_timer_get_time() / 1000 - start_time) < timeout_ms) {
        len = uart_read_bytes(modem_uart_num, at_rx_buffer, sizeof(at_rx_buffer) - 1,
                             pdMS_TO_TICKS(100));
        if (len > 0) {
            at_rx_buffer[len] = '\0';
            if (total_len + len < (int)response_size - 1) {
                memcpy(response + total_len, at_rx_buffer, len);
                total_len += len;
                response[total_len] = '\0';

                // Check for expected response
                if (expected && strstr(response, expected)) {
                    ESP_LOGI(TAG, "<<< (len=%d) %s", total_len,
                             total_len < 200 ? response : "[response too long to log]");
                    return ESP_OK;
                }

                // Check for error
                if (strstr(response, "ERROR")) {
                    ESP_LOGE(TAG, "<<< ERROR: %s", response);
                    return ESP_FAIL;
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    ESP_LOGW(TAG, "Timeout waiting for: %s (got: %s)", expected ? expected : "response",
             total_len > 0 ? response : "[nothing]");
    return ESP_ERR_TIMEOUT;
}

// Simple AT command without capturing response
static esp_err_t send_at_simple(const char* cmd, const char* expected, int timeout_ms)
{
    return send_at_cmd(cmd, expected, at_response, sizeof(at_response), timeout_ms);
}

// Exit PPP data mode to enable AT commands
static esp_err_t exit_ppp_for_http(void)
{
    ESP_LOGI(TAG, "Exiting PPP mode for HTTP operations...");

    // Use the PPP module's pause function which properly stops UART RX task first
    esp_err_t ret = a7670c_ppp_pause_for_at();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to pause PPP for AT commands");
        return ret;
    }

    ESP_LOGI(TAG, "Modem is in command mode");
    return ESP_OK;
}

// Disconnect current HTTPS session
static void shhttp_disconnect(void) {
    if (http_connected) {
        send_at_simple("AT+SHDISC", "OK", 5000);
        http_connected = false;
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

// Setup modem network (SIM, registration, APN, PDP)
static esp_err_t setup_modem_network(void)
{
    esp_err_t ret;

    // Check SIM
    ESP_LOGI(TAG, "Checking SIM card...");
    int sim_retries = 10;
    while (sim_retries-- > 0) {
        ret = send_at_simple("AT+CPIN?", "READY", 2000);
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "SIM OK");
            break;
        }
        ESP_LOGW(TAG, "SIM not ready, retrying... (%d/10)", 10 - sim_retries);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    if (sim_retries <= 0) {
        ESP_LOGE(TAG, "SIM card not ready");
        return ESP_FAIL;
    }

    // Wait for network registration
    ESP_LOGI(TAG, "Waiting for network registration...");
    int net_retries = 30;
    while (net_retries-- > 0) {
        ret = send_at_cmd("AT+CREG?", "OK", at_response, sizeof(at_response), 2000);
        if (ret == ESP_OK) {
            // Check for registered (1 or 5 or 6)
            if (strstr(at_response, ",1") || strstr(at_response, ",5") || strstr(at_response, ",6")) {
                ESP_LOGI(TAG, "Network registered");
                break;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(2000));
    }

    if (net_retries <= 0) {
        ESP_LOGE(TAG, "Network registration timeout");
        return ESP_ERR_TIMEOUT;
    }

    // Get APN from system config
    system_config_t *config = get_system_config();
    const char *apn = (config && strlen(config->sim_config.apn) > 0) ? config->sim_config.apn : "airteliot";

    // Set APN
    ESP_LOGI(TAG, "Setting APN: %s", apn);
    char apn_cmd[128];
    snprintf(apn_cmd, sizeof(apn_cmd), "AT+CGDCONT=1,\"IP\",\"%s\"", apn);
    send_at_simple(apn_cmd, "OK", 2000);

    // Attach to packet service
    ESP_LOGI(TAG, "Attaching to packet service...");
    send_at_simple("AT+CGATT=1", "OK", 10000);
    vTaskDelay(pdMS_TO_TICKS(2000));

    // Activate PDP context
    ESP_LOGI(TAG, "Activating PDP context...");
    send_at_simple("AT+CGACT=1,1", "OK", 10000);
    vTaskDelay(pdMS_TO_TICKS(2000));

    return ESP_OK;
}

// Connect to HTTPS server using AT+SH* commands
static esp_err_t shhttp_connect(const char* url)
{
    esp_err_t ret;
    parsed_url_t parsed;

    if (!parse_url(url, &parsed)) {
        ESP_LOGE(TAG, "Failed to parse URL: %s", url);
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Connecting to: %s (port %d, path: %s)", parsed.host, parsed.port, parsed.path);

    // Disconnect any existing session
    shhttp_disconnect();

    // Configure SSL (must be done before SHCONN)
    // Context 0, ignore RTC time (for certificate validation)
    send_at_simple("AT+CSSLCFG=\"ignorertctime\",0,1", "OK", 2000);

    // TLS 1.2 (value 3 = TLS 1.2, value 4 = ALL)
    send_at_simple("AT+CSSLCFG=\"sslversion\",0,3", "OK", 2000);

    // Enable SNI (Server Name Indication) - required for most HTTPS servers
    char sni_cmd[300];
    snprintf(sni_cmd, sizeof(sni_cmd), "AT+CSSLCFG=\"sni\",0,\"%s\"", parsed.host);
    send_at_simple(sni_cmd, "OK", 2000);

    // Trust all certificates (no CA verification) - SSL context 1, empty cert name
    // This is acceptable for OTA from known sources
    ret = send_at_simple("AT+SHSSL=1,\"\"", "OK", 2000);
    if (ret != ESP_OK) {
        // Try alternative syntax
        send_at_simple("AT+SHSSL=1", "OK", 2000);
    }

    // Configure SHHTTP
    // Set the full URL (including https://)
    char url_cmd[1500];
    snprintf(url_cmd, sizeof(url_cmd), "AT+SHCONF=\"URL\",\"%s\"", url);
    ret = send_at_simple(url_cmd, "OK", 5000);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set URL");
        return ret;
    }

    // Body length (0 for GET)
    send_at_simple("AT+SHCONF=\"BODYLEN\",0", "OK", 2000);

    // Header length
    send_at_simple("AT+SHCONF=\"HEADERLEN\",512", "OK", 2000);

    // Connection timeout
    send_at_simple("AT+SHCONF=\"TIMEOUT\",60", "OK", 2000);

    // Connect (TLS handshake happens here)
    ESP_LOGI(TAG, "Establishing HTTPS connection (TLS handshake)...");
    ret = send_at_simple("AT+SHCONN", "OK", 60000);  // Long timeout for TLS
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SHCONN failed - TLS handshake error");
        return ret;
    }

    // Verify connection state
    ret = send_at_cmd("AT+SHSTATE?", "+SHSTATE:", at_response, sizeof(at_response), 2000);
    if (ret == ESP_OK && strstr(at_response, "+SHSTATE: 1")) {
        ESP_LOGI(TAG, "HTTPS connected successfully!");
        http_connected = true;
    } else {
        ESP_LOGE(TAG, "Connection not established (SHSTATE != 1)");
        return ESP_FAIL;
    }

    return ESP_OK;
}

// Perform HTTPS GET request
static esp_err_t shhttp_get(int* http_status, size_t* content_length)
{
    if (!http_connected) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t ret;

    // Clear headers
    send_at_simple("AT+SHCHEAD", "OK", 2000);

    // Add headers
    send_at_simple("AT+SHAHEAD=\"User-Agent\",\"ESP32-OTA/1.0\"", "OK", 2000);
    send_at_simple("AT+SHAHEAD=\"Accept\",\"*/*\"", "OK", 2000);
    send_at_simple("AT+SHAHEAD=\"Connection\",\"keep-alive\"", "OK", 2000);

    // Perform GET request (method 1 = GET)
    // The path is already included in the URL set via SHCONF
    // Response format: +SHREQ: "GET",<status>,<length>
    ESP_LOGI(TAG, "Sending GET request...");
    ret = send_at_cmd("AT+SHREQ=\"/\",1", "+SHREQ:", at_response, sizeof(at_response), 120000);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SHREQ failed");
        return ret;
    }

    // Parse response: +SHREQ: "GET",<status>,<length>
    char* shreq_line = strstr(at_response, "+SHREQ:");
    if (shreq_line) {
        char method[16];
        int status = 0, length = 0;
        if (sscanf(shreq_line, "+SHREQ: \"%[^\"]\", %d, %d", method, &status, &length) == 3 ||
            sscanf(shreq_line, "+SHREQ: \"%[^\"]\",%d,%d", method, &status, &length) == 3) {
            *http_status = status;
            *content_length = length;
            current_content_length = length;
            ESP_LOGI(TAG, "HTTP Response: status=%d, content_length=%d", status, length);
            return ESP_OK;
        }
    }

    ESP_LOGE(TAG, "Failed to parse SHREQ response: %s", at_response);
    return ESP_FAIL;
}

// Read data from HTTPS response
static esp_err_t shhttp_read(uint8_t* buffer, size_t buffer_size, size_t offset, size_t* bytes_read)
{
    if (!http_connected || buffer == NULL || bytes_read == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    *bytes_read = 0;

    // Calculate how much to read
    size_t to_read = buffer_size;
    if (current_content_length > 0 && offset + to_read > current_content_length) {
        to_read = current_content_length - offset;
    }

    if (to_read == 0) {
        return ESP_OK;  // Nothing more to read
    }

    // AT+SHREAD=<offset>,<length>
    // Response: +SHREAD: <actual_length>\r\n<data>
    char read_cmd[64];
    snprintf(read_cmd, sizeof(read_cmd), "AT+SHREAD=%zu,%zu", offset, to_read);

    ESP_LOGI(TAG, ">>> %s", read_cmd);
    char cmd_with_crlf[128];
    snprintf(cmd_with_crlf, sizeof(cmd_with_crlf), "%s\r\n", read_cmd);
    uart_write_bytes(modem_uart_num, cmd_with_crlf, strlen(cmd_with_crlf));

    // Wait for +SHREAD header
    char header_buf[128] = {0};
    int header_len = 0;
    int64_t start_time = esp_timer_get_time() / 1000;
    int actual_length = 0;
    bool found_header = false;

    // First, read until we get +SHREAD: <length>
    while ((esp_timer_get_time() / 1000 - start_time) < 30000) {
        int len = uart_read_bytes(modem_uart_num, at_rx_buffer, 1, pdMS_TO_TICKS(100));
        if (len > 0) {
            if (header_len < (int)sizeof(header_buf) - 1) {
                header_buf[header_len++] = at_rx_buffer[0];
                header_buf[header_len] = '\0';

                // Check if we have the complete header line
                char* shread = strstr(header_buf, "+SHREAD:");
                if (shread) {
                    char* newline = strchr(shread, '\n');
                    if (newline) {
                        // Parse length
                        if (sscanf(shread, "+SHREAD: %d", &actual_length) == 1 ||
                            sscanf(shread, "+SHREAD:%d", &actual_length) == 1) {
                            ESP_LOGI(TAG, "Reading %d bytes of data...", actual_length);
                            found_header = true;
                            break;
                        }
                    }
                }

                // Check for error
                if (strstr(header_buf, "ERROR")) {
                    ESP_LOGE(TAG, "SHREAD error: %s", header_buf);
                    return ESP_FAIL;
                }
            }
        }
    }

    if (!found_header || actual_length == 0) {
        ESP_LOGW(TAG, "No data to read");
        return ESP_OK;
    }

    // Now read the binary data
    size_t data_read = 0;
    start_time = esp_timer_get_time() / 1000;

    while (data_read < (size_t)actual_length && (esp_timer_get_time() / 1000 - start_time) < 60000) {
        size_t remaining = actual_length - data_read;
        size_t chunk = remaining > sizeof(at_rx_buffer) ? sizeof(at_rx_buffer) : remaining;

        int len = uart_read_bytes(modem_uart_num, at_rx_buffer, chunk, pdMS_TO_TICKS(1000));
        if (len > 0) {
            // Copy to output buffer
            size_t copy_len = len;
            if (data_read + copy_len > buffer_size) {
                copy_len = buffer_size - data_read;
            }
            memcpy(buffer + data_read, at_rx_buffer, copy_len);
            data_read += len;
        }
    }

    // Wait for OK
    vTaskDelay(pdMS_TO_TICKS(100));
    uart_flush(modem_uart_num);

    *bytes_read = data_read;
    ESP_LOGI(TAG, "Read %zu bytes", data_read);

    return ESP_OK;
}

// Read Location header from redirect response
static esp_err_t read_redirect_location(char* location, size_t location_size)
{
    // Read response body which may contain redirect info
    // For HTTP redirects, the modem might have the URL in the response
    uint8_t temp_buf[1024];
    size_t bytes_read = 0;

    esp_err_t ret = shhttp_read(temp_buf, sizeof(temp_buf) - 1, 0, &bytes_read);
    if (ret == ESP_OK && bytes_read > 0) {
        temp_buf[bytes_read] = '\0';
        ESP_LOGI(TAG, "Redirect response: %s", temp_buf);

        // Look for Location header in response
        char* loc = strstr((char*)temp_buf, "Location:");
        if (!loc) loc = strstr((char*)temp_buf, "location:");

        if (loc) {
            loc += 9;  // Skip "Location:"
            while (*loc == ' ') loc++;  // Skip whitespace

            // Copy URL until newline or end
            int i = 0;
            while (loc[i] && loc[i] != '\r' && loc[i] != '\n' && i < (int)location_size - 1) {
                location[i] = loc[i];
                i++;
            }
            location[i] = '\0';
            return ESP_OK;
        }
    }

    return ESP_FAIL;
}

esp_err_t a7670c_http_init(void)
{
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "Initializing Modem HTTPS Service (SHHTTP)");
    ESP_LOGI(TAG, "========================================");

    // Get UART number from PPP module
    modem_uart_num = a7670c_get_uart_num();
    if (modem_uart_num < 0) {
        ESP_LOGE(TAG, "Modem UART not available");
        return ESP_ERR_INVALID_STATE;
    }

    // Exit PPP mode - this will power cycle the modem
    esp_err_t ret = exit_ppp_for_http();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to exit PPP mode");
        return ret;
    }

    // Setup network
    ret = setup_modem_network();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to setup network");
        return ret;
    }

    http_initialized = true;
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "Modem HTTPS service ready!");
    ESP_LOGI(TAG, "========================================");
    return ESP_OK;
}

esp_err_t a7670c_http_terminate(void)
{
    shhttp_disconnect();
    http_initialized = false;
    return ESP_OK;
}

esp_err_t a7670c_http_get(const char* url, modem_http_status_t* status, bool follow_redirects)
{
    if (!http_initialized || url == NULL || status == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(status, 0, sizeof(modem_http_status_t));
    esp_err_t ret;

    // Connect to server
    ret = shhttp_connect(url);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to connect to server");
        return ret;
    }

    // Perform GET
    int http_status = 0;
    size_t content_length = 0;
    ret = shhttp_get(&http_status, &content_length);
    if (ret != ESP_OK) {
        return ret;
    }

    status->status_code = http_status;
    status->content_length = content_length;
    status->is_redirect = (http_status >= 300 && http_status < 400);

    return ESP_OK;
}

esp_err_t a7670c_http_read(uint8_t* buffer, size_t buffer_size, size_t offset, size_t* bytes_read)
{
    return shhttp_read(buffer, buffer_size, offset, bytes_read);
}

esp_err_t a7670c_http_download_ota(const char* url, modem_http_progress_cb_t progress_cb)
{
    if (url == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "Starting Modem HTTPS OTA Download");
    ESP_LOGI(TAG, "URL: %s", url);
    ESP_LOGI(TAG, "========================================");

    esp_err_t ret;
    esp_ota_handle_t ota_handle = 0;
    const esp_partition_t *ota_partition = NULL;
    uint8_t *download_buffer = NULL;
    const size_t CHUNK_SIZE = 2048;  // Download in 2KB chunks
    bool ota_started = false;
    int last_progress = -10;

    // Initialize HTTP
    ret = a7670c_http_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize modem HTTPS");
        return ret;
    }

    // Allocate download buffer
    download_buffer = malloc(CHUNK_SIZE);
    if (!download_buffer) {
        ESP_LOGE(TAG, "Failed to allocate download buffer");
        a7670c_http_terminate();
        return ESP_ERR_NO_MEM;
    }

    // Handle GitHub redirects manually
    // GitHub returns 302 redirect to actual S3 URL
    const char* current_url = url;
    char redirect_url[1024] = {0};
    int redirect_count = 0;
    const int MAX_REDIRECTS = 5;
    int http_status = 0;
    size_t content_length = 0;

    while (redirect_count < MAX_REDIRECTS) {
        // Connect and perform GET
        ret = shhttp_connect(current_url);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to connect");
            goto cleanup;
        }

        ret = shhttp_get(&http_status, &content_length);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "GET request failed");
            goto cleanup;
        }

        ESP_LOGI(TAG, "HTTP status: %d, content_length: %zu", http_status, content_length);

        if (http_status == 200) {
            // Success - continue to download
            ESP_LOGI(TAG, "Got HTTP 200 OK, ready to download");
            break;
        } else if (http_status >= 300 && http_status < 400) {
            // Redirect - need to follow
            ESP_LOGI(TAG, "Redirect (%d) - reading location...", http_status);

            ret = read_redirect_location(redirect_url, sizeof(redirect_url));
            if (ret == ESP_OK && strlen(redirect_url) > 0) {
                ESP_LOGI(TAG, "Redirect to: %s", redirect_url);
                current_url = redirect_url;
                redirect_count++;

                // Disconnect and retry with new URL
                shhttp_disconnect();
                vTaskDelay(pdMS_TO_TICKS(1000));
                continue;
            } else {
                ESP_LOGE(TAG, "Failed to get redirect location");
                ret = ESP_FAIL;
                goto cleanup;
            }
        } else {
            ESP_LOGE(TAG, "HTTP error: %d", http_status);
            ret = ESP_FAIL;
            goto cleanup;
        }
    }

    if (http_status != 200) {
        ESP_LOGE(TAG, "Failed to get HTTP 200 after %d redirects", redirect_count);
        ret = ESP_FAIL;
        goto cleanup;
    }

    // Get OTA partition
    ota_partition = esp_ota_get_next_update_partition(NULL);
    if (ota_partition == NULL) {
        ESP_LOGE(TAG, "No OTA partition available");
        ret = ESP_FAIL;
        goto cleanup;
    }

    ESP_LOGI(TAG, "Writing to partition: %s @ 0x%lx", ota_partition->label, ota_partition->address);

    // Start OTA
    ret = esp_ota_begin(ota_partition, OTA_SIZE_UNKNOWN, &ota_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_begin failed: %s", esp_err_to_name(ret));
        goto cleanup;
    }
    ota_started = true;

    // Download and write firmware in chunks
    size_t total_downloaded = 0;

    ESP_LOGI(TAG, "Downloading firmware (%zu bytes)...", content_length);

    while (1) {
        size_t bytes_read = 0;

        ret = shhttp_read(download_buffer, CHUNK_SIZE, total_downloaded, &bytes_read);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to read HTTP data");
            goto cleanup;
        }

        if (bytes_read == 0) {
            // No more data
            break;
        }

        // Write to flash
        ret = esp_ota_write(ota_handle, download_buffer, bytes_read);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "esp_ota_write failed: %s", esp_err_to_name(ret));
            goto cleanup;
        }

        total_downloaded += bytes_read;

        // Calculate and report progress
        int progress = 0;
        if (content_length > 0) {
            progress = (total_downloaded * 100) / content_length;
        }

        if (progress_cb) {
            progress_cb(progress, total_downloaded, content_length);
        }

        // Log progress every 10%
        if (progress >= last_progress + 10) {
            ESP_LOGI(TAG, "Download progress: %d%% (%zu/%zu bytes)", progress, total_downloaded, content_length);
            last_progress = progress;
        }

        // Check if we've downloaded everything
        if (content_length > 0 && total_downloaded >= content_length) {
            break;
        }
    }

    ESP_LOGI(TAG, "Download complete: %zu bytes", total_downloaded);

    // Verify and finalize OTA
    ret = esp_ota_end(ota_handle);
    ota_handle = 0;
    ota_started = false;

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_end failed: %s", esp_err_to_name(ret));
        goto cleanup;
    }

    // Set boot partition
    ret = esp_ota_set_boot_partition(ota_partition);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_set_boot_partition failed: %s", esp_err_to_name(ret));
        goto cleanup;
    }

    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "OTA Update Successful!");
    ESP_LOGI(TAG, "Firmware written to: %s", ota_partition->label);
    ESP_LOGI(TAG, "Total bytes: %zu", total_downloaded);
    ESP_LOGI(TAG, "========================================");

    ret = ESP_OK;

cleanup:
    if (ota_started && ota_handle) {
        esp_ota_abort(ota_handle);
    }
    if (download_buffer) {
        free(download_buffer);
    }
    a7670c_http_terminate();

    return ret;
}

bool a7670c_http_is_available(void)
{
    // Check if we're in SIM mode
    system_config_t *config = get_system_config();
    if (config == NULL || config->network_mode != NETWORK_MODE_SIM) {
        return false;
    }

    // Check if modem UART is available
    int uart_num = a7670c_get_uart_num();
    return (uart_num >= 0);
}
