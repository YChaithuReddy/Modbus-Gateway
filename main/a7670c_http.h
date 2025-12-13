/**
 * @file a7670c_http.h
 * @brief A7670C Modem HTTP/HTTPS functions for OTA updates
 *
 * This module provides HTTP functionality using the modem's built-in
 * HTTP stack, which is more reliable over mobile networks than ESP32's
 * HTTP client for certain CDN providers (like GitHub).
 */

#ifndef A7670C_HTTP_H
#define A7670C_HTTP_H

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

// HTTP action types
typedef enum {
    MODEM_HTTP_GET = 0,
    MODEM_HTTP_POST = 1,
    MODEM_HTTP_HEAD = 2
} modem_http_action_t;

// HTTP status structure
typedef struct {
    int status_code;        // HTTP status code (200, 302, etc.)
    int content_length;     // Content-Length from response
    bool is_redirect;       // True if 3xx redirect
    char redirect_url[512]; // Redirect URL if applicable
} modem_http_status_t;

// Progress callback for OTA downloads
typedef void (*modem_http_progress_cb_t)(int progress, int bytes_downloaded, int total_bytes);

/**
 * @brief Initialize modem HTTP service
 *
 * Must be called before any other HTTP functions.
 * Automatically exits PPP mode if active.
 *
 * @return ESP_OK on success
 */
esp_err_t a7670c_http_init(void);

/**
 * @brief Terminate modem HTTP service
 *
 * Frees modem HTTP resources.
 *
 * @return ESP_OK on success
 */
esp_err_t a7670c_http_terminate(void);

/**
 * @brief Perform HTTP GET request
 *
 * @param url URL to fetch
 * @param status Output parameter for HTTP status
 * @param follow_redirects If true, automatically follow redirects
 * @return ESP_OK on success
 */
esp_err_t a7670c_http_get(const char* url, modem_http_status_t* status, bool follow_redirects);

/**
 * @brief Read HTTP response data
 *
 * Call this after a7670c_http_get() to read response body.
 *
 * @param buffer Buffer to store data
 * @param buffer_size Size of buffer
 * @param offset Starting offset in response
 * @param bytes_read Output: actual bytes read
 * @return ESP_OK on success, ESP_ERR_NOT_FINISHED if more data available
 */
esp_err_t a7670c_http_read(uint8_t* buffer, size_t buffer_size, size_t offset, size_t* bytes_read);

/**
 * @brief Download firmware via modem HTTP and write to OTA partition
 *
 * This is the main function for modem-based OTA updates.
 * Handles the complete download and flash write process.
 *
 * @param url Firmware URL (HTTPS supported)
 * @param progress_cb Optional progress callback
 * @return ESP_OK on success
 */
esp_err_t a7670c_http_download_ota(const char* url, modem_http_progress_cb_t progress_cb);

/**
 * @brief Check if modem HTTP is available
 *
 * Checks if we're in SIM mode and modem is accessible.
 *
 * @return true if modem HTTP can be used
 */
bool a7670c_http_is_available(void);

#endif // A7670C_HTTP_H
