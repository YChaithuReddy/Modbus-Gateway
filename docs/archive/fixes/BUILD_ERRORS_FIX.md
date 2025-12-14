# Build Errors - Quick Fix Guide

## Summary of Errors

The build found several issues that need to be fixed:

1. ❌ Missing `config.h` in copied files (ds3231_rtc.c, sd_card_logger.c, a7670c_ppp.c)
2. ❌ `network_stats_t` type conflict between headers
3. ❌ Missing SPI/I2C constants in web_config.c
4. ❌ JSON format string errors (too many arguments)
5. ❌ Missing function declarations/implementations
6. ❌ Function signature mismatches

---

## Fix 1: Remove config.h includes (3 files)

The copied files reference a `config.h` that doesn't exist in this project.

### File: `main/ds3231_rtc.c` (line 7)
**Remove this line:**
```c
#include "config.h"
```

### File: `main/sd_card_logger.c` (line 13)
**Remove this line:**
```c
#include "config.h"
```

### File: `main/a7670c_ppp.c` (line 16)
**Remove this line:**
```c
#include "config.h"
```

---

## Fix 2: Fix network_stats_t conflict

The type is defined in both `json_templates.h` (forward declaration) and `network_manager.h` (full definition).

### File: `main/json_templates.h` (lines 51-53)

**Replace:**
```c
// Forward declaration for network stats (defined in network_manager.h)
struct network_stats_t;
typedef struct network_stats_t network_stats_t;
```

**With:**
```c
// Forward declaration for network stats (defined in network_manager.h)
typedef struct network_stats_t network_stats_t;
```

**OR better - just remove the forward declaration entirely** since network_manager.h will be included.

---

## Fix 3: Add missing includes to web_config.c

### File: `main/web_config.c`

**Add these includes at the top (around line 10):**
```c
#include "driver/spi_common.h"
#include "driver/i2c.h"
```

---

## Fix 4: Fix network_manager.c PPP config issues

### File: `main/network_manager.c` (lines 155-157)

The `ppp_config_t` structure doesn't have `apn_user` and `apn_pass` fields in the A7670C driver.

**Replace:**
```c
strncpy(ppp_config.apn, network_config->sim_config.apn, sizeof(ppp_config.apn));
strncpy(ppp_config.apn_user, network_config->sim_config.apn_user, sizeof(ppp_config.apn_user));
strncpy(ppp_config.apn_pass, network_config->sim_config.apn_pass, sizeof(ppp_config.apn_pass));
```

**With:**
```c
// Note: ppp_config.apn is const char*, so just assign pointer
ppp_config.apn = network_config->sim_config.apn;
// APN user/pass are not supported by the current PPP config
// These would need to be added to a7670c_ppp.h if needed
```

---

## Fix 5: Fix network_manager.c signal strength function name

### File: `main/network_manager.c` (line 288)

**Replace:**
```c
if (a7670c_ppp_get_signal_strength(&sim_signal) == ESP_OK) {
    stats->signal_strength = sim_signal.quality;
```

**With:**
```c
if (a7670c_get_signal_strength(&sim_signal) == ESP_OK) {
    stats->signal_strength = sim_signal.rssi_dbm;
```

---

## Fix 6: Fix duplicate variable in main.c

### File: `main/main.c` (line 1410)

**Remove this line** (it's a duplicate):
```c
system_config_t* config = get_system_config();
```

The variable `config` is already declared at line 1361.

---

## Fix 7: Fix ds3231_init call in main.c

### File: `main/main.c` (lines 1539-1541)

The `ds3231_init()` function doesn't take parameters.

**Replace:**
```c
esp_err_t rtc_ret = ds3231_init(config->rtc_config.sda_pin,
                                 config->rtc_config.scl_pin,
                                 config->rtc_config.i2c_num);
```

**With:**
```c
esp_err_t rtc_ret = ds3231_init();
```

**Note:** The RTC pins are hardcoded in ds3231_rtc.c. If you need configurable pins, the function needs to be modified.

---

## Fix 8: Fix sd_card_init call in main.c

### File: `main/main.c` (line 1562)

The `sd_card_init()` function doesn't take parameters.

**Replace:**
```c
esp_err_t sd_ret = sd_card_init(&sd_cfg);
```

**With:**
```c
esp_err_t sd_ret = sd_card_init();
```

**Note:** SD card pins are hardcoded in sd_card_logger.c. If you need configurable pins, the function needs to be modified.

---

## Fix 9: JSON format string errors (CRITICAL)

The JSON templates have parameters but no format specifiers.

### File: `main/json_templates.c`

This is the issue we tried to fix earlier. You need to manually add the format specifiers.

**For each JSON type (FLOW, LEVEL, RAINGAUGE, BOREWELL, ENERGY, QUALITY), add these fields:**

Before closing `"}",` add:
```c
","signal_strength":%d,"network_type":"%s","network_quality":"%s"
```

And at the end of parameters, add:
```c
params->signal_strength,
params->network_type,
params->network_quality
```

**Example for FLOW (lines 112-120):**

**Current:**
```c
snprintf(json_buffer, buffer_size,
    "{"
    "\"consumption\":%.2f,"
    "\"created_on\":\"%s\","
    "\"unit_id\":\"%s\""
    "}",
    params->scaled_value,
    params->timestamp,
    params->unit_id);
```

**Fixed:**
```c
snprintf(json_buffer, buffer_size,
    "{"
    "\"consumption\":%.2f,"
    "\"created_on\":\"%s\","
    "\"unit_id\":\"%s\","
    "\"signal_strength\":%d,"
    "\"network_type\":\"%s\","
    "\"network_quality\":\"%s\""
    "}",
    params->scaled_value,
    params->timestamp,
    params->unit_id,
    params->signal_strength,
    params->network_type,
    params->network_quality);
```

**Repeat for:** LEVEL (line 129), RAINGAUGE (line 146), BOREWELL (line 163), ENERGY (line 201), QUALITY (line 229)

---

## Quick Fix Script

I'll create a script to automate most of these fixes.

---

## Priority Order

1. **HIGH**: Fix config.h includes (3 files)
2. **HIGH**: Fix web_config.c includes
3. **HIGH**: Fix main.c function calls (ds3231_init, sd_card_init)
4. **HIGH**: Fix duplicate variable in main.c
5. **MEDIUM**: Fix network_stats_t conflict
6. **MEDIUM**: Fix network_manager.c issues
7. **LOW**: Fix JSON format strings (can be done later - won't affect WiFi mode)

---

## After Fixes - Test Build

```bash
idf.py build
```

If build succeeds, the WiFi-only mode should work!
