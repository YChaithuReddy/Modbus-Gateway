# Quick Start - Integration Checklist

## 🚀 Fast Track to Complete Integration

Follow these steps in order to complete the remaining 15% of the project.

---

## Step 1: Build Test (5 minutes)

```bash
cd "C:\Users\chath\OneDrive\Documents\Python code\modbus_iot_gateway"
idf.py fullclean
idf.py build
```

**Expected:** Build completes successfully
**If it fails:** Check error messages, verify all files are present

---

## Step 2: Add Required Includes to web_config.c (2 minutes)

Open `main/web_config.c` and add these includes at the top:

```c
#include "network_manager.h"
#include "sd_card_logger.h"
#include "ds3231_rtc.h"
#include "a7670c_ppp.h"
#include "cJSON.h"
```

---

## Step 3: Copy API Handlers (10 minutes)

1. Open `web_api_handlers.c`
2. Copy all 13 API handler functions
3. Paste them into `main/web_config.c` before the `web_config_start_ap_mode()` function

---

## Step 4: Register URI Handlers (15 minutes)

In `web_config_start_ap_mode()` function, find where URIs are registered.

Add this code block:

```c
// Increase max handlers
httpd_config_t config = HTTPD_DEFAULT_CONFIG();
config.stack_size = 16384;
config.max_uri_handlers = 32;  // INCREASE THIS

// ... after existing httpd_register_uri_handler calls ...

// Network API
httpd_uri_t uri_api_network_status = {"/api/network/status", HTTP_GET, api_network_status_handler, NULL};
httpd_register_uri_handler(server, &uri_api_network_status);

httpd_uri_t uri_api_network_mode = {"/api/network/mode", HTTP_POST, api_network_mode_handler, NULL};
httpd_register_uri_handler(server, &uri_api_network_mode);

// WiFi API
httpd_uri_t uri_api_wifi_config = {"/api/network/wifi", HTTP_POST, api_wifi_config_handler, NULL};
httpd_register_uri_handler(server, &uri_api_wifi_config);

httpd_uri_t uri_api_wifi_test = {"/api/network/wifi/test", HTTP_POST, api_wifi_test_handler, NULL};
httpd_register_uri_handler(server, &uri_api_wifi_test);

// SIM API
httpd_uri_t uri_api_sim_config = {"/api/network/sim", HTTP_POST, api_sim_config_handler, NULL};
httpd_register_uri_handler(server, &uri_api_sim_config);

httpd_uri_t uri_api_sim_test = {"/api/network/sim/test", HTTP_POST, api_sim_test_handler, NULL};
httpd_register_uri_handler(server, &uri_api_sim_test);

// SD Card API
httpd_uri_t uri_api_sd_config = {"/api/sd/config", HTTP_POST, api_sd_config_handler, NULL};
httpd_register_uri_handler(server, &uri_api_sd_config);

httpd_uri_t uri_api_sd_status = {"/api/sd/status", HTTP_GET, api_sd_status_handler, NULL};
httpd_register_uri_handler(server, &uri_api_sd_status);

httpd_uri_t uri_api_sd_clear = {"/api/sd/clear", HTTP_POST, api_sd_clear_handler, NULL};
httpd_register_uri_handler(server, &uri_api_sd_clear);

// RTC API
httpd_uri_t uri_api_rtc_config = {"/api/rtc/config", HTTP_POST, api_rtc_config_handler, NULL};
httpd_register_uri_handler(server, &uri_api_rtc_config);

httpd_uri_t uri_api_rtc_sync = {"/api/rtc/sync", HTTP_POST, api_rtc_sync_handler, NULL};
httpd_register_uri_handler(server, &uri_api_rtc_sync);

// System API
httpd_uri_t uri_api_reboot_operation = {"/api/system/reboot_operation", HTTP_POST, api_reboot_operation_handler, NULL};
httpd_register_uri_handler(server, &uri_api_reboot_operation);

httpd_uri_t uri_api_reboot = {"/api/system/reboot", HTTP_POST, api_reboot_handler, NULL};
httpd_register_uri_handler(server, &uri_api_reboot);
```

---

## Step 5: Add SD Card Helper Functions (20 minutes)

### 5a. Add to sd_card_logger.h:

```c
esp_err_t sd_card_get_space(uint32_t *total_kb, uint32_t *free_kb);
int sd_card_get_cached_count(void);
esp_err_t sd_card_clear_cache(void);
```

### 5b. Add to sd_card_logger.c:

Copy the implementations from `WEB_API_INTEGRATION_GUIDE.md` Step 5.

---

## Step 6: Add SIM Helper Functions (15 minutes)

### 6a. Add to a7670c_ppp.h:

```c
typedef struct {
    int csq;
    int rssi_dbm;
} signal_strength_t;

esp_err_t a7670c_get_signal_strength(signal_strength_t *signal);
esp_err_t a7670c_get_operator(char *operator_name, size_t size);
```

### 6b. Add to a7670c_ppp.c:

Copy the implementations from `WEB_API_INTEGRATION_GUIDE.md` Step 6.

---

## Step 7: Update CMakeLists.txt (2 minutes)

Open `main/CMakeLists.txt` and ensure `json` is in REQUIRES:

```cmake
idf_component_register(
    SRCS "network_manager.c" "ds3231_rtc.c" "sd_card_logger.c"
         "a7670c_ppp.c" "main.c" "modbus.c" "web_config.c"
         "sensor_manager.c" "json_templates.c"
    INCLUDE_DIRS "."
    EMBED_FILES "azure_ca_cert.pem"
    REQUIRES json
)
```

---

## Step 8: Build and Flash (10 minutes)

```bash
idf.py build
idf.py -p COM3 flash monitor
```

Replace `COM3` with your actual port.

---

## Step 9: Test API Endpoints (15 minutes)

1. Enter config mode (pull GPIO 34 low)
2. Connect to "ModbusIoT-Config" WiFi
3. Test API endpoints:

```bash
# Test network status
curl http://192.168.4.1/api/network/status

# Test SD status
curl http://192.168.4.1/api/sd/status
```

Expected: JSON responses with status data

---

## Step 10: Add Web UI (Optional - 2 hours)

If you want the full web interface:

1. Find WiFi configuration section in `main/web_config.c` (line ~1115)
2. Replace with Network Mode HTML from `WEB_UI_NETWORK_MODE.md`
3. Add CSS styles to `<style>` section
4. Add JavaScript functions to `<script>` section

---

## ✅ Success Criteria

After completing steps 1-9:

- ✅ Build completes without errors
- ✅ Device boots normally
- ✅ WiFi mode works (no regression)
- ✅ API endpoints respond with JSON
- ✅ Configuration saves to NVS

---

## 🔍 Troubleshooting

### Build fails with "undefined reference to cJSON_xxx"
**Fix:** Add `REQUIRES json` to CMakeLists.txt (Step 7)

### Build fails with "undefined reference to api_xxx_handler"
**Fix:** Ensure API handler functions copied to web_config.c (Step 3)

### API returns 404
**Fix:** Check URI registration in Step 4, verify handlers are registered

### Device crashes on boot
**Fix:** Check serial monitor for error messages, verify GPIO pin conflicts

---

## 📊 Time Estimates

| Step | Time | Difficulty |
|------|------|------------|
| 1. Build Test | 5 min | Easy |
| 2. Add Includes | 2 min | Easy |
| 3. Copy Handlers | 10 min | Easy |
| 4. Register URIs | 15 min | Medium |
| 5. SD Functions | 20 min | Medium |
| 6. SIM Functions | 15 min | Medium |
| 7. CMakeLists | 2 min | Easy |
| 8. Build & Flash | 10 min | Easy |
| 9. Test APIs | 15 min | Easy |
| **Total** | **1.5 hours** | **Medium** |

Step 10 (Web UI) is optional and adds 2 hours.

---

## 📚 Reference Documents

- **Detailed Integration:** `WEB_API_INTEGRATION_GUIDE.md`
- **Overall Status:** `FINAL_SESSION_SUMMARY.md`
- **Web UI Design:** `WEB_UI_NETWORK_MODE.md`
- **Build Testing:** `BUILD_AND_TEST_GUIDE.md`

---

## 🎯 After Integration

Once all steps are complete:

1. **Test WiFi mode thoroughly** (regression test)
2. **Enable SD card** in config, test offline caching
3. **Insert SIM card** and test 4G connectivity
4. **Enable RTC** and test time synchronization
5. **Deploy to production** when satisfied

---

**Good luck! You're 85% done - just 1.5 hours to completion!** 🚀
