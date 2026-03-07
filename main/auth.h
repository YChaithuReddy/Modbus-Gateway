// auth.h - Web authentication for ESP32 IoT Gateway
#ifndef AUTH_H
#define AUTH_H

#include "esp_err.h"
#include "esp_http_server.h"
#include <stdbool.h>

// Max concurrent sessions
#define AUTH_MAX_SESSIONS 3
// Session timeout in seconds (8 hours)
#define AUTH_SESSION_TIMEOUT_SEC (8 * 3600)
// Session token length (hex string)
#define AUTH_TOKEN_LEN 32

// Initialize auth module - loads credentials from NVS, sets defaults if first boot
esp_err_t auth_init(void);

// Verify username and password against stored credentials
bool auth_verify_credentials(const char *username, const char *password);

// Create a new session, returns token string (caller must not free - static buffer)
const char* auth_create_session(void);

// Check if a session token is valid
bool auth_check_session(const char *token);

// Destroy a session (logout)
void auth_destroy_session(const char *token);

// Check HTTP request for valid auth (session cookie or basic auth)
// Returns true if authenticated
bool auth_check_request(httpd_req_t *req);

// Change password (verifies old password first)
esp_err_t auth_change_password(const char *old_password, const char *new_password);

// Get stored username
const char* auth_get_username(void);

// Macro to check auth at the start of any handler
#define CHECK_AUTH(req) do { \
    if (!auth_check_request(req)) { \
        httpd_resp_set_status(req, "401 Unauthorized"); \
        httpd_resp_set_type(req, "application/json"); \
        httpd_resp_sendstr(req, "{\"error\":\"Authentication required\"}"); \
        return ESP_OK; \
    } \
} while(0)

// Check auth but redirect to login page for browser requests
#define CHECK_AUTH_PAGE(req) do { \
    if (!auth_check_request(req)) { \
        httpd_resp_set_status(req, "302 Found"); \
        httpd_resp_set_hdr(req, "Location", "/login"); \
        httpd_resp_sendstr(req, "Redirecting to login..."); \
        return ESP_OK; \
    } \
} while(0)

#endif // AUTH_H
