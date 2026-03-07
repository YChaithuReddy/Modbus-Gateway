// auth.c - Web authentication for ESP32 IoT Gateway
// SHA-256 password hashing, session tokens, NVS credential storage

#include "auth.h"
#include "esp_log.h"
#include "esp_random.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "mbedtls/sha256.h"
#include <string.h>
#include <stdio.h>
#include <time.h>
#include "esp_timer.h"

static const char *TAG = "AUTH";

// NVS namespace for auth
#define AUTH_NVS_NAMESPACE "auth"
#define AUTH_NVS_KEY_USERNAME "username"
#define AUTH_NVS_KEY_PWHASH "pw_hash"
#define AUTH_NVS_KEY_INIT "auth_init"

// Default credentials
#define AUTH_DEFAULT_USERNAME "admin"
#define AUTH_DEFAULT_PASSWORD "fluxgen123"

// SHA-256 hash length
#define SHA256_LEN 32

// Session storage
typedef struct {
    bool active;
    char token[AUTH_TOKEN_LEN + 1];
    int64_t created_at;  // esp_timer timestamp in microseconds
} auth_session_t;

static auth_session_t sessions[AUTH_MAX_SESSIONS] = {0};
static char stored_username[32] = {0};
static uint8_t stored_pw_hash[SHA256_LEN] = {0};
static bool auth_initialized = false;

// Hash a password with SHA-256
static void hash_password(const char *password, uint8_t *out_hash) {
    mbedtls_sha256_context ctx;
    mbedtls_sha256_init(&ctx);
    mbedtls_sha256_starts(&ctx, 0); // 0 = SHA-256 (not SHA-224)
    mbedtls_sha256_update(&ctx, (const unsigned char *)password, strlen(password));
    mbedtls_sha256_finish(&ctx, out_hash);
    mbedtls_sha256_free(&ctx);
}

// Generate a random hex token
static void generate_token(char *token, size_t len) {
    for (size_t i = 0; i < len; i += 8) {
        uint32_t rand = esp_random();
        size_t remaining = len - i;
        size_t to_write = remaining > 8 ? 8 : remaining;
        snprintf(token + i, to_write + 1, "%08x", rand);
    }
    token[len] = '\0';
}

// Get current time in microseconds (for session expiry)
static int64_t get_time_us(void) {
    return esp_timer_get_time();
}

esp_err_t auth_init(void) {
    nvs_handle_t handle;
    esp_err_t err;

    err = nvs_open(AUTH_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS auth namespace: %s", esp_err_to_name(err));
        return err;
    }

    // Check if auth has been initialized before
    uint8_t init_flag = 0;
    err = nvs_get_u8(handle, AUTH_NVS_KEY_INIT, &init_flag);

    if (err == ESP_ERR_NVS_NOT_FOUND || init_flag == 0) {
        // First boot - set default credentials
        ESP_LOGI(TAG, "First boot: setting default credentials (admin/fluxgen123)");

        strncpy(stored_username, AUTH_DEFAULT_USERNAME, sizeof(stored_username) - 1);
        hash_password(AUTH_DEFAULT_PASSWORD, stored_pw_hash);

        nvs_set_str(handle, AUTH_NVS_KEY_USERNAME, stored_username);
        nvs_set_blob(handle, AUTH_NVS_KEY_PWHASH, stored_pw_hash, SHA256_LEN);
        nvs_set_u8(handle, AUTH_NVS_KEY_INIT, 1);
        nvs_commit(handle);

        ESP_LOGI(TAG, "Default credentials saved to NVS");
    } else {
        // Load existing credentials
        size_t username_len = sizeof(stored_username);
        nvs_get_str(handle, AUTH_NVS_KEY_USERNAME, stored_username, &username_len);

        size_t hash_len = SHA256_LEN;
        nvs_get_blob(handle, AUTH_NVS_KEY_PWHASH, stored_pw_hash, &hash_len);

        ESP_LOGI(TAG, "Loaded auth credentials for user: %s", stored_username);
    }

    nvs_close(handle);

    // Clear sessions
    memset(sessions, 0, sizeof(sessions));
    auth_initialized = true;

    return ESP_OK;
}

bool auth_verify_credentials(const char *username, const char *password) {
    if (!auth_initialized || !username || !password) return false;

    // Check username
    if (strcmp(username, stored_username) != 0) {
        ESP_LOGW(TAG, "Login failed: wrong username '%s'", username);
        return false;
    }

    // Hash provided password and compare
    uint8_t provided_hash[SHA256_LEN];
    hash_password(password, provided_hash);

    if (memcmp(provided_hash, stored_pw_hash, SHA256_LEN) != 0) {
        ESP_LOGW(TAG, "Login failed: wrong password for user '%s'", username);
        return false;
    }

    ESP_LOGI(TAG, "Login successful for user '%s'", username);
    return true;
}

const char* auth_create_session(void) {
    int64_t now = get_time_us();

    // Find a free slot or the oldest session
    int slot = -1;
    int64_t oldest_time = INT64_MAX;
    int oldest_slot = 0;

    for (int i = 0; i < AUTH_MAX_SESSIONS; i++) {
        if (!sessions[i].active) {
            slot = i;
            break;
        }
        // Check for expired sessions
        if ((now - sessions[i].created_at) > (int64_t)AUTH_SESSION_TIMEOUT_SEC * 1000000LL) {
            sessions[i].active = false;
            slot = i;
            break;
        }
        if (sessions[i].created_at < oldest_time) {
            oldest_time = sessions[i].created_at;
            oldest_slot = i;
        }
    }

    // If no free slot, evict oldest
    if (slot == -1) {
        slot = oldest_slot;
        ESP_LOGW(TAG, "Evicting oldest session to make room");
    }

    generate_token(sessions[slot].token, AUTH_TOKEN_LEN);
    sessions[slot].active = true;
    sessions[slot].created_at = now;

    ESP_LOGI(TAG, "Created session in slot %d", slot);
    return sessions[slot].token;
}

bool auth_check_session(const char *token) {
    if (!token || !auth_initialized) return false;

    int64_t now = get_time_us();

    for (int i = 0; i < AUTH_MAX_SESSIONS; i++) {
        if (!sessions[i].active) continue;

        // Check expiry
        if ((now - sessions[i].created_at) > (int64_t)AUTH_SESSION_TIMEOUT_SEC * 1000000LL) {
            sessions[i].active = false;
            continue;
        }

        if (strcmp(sessions[i].token, token) == 0) {
            return true;
        }
    }

    return false;
}

void auth_destroy_session(const char *token) {
    if (!token) return;

    for (int i = 0; i < AUTH_MAX_SESSIONS; i++) {
        if (sessions[i].active && strcmp(sessions[i].token, token) == 0) {
            sessions[i].active = false;
            memset(sessions[i].token, 0, sizeof(sessions[i].token));
            ESP_LOGI(TAG, "Session destroyed in slot %d", i);
            return;
        }
    }
}

bool auth_check_request(httpd_req_t *req) {
    if (!auth_initialized) return true; // Allow access if auth not initialized

    // Check for session cookie first
    char cookie_buf[256] = {0};
    if (httpd_req_get_hdr_value_str(req, "Cookie", cookie_buf, sizeof(cookie_buf)) == ESP_OK) {
        // Parse "session=<token>" from cookie string
        char *session_start = strstr(cookie_buf, "session=");
        if (session_start) {
            session_start += 8; // skip "session="
            char token[AUTH_TOKEN_LEN + 1] = {0};
            // Copy until ';' or end of string
            int i = 0;
            while (session_start[i] && session_start[i] != ';' && i < AUTH_TOKEN_LEN) {
                token[i] = session_start[i];
                i++;
            }
            token[i] = '\0';

            if (auth_check_session(token)) {
                return true;
            }
        }
    }

    // Check for HTTP Basic Auth
    char auth_buf[256] = {0};
    if (httpd_req_get_hdr_value_str(req, "Authorization", auth_buf, sizeof(auth_buf)) == ESP_OK) {
        if (strncmp(auth_buf, "Basic ", 6) == 0) {
            // Decode base64 - simple inline decoder for "username:password"
            // For ESP32, we just compare the whole encoded string
            // Create expected base64 for stored credentials
            // Actually, let's do a simple base64 decode
            const char *encoded = auth_buf + 6;

            // Simple base64 decode (handles basic auth credentials)
            static const char b64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
            char decoded[128] = {0};
            int j = 0;
            size_t enc_len = strlen(encoded);

            for (size_t i = 0; i < enc_len; i += 4) {
                uint32_t n = 0;
                int pad = 0;
                for (int k = 0; k < 4 && (i + k) < enc_len; k++) {
                    char c = encoded[i + k];
                    if (c == '=') { pad++; continue; }
                    const char *p = strchr(b64, c);
                    if (!p) continue;
                    n = (n << 6) | (uint32_t)(p - b64);
                }
                if (pad < 3 && j < (int)sizeof(decoded) - 1) decoded[j++] = (char)((n >> 16) & 0xFF);
                if (pad < 2 && j < (int)sizeof(decoded) - 1) decoded[j++] = (char)((n >> 8) & 0xFF);
                if (pad < 1 && j < (int)sizeof(decoded) - 1) decoded[j++] = (char)(n & 0xFF);
            }
            decoded[j] = '\0';

            // Split "username:password"
            char *colon = strchr(decoded, ':');
            if (colon) {
                *colon = '\0';
                return auth_verify_credentials(decoded, colon + 1);
            }
        }
    }

    return false;
}

esp_err_t auth_change_password(const char *old_password, const char *new_password) {
    if (!old_password || !new_password) return ESP_ERR_INVALID_ARG;

    // Verify old password
    uint8_t old_hash[SHA256_LEN];
    hash_password(old_password, old_hash);
    if (memcmp(old_hash, stored_pw_hash, SHA256_LEN) != 0) {
        ESP_LOGW(TAG, "Password change failed: wrong current password");
        return ESP_ERR_INVALID_ARG;
    }

    // Hash and store new password
    hash_password(new_password, stored_pw_hash);

    nvs_handle_t handle;
    esp_err_t err = nvs_open(AUTH_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;

    nvs_set_blob(handle, AUTH_NVS_KEY_PWHASH, stored_pw_hash, SHA256_LEN);
    nvs_commit(handle);
    nvs_close(handle);

    // Invalidate all sessions (force re-login)
    memset(sessions, 0, sizeof(sessions));

    ESP_LOGI(TAG, "Password changed successfully");
    return ESP_OK;
}

const char* auth_get_username(void) {
    return stored_username;
}
