/**
 * @file a7670c_http.c
 * @brief A7670C Modem HTTP/HTTPS implementation for OTA updates
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
static int modem_uart_num = -1;

// UART buffers for AT commands
static uint8_t at_rx_buffer[4096];
static char at_response[4096];

// Send AT command and get full response (similar to a7670c_ppp.c but accessible here)
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

esp_err_t a7670c_http_init(void)
{
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "Initializing Modem HTTP Service");
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

    // After power cycle, we need to set up SIM and network
    ESP_LOGI(TAG, "Setting up modem for HTTP...");

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
        if (send_at_simple("AT+CREG?", ",1", 2000) == ESP_OK ||
            send_at_simple("AT+CREG?", ",5", 2000) == ESP_OK ||
            send_at_simple("AT+CREG?", ",6", 2000) == ESP_OK) {
            ESP_LOGI(TAG, "Network registered");
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(2000));
    }

    if (net_retries <= 0) {
        ESP_LOGE(TAG, "Network registration timeout");
        return ESP_ERR_TIMEOUT;
    }

    // Get APN from system config
    system_config_t *config = get_system_config();
    const char *apn = (config && strlen(config->sim_apn) > 0) ? config->sim_apn : "airteliot";

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

    // Terminate any existing HTTP session
    send_at_simple("AT+HTTPTERM", "OK", 2000);
    vTaskDelay(pdMS_TO_TICKS(500));

    // Initialize HTTP service
    ESP_LOGI(TAG, "Initializing HTTP service...");
    ret = send_at_simple("AT+HTTPINIT", "OK", 5000);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize HTTP service");
        return ret;
    }

    // Configure SSL for HTTPS
    // SSL context 0, TLS 1.2
    send_at_simple("AT+CSSLCFG=\"sslversion\",0,4", "OK", 2000);
    send_at_simple("AT+CSSLCFG=\"ignorelocaltime\",0,1", "OK", 2000);
    send_at_simple("AT+CSSLCFG=\"enableSNI\",0,1", "OK", 2000);

    // Enable HTTPS
    ret = send_at_simple("AT+HTTPSSL=1", "OK", 2000);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "HTTPSSL command failed, continuing anyway...");
    }

    // Set User-Agent (required for GitHub)
    send_at_simple("AT+HTTPPARA=\"UA\",\"Mozilla/5.0 (Linux; Android 10) AppleWebKit/537.36\"", "OK", 2000);

    // Set content type
    send_at_simple("AT+HTTPPARA=\"CONTENT\",\"application/octet-stream\"", "OK", 2000);

    // Enable redirect following
    send_at_simple("AT+HTTPPARA=\"REDIR\",1", "OK", 2000);

    http_initialized = true;
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "Modem HTTP service initialized successfully!");
    ESP_LOGI(TAG, "========================================");
    return ESP_OK;
}

esp_err_t a7670c_http_terminate(void)
{
    if (!http_initialized) {
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Terminating HTTP service...");

    send_at_simple("AT+HTTPTERM", "OK", 2000);
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

    ESP_LOGI(TAG, "HTTP GET: %s", url);

    // Set URL
    char url_cmd[2048];
    snprintf(url_cmd, sizeof(url_cmd), "AT+HTTPPARA=\"URL\",\"%s\"", url);
    ret = send_at_simple(url_cmd, "OK", 5000);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set URL");
        return ret;
    }

    // Perform GET request
    // Response format: +HTTPACTION: <method>,<status_code>,<data_length>
    ret = send_at_cmd("AT+HTTPACTION=0", "+HTTPACTION:", at_response, sizeof(at_response), 120000);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "HTTP GET request failed");
        return ret;
    }

    // Parse response: +HTTPACTION: 0,<status>,<length>
    char* action_line = strstr(at_response, "+HTTPACTION:");
    if (action_line) {
        int method, http_status, content_len;
        if (sscanf(action_line, "+HTTPACTION: %d,%d,%d", &method, &http_status, &content_len) == 3) {
            status->status_code = http_status;
            status->content_length = content_len;
            status->is_redirect = (http_status >= 300 && http_status < 400);

            ESP_LOGI(TAG, "HTTP Response: status=%d, content_length=%d", http_status, content_len);

            // Handle redirects
            if (status->is_redirect && follow_redirects) {
                // Read redirect response to get Location header
                // The modem may have already followed the redirect with REDIR=1
                ESP_LOGI(TAG, "Redirect detected (status %d)", http_status);
            }
        } else {
            ESP_LOGE(TAG, "Failed to parse HTTPACTION response");
            return ESP_FAIL;
        }
    } else {
        ESP_LOGE(TAG, "No HTTPACTION in response");
        return ESP_FAIL;
    }

    return ESP_OK;
}

esp_err_t a7670c_http_read(uint8_t* buffer, size_t buffer_size, size_t offset, size_t* bytes_read)
{
    if (!http_initialized || buffer == NULL || bytes_read == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    *bytes_read = 0;

    // Request data chunk
    // AT+HTTPREAD=<offset>,<size>
    char read_cmd[64];
    snprintf(read_cmd, sizeof(read_cmd), "AT+HTTPREAD=%zu,%zu", offset, buffer_size);

    // Response format:
    // +HTTPREAD: <actual_length>
    // <binary data>
    // OK

    ESP_LOGI(TAG, ">>> %s", read_cmd);
    char cmd_with_crlf[128];
    snprintf(cmd_with_crlf, sizeof(cmd_with_crlf), "%s\r\n", read_cmd);
    uart_write_bytes(modem_uart_num, cmd_with_crlf, strlen(cmd_with_crlf));

    // Wait for +HTTPREAD header
    char header_buf[128] = {0};
    int header_len = 0;
    int64_t start_time = esp_timer_get_time() / 1000;
    int actual_length = 0;

    // First, read until we get +HTTPREAD: <length>
    while ((esp_timer_get_time() / 1000 - start_time) < 30000) {
        int len = uart_read_bytes(modem_uart_num, at_rx_buffer, 1, pdMS_TO_TICKS(100));
        if (len > 0) {
            if (header_len < (int)sizeof(header_buf) - 1) {
                header_buf[header_len++] = at_rx_buffer[0];
                header_buf[header_len] = '\0';

                // Check if we have the complete header line
                char* httpread = strstr(header_buf, "+HTTPREAD:");
                if (httpread) {
                    char* newline = strchr(httpread, '\n');
                    if (newline) {
                        // Parse length
                        if (sscanf(httpread, "+HTTPREAD: %d", &actual_length) == 1) {
                            ESP_LOGI(TAG, "Reading %d bytes of data...", actual_length);
                            break;
                        }
                    }
                }

                // Check for error
                if (strstr(header_buf, "ERROR")) {
                    ESP_LOGE(TAG, "HTTPREAD error: %s", header_buf);
                    return ESP_FAIL;
                }
            }
        }
    }

    if (actual_length == 0) {
        ESP_LOGW(TAG, "No data to read (length=0)");
        return ESP_OK;  // Not an error, just no more data
    }

    // Now read the binary data
    size_t data_read = 0;
    start_time = esp_timer_get_time() / 1000;

    while (data_read < (size_t)actual_length && (esp_timer_get_time() / 1000 - start_time) < 60000) {
        size_t to_read = actual_length - data_read;
        if (to_read > sizeof(at_rx_buffer)) {
            to_read = sizeof(at_rx_buffer);
        }

        int len = uart_read_bytes(modem_uart_num, at_rx_buffer, to_read, pdMS_TO_TICKS(1000));
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

esp_err_t a7670c_http_download_ota(const char* url, modem_http_progress_cb_t progress_cb)
{
    if (url == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "Starting Modem HTTP OTA Download");
    ESP_LOGI(TAG, "URL: %s", url);
    ESP_LOGI(TAG, "========================================");

    esp_err_t ret;
    modem_http_status_t http_status;
    esp_ota_handle_t ota_handle = 0;
    const esp_partition_t *ota_partition = NULL;
    uint8_t *download_buffer = NULL;
    const size_t CHUNK_SIZE = 2048;  // Download in 2KB chunks
    bool ota_started = false;
    int last_progress = -10;

    // Initialize HTTP
    ret = a7670c_http_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize modem HTTP");
        return ret;
    }

    // Allocate download buffer
    download_buffer = malloc(CHUNK_SIZE);
    if (!download_buffer) {
        ESP_LOGE(TAG, "Failed to allocate download buffer");
        a7670c_http_terminate();
        return ESP_ERR_NO_MEM;
    }

    // Handle redirects manually for GitHub URLs
    const char* current_url = url;
    char redirect_url[2048] = {0};
    int redirect_count = 0;
    const int MAX_REDIRECTS = 5;

    while (redirect_count < MAX_REDIRECTS) {
        // Perform GET request
        ret = a7670c_http_get(current_url, &http_status, false);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "HTTP GET failed");
            goto cleanup;
        }

        // Check for redirect (modem with REDIR=1 should follow automatically)
        if (http_status.status_code == 200) {
            ESP_LOGI(TAG, "Got HTTP 200 OK, content_length=%d", http_status.content_length);
            break;
        } else if (http_status.is_redirect) {
            // Need to read response to get Location header
            // For now, rely on modem's auto-redirect (REDIR=1)
            ESP_LOGW(TAG, "Redirect status %d - modem should follow automatically", http_status.status_code);
            redirect_count++;

            // Re-initialize HTTP for redirect
            a7670c_http_terminate();
            vTaskDelay(pdMS_TO_TICKS(1000));

            ret = a7670c_http_init();
            if (ret != ESP_OK) {
                goto cleanup;
            }
        } else {
            ESP_LOGE(TAG, "HTTP error: %d", http_status.status_code);
            ret = ESP_FAIL;
            goto cleanup;
        }
    }

    if (http_status.status_code != 200) {
        ESP_LOGE(TAG, "Failed to get HTTP 200 after redirects");
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
    size_t content_length = http_status.content_length > 0 ? http_status.content_length : 0;

    ESP_LOGI(TAG, "Downloading firmware (%d bytes)...", http_status.content_length);

    while (1) {
        size_t bytes_read = 0;

        ret = a7670c_http_read(download_buffer, CHUNK_SIZE, total_downloaded, &bytes_read);
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
