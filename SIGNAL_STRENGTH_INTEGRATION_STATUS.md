# Signal Strength Integration Status Report

**Date:** November 8, 2025
**Task:** Phase 2 - Signal Strength Telemetry Integration
**Status:** 85% Complete

---

## ✅ COMPLETED WORK

### 1. Header File Updates (100% Complete)

**File:** `main/json_templates.h`

Added network telemetry fields to `json_params_t` structure (Lines 33-36):
```c
typedef struct {
    json_template_type_t type;
    char unit_id[16];
    double scaled_value;
    uint32_t raw_value;
    char timestamp[32];
    int slave_id;
    // Network telemetry fields (NEW)
    int signal_strength;          // Signal strength in dBm (WiFi RSSI or SIM RSSI)
    char network_type[16];        // "WiFi" or "4G"
    char network_quality[16];     // "Excellent", "Good", "Fair", "Poor", "Unknown"
    // ... rest of structure
} json_params_t;
```

Added network_stats_t forward declaration (Lines 51-53):
```c
// Forward declaration for network stats (defined in network_manager.h)
struct network_stats_t;
typedef struct network_stats_t network_stats_t;
```

Updated function signatures (Lines 57-63):
```c
esp_err_t generate_sensor_json(const sensor_config_t* sensor, double scaled_value,
                              uint32_t raw_value, const network_stats_t* net_stats,
                              char* json_buffer, size_t buffer_size);

esp_err_t generate_sensor_json_with_hex(const sensor_config_t* sensor, double scaled_value,
                              uint32_t raw_value, const char* hex_string,
                              const network_stats_t* net_stats,
                              char* json_buffer, size_t buffer_size);
```

---

### 2. Source File Updates (100% Complete)

**File:** `main/json_templates.c`

#### A. Added Include (Line 4):
```c
#include "network_manager.h"
```

#### B. Updated `generate_sensor_json()` Function (Lines 250-334):

**Function Signature:**
```c
esp_err_t generate_sensor_json(const sensor_config_t* sensor, double scaled_value,
                              uint32_t raw_value, const network_stats_t* net_stats,
                              char* json_buffer, size_t buffer_size)
```

**Added Network Stats Handling (After line 280):**
```c
// Add network telemetry data
if (net_stats) {
    params.signal_strength = net_stats->signal_strength;
    strncpy(params.network_type, net_stats->network_type, sizeof(params.network_type) - 1);

    // Determine quality based on signal strength
    if (net_stats->signal_strength >= -60) {
        strncpy(params.network_quality, "Excellent", sizeof(params.network_quality) - 1);
    } else if (net_stats->signal_strength >= -70) {
        strncpy(params.network_quality, "Good", sizeof(params.network_quality) - 1);
    } else if (net_stats->signal_strength >= -80) {
        strncpy(params.network_quality, "Fair", sizeof(params.network_quality) - 1);
    } else {
        strncpy(params.network_quality, "Poor", sizeof(params.network_quality) - 1);
    }
} else {
    // Default values when network stats unavailable
    params.signal_strength = 0;
    strncpy(params.network_type, "Unknown", sizeof(params.network_type) - 1);
    strncpy(params.network_quality, "Unknown", sizeof(params.network_quality) - 1);
}
```

#### C. Updated `generate_sensor_json_with_hex()` Function (Lines 335-396):

**Function Signature:**
```c
esp_err_t generate_sensor_json_with_hex(const sensor_config_t* sensor, double scaled_value,
                              uint32_t raw_value, const char* hex_string,
                              const network_stats_t* net_stats,
                              char* json_buffer, size_t buffer_size)
```

**Same network stats handling code added after timestamp formatting.**

---

### 3. main.c Updates (100% Complete)

**File:** `main/main.c`

#### A. Network Stats Fetching in `create_telemetry_payload()` (Lines 658-670):
```c
static void create_telemetry_payload(char* payload, size_t payload_size) {
    system_config_t *config = get_system_config();

    // Get network statistics for telemetry
    network_stats_t net_stats = {0};
    if (network_manager_is_connected()) {
        network_manager_get_stats(&net_stats);
        ESP_LOGI(TAG, "[NET] Signal: %d dBm, Type: %s",
                 net_stats.signal_strength, net_stats.network_type);
    } else {
        net_stats.signal_strength = 0;
        strncpy(net_stats.network_type, "Offline", sizeof(net_stats.network_type));
    }

    // ... rest of function
}
```

#### B. Updated JSON Generation Calls (Lines 729-747):
```c
// Check if sensor is ENERGY type and has hex string available
if (strcasecmp(matching_sensor->sensor_type, "ENERGY") == 0 &&
    strlen(readings[i].raw_hex) > 0) {
    json_result = generate_sensor_json_with_hex(
        matching_sensor,
        readings[i].value,
        readings[i].raw_value,
        readings[i].raw_hex,
        &net_stats,  // NEW: Pass network stats
        temp_json,
        MAX_JSON_PAYLOAD_SIZE
    );
} else {
    json_result = generate_sensor_json(
        matching_sensor,
        readings[i].value,
        readings[i].raw_value ? readings[i].raw_value : (uint32_t)(readings[i].value * 10000),
        &net_stats,  // NEW: Pass network stats
        temp_json,
        MAX_JSON_PAYLOAD_SIZE
    );
}
```

---

## ⏳ REMAINING WORK (15%)

### Update JSON Template Output (In Progress)

**File:** `main/json_templates.c` - Function `create_json_payload()` (Lines 90-247)

Need to add signal strength fields to the actual JSON output for each sensor type:

#### Required Changes:

**1. JSON_TYPE_FLOW (Lines 109-121):**
```c
// Current:
snprintf(json_buffer, buffer_size,
    "{"
    "\"consumption\":%.2f,"
    "\"created_on\":\"%s\","
    "\"unit_id\":\"%s\""
    "}",
    params->scaled_value,
    params->timestamp,
    params->unit_id);

// Need to change to:
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

**2. JSON_TYPE_LEVEL (Lines 123-135):**
- Add same three fields: signal_strength, network_type, network_quality

**3. JSON_TYPE_RAINGAUGE (Lines 137-149):**
- Add same three fields

**4. JSON_TYPE_BOREWELL (Lines 151-163):**
- Add same three fields

**5. JSON_TYPE_ENERGY (Lines 165-201):**
```c
// Current (line 188):
snprintf(json_buffer, buffer_size,
    "{"
    "\"ene_con_hex\":\"%s\","
    "\"type\":\"ENERGY\","
    "\"created_on_epoch\":%" PRIu32 ","
    "\"slave_id\":%d,"
    "\"meter\":\"%s\""
    "}",
    hex_value,
    epoch_time,
    params->slave_id,
    strlen(params->extra_params.meter_id) > 0 ? params->extra_params.meter_id : params->unit_id);

// Need to add:
snprintf(json_buffer, buffer_size,
    "{"
    "\"ene_con_hex\":\"%s\","
    "\"type\":\"ENERGY\","
    "\"created_on_epoch\":%" PRIu32 ","
    "\"slave_id\":%d,"
    "\"meter\":\"%s\","
    "\"signal_strength\":%d,"
    "\"network_type\":\"%s\","
    "\"network_quality\":\"%s\""
    "}",
    hex_value,
    epoch_time,
    params->slave_id,
    strlen(params->extra_params.meter_id) > 0 ? params->extra_params.meter_id : params->unit_id,
    params->signal_strength,
    params->network_type,
    params->network_quality);
```

**6. JSON_TYPE_QUALITY (Lines 203-238):**
```c
// Current (line 213):
snprintf(json_buffer, buffer_size,
    "{"
    "\"params_data\":{"
    "\"pH\":%.2f,"
    "\"TDS\":%.2f,"
    "\"Temp\":%.2f,"
    "\"HUMIDITY\":%.2f,"
    "\"TSS\":%.2f,"
    "\"BOD\":%.2f,"
    "\"COD\":%.2f"
    "},"
    "\"type\":\"QUALITY\","
    "\"created_on\":\"%s\","
    "\"unit_id\":\"%s\""
    "}",
    ph_value,
    tds_value,
    temp_value,
    humidity_value,
    tss_value,
    bod_value,
    cod_value,
    params->timestamp,
    params->unit_id);

// Need to add:
snprintf(json_buffer, buffer_size,
    "{"
    "\"params_data\":{"
    "\"pH\":%.2f,"
    "\"TDS\":%.2f,"
    "\"Temp\":%.2f,"
    "\"HUMIDITY\":%.2f,"
    "\"TSS\":%.2f,"
    "\"BOD\":%.2f,"
    "\"COD\":%.2f"
    "},"
    "\"type\":\"QUALITY\","
    "\"created_on\":\"%s\","
    "\"unit_id\":\"%s\","
    "\"signal_strength\":%d,"
    "\"network_type\":\"%s\","
    "\"network_quality\":\"%s\""
    "}",
    ph_value,
    tds_value,
    temp_value,
    humidity_value,
    tss_value,
    bod_value,
    cod_value,
    params->timestamp,
    params->unit_id,
    params->signal_strength,
    params->network_type,
    params->network_quality);
```

---

## 📝 MANUAL UPDATE INSTRUCTIONS

Due to OneDrive sync conflicts preventing automated editing, these updates should be made manually:

### Step 1: Open File
```bash
# Open in your preferred editor:
nano /c/Users/chath/OneDrive/Documents/Python\ code/modbus_iot_gateway/main/json_templates.c

# Or use VS Code:
code /c/Users/chath/OneDrive/Documents/Python\ code/modbus_iot_gateway/main/json_templates.c
```

### Step 2: Find Each Case Statement
Search for each `case JSON_TYPE_XXX:` and update the `snprintf` call to include the three new fields.

### Step 3: Pattern to Follow
For each JSON type, add these three lines before the closing `"}",`:
```c
,"\"signal_strength\":%d,"
"\"network_type\":\"%s\","
"\"network_quality\":\"%s\""
```

And add these three parameters at the end of the parameter list:
```c
params->signal_strength,
params->network_type,
params->network_quality
```

### Step 4: Verify Changes
After editing, compile to check for syntax errors:
```bash
cd "/c/Users/chath/OneDrive/Documents/Python code/modbus_iot_gateway"
idf.py build
```

---

## 🧪 TESTING PLAN

Once JSON templates are updated, test with these steps:

### Test 1: Verify Compilation
```bash
cd "/c/Users/chath/OneDrive/Documents/Python code/modbus_iot_gateway"
idf.py fullclean
idf.py build
```

### Test 2: Flash and Monitor
```bash
idf.py -p COM3 flash monitor
```

### Test 3: Check JSON Output
Look for telemetry JSON in logs with format:
```json
{
  "consumption": 1234.56,
  "created_on": "2025-11-08T10:30:00Z",
  "unit_id": "FM001",
  "signal_strength": -73,
  "network_type": "WiFi",
  "network_quality": "Good"
}
```

### Test 4: Verify Different Network Types
- **WiFi Mode:** Should show `"network_type": "WiFi"` with WiFi RSSI
- **SIM Mode:** Should show `"network_type": "4G"` with cellular signal strength
- **Offline:** Should show `"network_type": "Offline"` with `signal_strength: 0`

---

## 📊 SIGNAL STRENGTH QUALITY MAPPING

The code uses these thresholds:

| Signal Strength (dBm) | Quality Level |
|-----------------------|---------------|
| >= -60                | Excellent     |
| -70 to -61            | Good          |
| -80 to -71            | Fair          |
| < -80                 | Poor          |

---

## 📦 BACKUP FILES CREATED

- `json_templates.c.backup` - Initial backup before modifications
- `json_templates.c.bak2` - After first function update
- `json_templates.c.bak3` - After second function update
- `json_templates.c.bak4` - Before JSON payload updates
- `json_templates.c.working` - Clean version with function signatures updated

**Current Working File:** `json_templates.c` (has function signatures updated, needs JSON output updates)

---

## 🎯 SUMMARY

### Completed (85%):
✅ Added network telemetry fields to `json_params_t` structure
✅ Updated `generate_sensor_json()` function signature
✅ Updated `generate_sensor_json_with_hex()` function signature
✅ Added network stats handling logic in both functions
✅ Updated `create_telemetry_payload()` to fetch network stats
✅ Updated all JSON generation call sites in main.c

### Remaining (15%):
⏳ Update 6 JSON template outputs in `create_json_payload()` to include signal strength fields

### Estimated Time to Complete:
**15-20 minutes** of manual editing to update the 6 JSON snprintf statements

---

**Last Updated:** November 8, 2025
**Next Action:** Manually update JSON template outputs in `create_json_payload()` function
