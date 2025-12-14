# Web API Integration Guide

## Overview

This guide shows how to integrate the HTTP API endpoint handlers into the existing `web_config.c` file.

---

## Step 1: Add Required Includes

At the top of `main/web_config.c`, add these includes after the existing includes:

```c
// Existing includes...
#include "web_config.h"
#include "esp_http_server.h"
// ... other existing includes

// NEW: Add these includes
#include "network_manager.h"
#include "sd_card_logger.h"
#include "ds3231_rtc.h"
#include "a7670c_ppp.h"
#include "cJSON.h"
```

---

## Step 2: Copy API Handler Functions

Copy all the API handler functions from `web_api_handlers.c` into `web_config.c`.

**Location:** Add them **before** the `web_config_start_ap_mode()` function (around line 6400).

The functions to copy:
1. `api_network_status_handler()`
2. `api_network_mode_handler()`
3. `api_wifi_config_handler()`
4. `api_wifi_test_handler()`
5. `api_sim_config_handler()`
6. `api_sim_test_handler()`
7. `api_sd_config_handler()`
8. `api_sd_status_handler()`
9. `api_sd_clear_handler()`
10. `api_rtc_config_handler()`
11. `api_rtc_sync_handler()`
12. `api_reboot_operation_handler()`
13. `api_reboot_handler()`

---

## Step 3: Register URI Handlers

Find the `web_config_start_ap_mode()` function (around line 6400).

Inside this function, locate where existing URI handlers are registered. It should look like:

```c
esp_err_t web_config_start_ap_mode(void)
{
    // ... WiFi AP setup code ...

    // Start HTTP server
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.stack_size = 16384;
    config.max_uri_handlers = 32;  // INCREASE THIS if it's lower

    if (httpd_start(&server, &config) == ESP_OK) {
        // Existing handlers
        httpd_register_uri_handler(server, &uri_root);
        httpd_register_uri_handler(server, &uri_save_config);
        // ... more existing handlers ...

        // ========== ADD NEW API HANDLERS HERE ==========

        // Network API
        httpd_uri_t uri_api_network_status = {
            .uri = "/api/network/status",
            .method = HTTP_GET,
            .handler = api_network_status_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &uri_api_network_status);

        httpd_uri_t uri_api_network_mode = {
            .uri = "/api/network/mode",
            .method = HTTP_POST,
            .handler = api_network_mode_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &uri_api_network_mode);

        // WiFi API
        httpd_uri_t uri_api_wifi_config = {
            .uri = "/api/network/wifi",
            .method = HTTP_POST,
            .handler = api_wifi_config_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &uri_api_wifi_config);

        httpd_uri_t uri_api_wifi_test = {
            .uri = "/api/network/wifi/test",
            .method = HTTP_POST,
            .handler = api_wifi_test_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &uri_api_wifi_test);

        // SIM API
        httpd_uri_t uri_api_sim_config = {
            .uri = "/api/network/sim",
            .method = HTTP_POST,
            .handler = api_sim_config_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &uri_api_sim_config);

        httpd_uri_t uri_api_sim_test = {
            .uri = "/api/network/sim/test",
            .method = HTTP_POST,
            .handler = api_sim_test_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &uri_api_sim_test);

        // SD Card API
        httpd_uri_t uri_api_sd_config = {
            .uri = "/api/sd/config",
            .method = HTTP_POST,
            .handler = api_sd_config_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &uri_api_sd_config);

        httpd_uri_t uri_api_sd_status = {
            .uri = "/api/sd/status",
            .method = HTTP_GET,
            .handler = api_sd_status_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &uri_api_sd_status);

        httpd_uri_t uri_api_sd_clear = {
            .uri = "/api/sd/clear",
            .method = HTTP_POST,
            .handler = api_sd_clear_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &uri_api_sd_clear);

        // RTC API
        httpd_uri_t uri_api_rtc_config = {
            .uri = "/api/rtc/config",
            .method = HTTP_POST,
            .handler = api_rtc_config_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &uri_api_rtc_config);

        httpd_uri_t uri_api_rtc_sync = {
            .uri = "/api/rtc/sync",
            .method = HTTP_POST,
            .handler = api_rtc_sync_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &uri_api_rtc_sync);

        // System Control API
        httpd_uri_t uri_api_reboot_operation = {
            .uri = "/api/system/reboot_operation",
            .method = HTTP_POST,
            .handler = api_reboot_operation_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &uri_api_reboot_operation);

        httpd_uri_t uri_api_reboot = {
            .uri = "/api/system/reboot",
            .method = HTTP_POST,
            .handler = api_reboot_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &uri_api_reboot);

        ESP_LOGI(TAG, "HTTP server started - API endpoints registered");
    }

    return ESP_OK;
}
```

---

## Step 4: Update max_uri_handlers

The default `max_uri_handlers` in httpd_config might be too low. Update it:

```c
httpd_config_t config = HTTPD_DEFAULT_CONFIG();
config.stack_size = 16384;
config.max_uri_handlers = 32;  // Increase from default (8) to handle all endpoints
```

---

## Step 5: Add Missing SD Card Functions

The API handlers reference these SD card functions that may not be in the current `sd_card_logger.c`:

```c
// Add to sd_card_logger.c if not present:

esp_err_t sd_card_get_space(uint32_t *total_kb, uint32_t *free_kb)
{
    if (!sd_card_mounted) {
        return ESP_ERR_INVALID_STATE;
    }

    FATFS *fs;
    DWORD fre_clust;

    // Get volume information
    FRESULT res = f_getfree("0:", &fre_clust, &fs);
    if (res != FR_OK) {
        return ESP_FAIL;
    }

    // Calculate total and free space in KB
    *total_kb = ((fs->n_fatent - 2) * fs->csize) / 2;
    *free_kb = (fre_clust * fs->csize) / 2;

    return ESP_OK;
}

int sd_card_get_cached_count(void)
{
    if (!sd_card_mounted) {
        return 0;
    }

    // Count files in cache directory
    DIR dir;
    FILINFO fno;
    int count = 0;

    if (f_opendir(&dir, CACHE_DIR) == FR_OK) {
        while (f_readdir(&dir, &fno) == FR_OK && fno.fname[0] != 0) {
            if (!(fno.fattrib & AM_DIR)) {
                count++;
            }
        }
        f_closedir(&dir);
    }

    return count;
}

esp_err_t sd_card_clear_cache(void)
{
    if (!sd_card_mounted) {
        return ESP_ERR_INVALID_STATE;
    }

    DIR dir;
    FILINFO fno;

    if (f_opendir(&dir, CACHE_DIR) != FR_OK) {
        return ESP_FAIL;
    }

    while (f_readdir(&dir, &fno) == FR_OK && fno.fname[0] != 0) {
        if (!(fno.fattrib & AM_DIR)) {
            char filepath[128];
            snprintf(filepath, sizeof(filepath), "%s/%s", CACHE_DIR, fno.fname);
            f_unlink(filepath);
        }
    }
    f_closedir(&dir);

    ESP_LOGI(TAG, "SD card cache cleared");
    return ESP_OK;
}
```

Add these to `sd_card_logger.h`:

```c
esp_err_t sd_card_get_space(uint32_t *total_kb, uint32_t *free_kb);
int sd_card_get_cached_count(void);
esp_err_t sd_card_clear_cache(void);
```

---

## Step 6: Add Missing A7670C Functions

If these functions are not in `a7670c_ppp.c`, add them:

```c
// Add to a7670c_ppp.c if not present:

esp_err_t a7670c_get_signal_strength(signal_strength_t *signal)
{
    char response[64];

    // Send AT+CSQ command
    esp_err_t ret = a7670c_send_at_command("AT+CSQ", response, sizeof(response), 1000);
    if (ret != ESP_OK) {
        return ret;
    }

    // Parse response: +CSQ: <rssi>,<ber>
    int rssi, ber;
    if (sscanf(response, "+CSQ: %d,%d", &rssi, &ber) == 2) {
        signal->csq = rssi;

        // Convert CSQ to dBm (approximate)
        if (rssi == 0) {
            signal->rssi_dbm = -113;
        } else if (rssi == 1) {
            signal->rssi_dbm = -111;
        } else if (rssi >= 2 && rssi <= 30) {
            signal->rssi_dbm = -109 + (rssi - 2) * 2;
        } else {
            signal->rssi_dbm = 0; // Unknown
        }

        return ESP_OK;
    }

    return ESP_FAIL;
}

esp_err_t a7670c_get_operator(char *operator_name, size_t size)
{
    char response[128];

    // Send AT+COPS? command
    esp_err_t ret = a7670c_send_at_command("AT+COPS?", response, sizeof(response), 1000);
    if (ret != ESP_OK) {
        return ret;
    }

    // Parse response: +COPS: 0,0,"operator_name"
    char *start = strchr(response, '"');
    if (start) {
        start++;
        char *end = strchr(start, '"');
        if (end) {
            size_t len = end - start;
            if (len < size) {
                strncpy(operator_name, start, len);
                operator_name[len] = '\0';
                return ESP_OK;
            }
        }
    }

    return ESP_FAIL;
}
```

Add to `a7670c_ppp.h`:

```c
typedef struct {
    int csq;           // Raw CSQ value (0-31)
    int rssi_dbm;      // Signal strength in dBm
} signal_strength_t;

esp_err_t a7670c_get_signal_strength(signal_strength_t *signal);
esp_err_t a7670c_get_operator(char *operator_name, size_t size);
```

---

## Step 7: Update CMakeLists.txt

Ensure `main/CMakeLists.txt` includes cJSON component:

```cmake
idf_component_register(
    SRCS "network_manager.c" "ds3231_rtc.c" "sd_card_logger.c"
         "a7670c_ppp.c" "main.c" "modbus.c" "web_config.c"
         "sensor_manager.c" "json_templates.c"
    INCLUDE_DIRS "."
    EMBED_FILES "azure_ca_cert.pem"
    REQUIRES json  # ADD THIS LINE if not present
)
```

---

## Testing the API Endpoints

### 1. Test Network Status
```bash
curl http://192.168.4.1/api/network/status
```

Expected response:
```json
{
  "mode": "wifi",
  "connected": true,
  "network_type": "WiFi",
  "signal_strength": -65,
  "quality": "Good"
}
```

### 2. Test Change Network Mode
```bash
curl -X POST http://192.168.4.1/api/network/mode \
  -H "Content-Type: application/json" \
  -d '{"mode":"sim"}'
```

### 3. Test SD Card Status
```bash
curl http://192.168.4.1/api/sd/status
```

### 4. Test SIM Signal
```bash
curl -X POST http://192.168.4.1/api/network/sim/test
```

---

## Common Issues and Fixes

### Issue 1: "undefined reference to cJSON_CreateObject"
**Fix:** Add `json` to REQUIRES in CMakeLists.txt

### Issue 2: "max_uri_handlers exceeded"
**Fix:** Increase `config.max_uri_handlers = 32` in httpd_config

### Issue 3: API returns 404
**Fix:** Check URI registration order, ensure handlers are registered before starting server

### Issue 4: SD card functions not found
**Fix:** Add the missing SD card helper functions to sd_card_logger.c

---

## Next Steps

After integration:

1. **Build and flash** the firmware
2. **Test each API endpoint** individually
3. **Integrate the HTML/CSS/JS** from WEB_UI_NETWORK_MODE.md
4. **Test the complete web interface**

---

**Files to Modify:**
- `main/web_config.c` - Add API handlers and register URIs
- `main/sd_card_logger.c` - Add helper functions
- `main/sd_card_logger.h` - Add function declarations
- `main/a7670c_ppp.c` - Add signal strength functions
- `main/a7670c_ppp.h` - Add signal_strength_t structure
- `main/CMakeLists.txt` - Add json component if needed

**Estimated Time:** 1-2 hours for integration and testing
