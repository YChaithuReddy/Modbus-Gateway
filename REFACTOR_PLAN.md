# Architecture Refactor Plan - Remove network_manager.c

**Date**: 2025-11-09
**Objective**: Remove `network_manager.c` and restore old STA+AP web config logic while keeping all new features

---

## CURRENT ARCHITECTURE (With network_manager.c)

```
Boot → NVS Init → Web Config Init → Network Manager Init → Network Manager Start
                                          ↓
                          (Handles both WiFi and SIM internally)
                                          ↓
                              Wait for connection → Initialize Time
                                          ↓
                                  Start Tasks (Modbus, MQTT, Telemetry)
```

**Problems**:
- Complex network_manager abstraction layer
- WiFi double-initialization issues
- Harder to maintain and debug
- Deviates from working stable code

---

## TARGET ARCHITECTURE (Old Working Pattern + New Features)

```
Boot → NVS Init → Check Config State
         ↓
    ┌────┴────┐
    │         │
FIRST BOOT   HAS CONFIG
    │         │
    ↓         ↓
AP Mode    Read network_mode from NVS
  (Web      ↓
  Config)   ├─ WiFi Mode: esp_wifi_init → esp_wifi_connect
    │       │               ↓
    │       │          Wait for IP → Initialize Time
    │       │               ↓
    │       └─ SIM Mode: a7670c_ppp_init → a7670c_ppp_connect
    │                       ↓
    │                  Wait for IP → Initialize Time
    │                       ↓
    └──────────────────────────────→ Start Tasks (Modbus, MQTT, Telemetry)
                                              ↓
                                      SD/RTC features work in both modes
```

---

## FILES TO MODIFY

### 1. **DELETE**:
- `main/network_manager.c`
- `main/network_manager.h`

### 2. **MODIFY**:
- `main/main.c` - Remove network_manager calls, add direct WiFi/SIM logic
- `main/web_config.c` - Already has UI, just needs to stay as primary entry
- `main/CMakeLists.txt` - Remove network_manager.c from SRCS

### 3. **KEEP UNCHANGED**:
- `main/a7670c_ppp.c` - Direct SIM module driver
- `main/sd_card_logger.c` - SD card logging
- `main/ds3231_rtc.c` - RTC driver
- `main/modbus.c` - Modbus communication
- `main/json_templates.c` - Telemetry formatting

---

## DETAILED CHANGES NEEDED IN main.c

### Change #1: Remove network_manager include
```c
// BEFORE:
#include "network_manager.h"

// AFTER:
// (removed - use direct WiFi and SIM APIs)
```

### Change #2: Add direct includes
```c
// ADD:
#include "esp_wifi.h"
#include "esp_netif.h"
#include "a7670c_ppp.h"
```

### Change #3: Replace network_manager_is_connected()
```c
// BEFORE:
if (network_manager_is_connected()) {
    // do something
}

// AFTER - For WiFi mode:
wifi_ap_record_t ap_info;
bool wifi_connected = (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK);
if (wifi_connected) {
    // do something
}

// OR - For SIM mode:
bool sim_connected = a7670c_is_connected();
if (sim_connected) {
    // do something
}

// OR - Generic check:
bool network_connected = false;
if (config->network_mode == NETWORK_MODE_WIFI) {
    wifi_ap_record_t ap_info;
    network_connected = (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK);
} else {
    network_connected = a7670c_is_connected();
}
```

### Change #4: Replace network_manager_init() and network_manager_start()
```c
// BEFORE (lines 1663-1700):
ret = network_manager_init(config);
if (ret != ESP_OK) {
    ESP_LOGW(TAG, "[NET] Network manager init failed - continuing in offline mode");
} else {
    ret = network_manager_start();
    // ...
}

// AFTER:
if (config->network_mode == NETWORK_MODE_WIFI) {
    // WiFi Mode - Direct initialization
    ESP_LOGI(TAG, "[WIFI] Starting WiFi STA mode...");

    esp_netif_create_default_wifi_sta();

    wifi_config_t wifi_config = {
        .sta = {
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
            .pmf_cfg = {
                .capable = true,
                .required = false
            },
        },
    };

    strncpy((char *)wifi_config.sta.ssid, config->wifi_ssid, sizeof(wifi_config.sta.ssid));
    strncpy((char *)wifi_config.sta.password, config->wifi_password, sizeof(wifi_config.sta.password));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    // Wait for connection with timeout
    ESP_LOGI(TAG, "[WIFI] Waiting for connection...");
    int retry = 0;
    while (retry < 30) {
        wifi_ap_record_t ap_info;
        if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
            ESP_LOGI(TAG, "[WIFI] ✅ Connected to %s", config->wifi_ssid);
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
        retry++;
    }

    if (retry >= 30) {
        ESP_LOGW(TAG, "[WIFI] ⚠️ Connection timeout - entering offline mode");
    }

} else if (config->network_mode == NETWORK_MODE_SIM) {
    // SIM Mode - Direct A7670C initialization
    ESP_LOGI(TAG, "[SIM] Starting SIM module (A7670C)...");

    ppp_config_t ppp_config = {
        .uart_num = config->sim_config.uart_num,
        .tx_pin = config->sim_config.uart_tx_pin,
        .rx_pin = config->sim_config.uart_rx_pin,
        .pwr_pin = config->sim_config.pwr_pin,
        .reset_pin = config->sim_config.reset_pin,
        .baud_rate = config->sim_config.uart_baud_rate,
        .apn = config->sim_config.apn,
        .user = config->sim_config.apn_user,
        .pass = config->sim_config.apn_pass,
    };

    ret = a7670c_ppp_init(&ppp_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "[SIM] ❌ Failed to initialize A7670C: %s", esp_err_to_name(ret));
        ESP_LOGW(TAG, "[SIM] Entering offline mode");
    } else {
        ret = a7670c_ppp_connect();
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "[SIM] ❌ Failed to connect PPP: %s", esp_err_to_name(ret));
            ESP_LOGW(TAG, "[SIM] Entering offline mode");
        } else {
            // Wait for PPP connection with timeout
            ESP_LOGI(TAG, "[SIM] Waiting for PPP connection...");
            int retry = 0;
            while (retry < 60) {
                if (a7670c_is_connected()) {
                    ESP_LOGI(TAG, "[SIM] ✅ PPP connection established");
                    break;
                }
                vTaskDelay(pdMS_TO_TICKS(1000));
                retry++;
            }

            if (retry >= 60) {
                ESP_LOGW(TAG, "[SIM] ⚠️ PPP connection timeout - entering offline mode");
            }
        }
    }
}

// Check if network is connected (either mode)
bool network_connected = false;
if (config->network_mode == NETWORK_MODE_WIFI) {
    wifi_ap_record_t ap_info;
    network_connected = (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK);
} else {
    network_connected = a7670c_is_connected();
}

// Only initialize time if network connected
if (network_connected) {
    initialize_time();
} else {
    ESP_LOGW(TAG, "[TIME] ⚠️ Skipping SNTP - network not available");
}
```

### Change #5: Replace network_manager_get_stats()
```c
// BEFORE (line 704, 1684):
network_stats_t net_stats;
network_manager_get_stats(&net_stats);

// AFTER:
network_stats_t net_stats = {0};

if (config->network_mode == NETWORK_MODE_WIFI) {
    wifi_ap_record_t ap_info;
    if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
        net_stats.signal_strength = ap_info.rssi;
        strncpy(net_stats.network_type, "WiFi", sizeof(net_stats.network_type));

        // Determine quality
        if (ap_info.rssi >= -60) {
            strncpy(net_stats.network_quality, "Excellent", sizeof(net_stats.network_quality));
        } else if (ap_info.rssi >= -70) {
            strncpy(net_stats.network_quality, "Good", sizeof(net_stats.network_quality));
        } else if (ap_info.rssi >= -80) {
            strncpy(net_stats.network_quality, "Fair", sizeof(net_stats.network_quality));
        } else {
            strncpy(net_stats.network_quality, "Poor", sizeof(net_stats.network_quality));
        }
    }
} else {
    signal_strength_t signal;
    if (a7670c_get_signal_strength(&signal) == ESP_OK) {
        net_stats.signal_strength = signal.rssi_dbm;
        strncpy(net_stats.network_type, "4G", sizeof(net_stats.network_type));
        strncpy(net_stats.operator_name, signal.operator_name, sizeof(net_stats.operator_name));

        // Determine quality
        if (signal.rssi_dbm >= -70) {
            strncpy(net_stats.network_quality, "Excellent", sizeof(net_stats.network_quality));
        } else if (signal.rssi_dbm >= -85) {
            strncpy(net_stats.network_quality, "Good", sizeof(net_stats.network_quality));
        } else if (signal.rssi_dbm >= -100) {
            strncpy(net_stats.network_quality, "Fair", sizeof(net_stats.network_quality));
        } else {
            strncpy(net_stats.network_quality, "Poor", sizeof(net_stats.network_quality));
        }
    }
}
```

---

## MODIFICATIONS TO web_config.c

### Keep All New UI (Already Complete):
- ✅ Network Mode selector
- ✅ SIM configuration panel
- ✅ SD card configuration panel
- ✅ RTC configuration panel
- ✅ All JavaScript functions
- ✅ All REST API endpoints

### Remove Only:
```c
// BEFORE:
#include "network_manager.h"

// AFTER:
// (removed)
```

### Update API handlers to use direct calls:
```c
// In api_sim_test_handler():
// Already calls a7670c_get_signal_strength() directly ✅

// In other handlers:
// No changes needed - they already use direct module APIs ✅
```

---

## NEW HELPER FUNCTION TO ADD IN main.c

```c
// Add this helper function to check network connectivity
static bool is_network_connected(void) {
    system_config_t *config = get_system_config();
    if (!config) return false;

    if (config->network_mode == NETWORK_MODE_WIFI) {
        wifi_ap_record_t ap_info;
        return (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK);
    } else {
        return a7670c_is_connected();
    }
}

// Use this instead of network_manager_is_connected() everywhere
```

---

## INITIALIZATION SEQUENCE (OLD WORKING PATTERN)

### Restored Flow:
```
1. NVS init
2. Web config init (always initializes WiFi stack for AP mode)
3. RTC init (optional)
4. SD card init (optional)
5. GPIO config
6. Modbus init
7. Queue create
8. Check network_mode from NVS:
   ├─ WiFi: Direct WiFi STA connect
   └─ SIM: Direct A7670C PPP connect
9. Wait for connection (with timeout, graceful failure)
10. Initialize time IF network connected
11. RTC sync IF network connected
12. Create tasks (Modbus, MQTT, Telemetry)
```

**Key Difference from Current Code**:
- No network_manager layer
- Direct WiFi or SIM initialization based on config
- Simpler, more predictable flow
- Matches original working code architecture

---

## BENEFITS OF THIS REFACTOR

1. ✅ **Simpler Architecture** - Remove abstraction layer
2. ✅ **Easier Debugging** - Direct API calls, clear flow
3. ✅ **Matches Working Code** - Returns to proven pattern
4. ✅ **Better Control** - Fine-tune WiFi vs SIM behavior
5. ✅ **Keeps All Features** - SIM, SD, RTC, web UI all functional
6. ✅ **Production Ready** - Based on stable codebase

---

## TESTING PLAN

### Phase 1: Compilation
- [ ] Remove network_manager files
- [ ] Update includes in main.c and web_config.c
- [ ] Fix all compilation errors
- [ ] Verify successful build

### Phase 2: WiFi Mode
- [ ] Boot device, configure WiFi via web UI
- [ ] Reboot, verify WiFi connection
- [ ] Test MQTT telemetry
- [ ] Test SD caching when offline
- [ ] Test RTC timekeeping

### Phase 3: SIM Mode
- [ ] Configure SIM mode via web UI
- [ ] Reboot, verify PPP connection
- [ ] Test MQTT telemetry over 4G
- [ ] Test SD caching when offline
- [ ] Test signal monitoring

### Phase 4: Mode Switching
- [ ] Switch from WiFi to SIM via web UI
- [ ] Switch from SIM to WiFi via web UI
- [ ] Verify configurations persist
- [ ] Verify smooth transitions

---

## RISK MITIGATION

### Potential Issues:
1. ⚠️ WiFi event handlers - May need adjustment
2. ⚠️ PPP event handlers - May need direct registration
3. ⚠️ JSON telemetry - Update signal strength source
4. ⚠️ Web UI API calls - Verify they still work

### Mitigation:
- Keep incremental backups
- Test each change before proceeding
- Maintain compatibility with existing NVS data
- Preserve all working web UI code

---

## IMPLEMENTATION ORDER

1. **Remove Files** (DELETE)
   - network_manager.c
   - network_manager.h

2. **Update Includes** (main.c, web_config.c)
   - Remove network_manager.h
   - Add direct WiFi/PPP includes if needed

3. **Add Helper Function** (main.c)
   - `is_network_connected()`

4. **Replace Calls** (main.c)
   - Replace network_manager_init/start with direct code
   - Replace network_manager_is_connected with helper
   - Replace network_manager_get_stats with direct calls

5. **Test Compilation**
   - Fix any remaining errors

6. **Test Functionality**
   - WiFi mode
   - SIM mode
   - All features

---

**Status**: ⏳ READY TO IMPLEMENT
**Estimated Time**: 2-3 hours
**Risk Level**: Medium (significant refactor, but well-planned)
**Expected Outcome**: ✅ Cleaner, simpler, more maintainable code

---

**Next Steps**:
Would you like me to proceed with this refactor now?
