# SIM Module & SD Card Integration Guide
## Modbus IoT Gateway - Enhanced with Cellular Connectivity

**Version:** 2.0
**Date:** November 8, 2025
**Status:** Integration in Progress

---

## 🎯 Integration Overview

This document describes the integration of cellular connectivity (A7670C SIM module) and offline data logging (SD card with RTC) into the existing Modbus IoT Gateway project.

### ✅ What's Been Completed

1. **✅ Updated `web_config.h`**
   - Added `network_mode_t` enum (WIFI/SIM selection)
   - Added `sim_module_config_t` structure with GPIO and APN settings
   - Added `sd_card_config_t` structure with SPI pins and caching options
   - Added `rtc_config_t` structure for DS3231 RTC
   - Updated `system_config_t` to include all new configurations

2. **✅ Created `network_manager` abstraction layer**
   - `network_manager.h` - API for network abstraction
   - `network_manager.c` - Implementation supporting WiFi/SIM switching
   - Unified API: `network_manager_init()`, `network_manager_start()`, etc.
   - Automatic mode detection and connection

3. **✅ Copied and integrated external modules**
   - `a7670c_ppp.c/h` - A7670C SIM module driver with PPP
   - `sd_card_logger.c/h` - SD card offline caching
   - `ds3231_rtc.c/h` - DS3231 RTC for accurate timestamps

4. **✅ Updated `CMakeLists.txt`**
   - Added all new source files to build system

---

## 🏗️ Architecture Changes

### New File Structure

```
modbus_iot_gateway/main/
├── main.c                    # NEEDS UPDATE - integrate network_manager
├── modbus.c/h               # Unchanged - Modbus RTU driver
├── sensor_manager.c/h       # Unchanged - Multi-sensor reading
├── json_templates.c/h       # Unchanged - JSON payload generation
├── web_config.c/h           # NEEDS UPDATE - Add UI for network mode
├── iot_configs.h            # Unchanged - Azure credentials
├── azure_ca_cert.pem        # Unchanged - Azure certificate
│
├── network_manager.c/h      # ✅ NEW - Network abstraction layer
├── a7670c_ppp.c/h          # ✅ NEW - SIM module driver (PPP)
├── sd_card_logger.c/h       # ✅ NEW - Offline data caching
└── ds3231_rtc.c/h          # ✅ NEW - Real-time clock driver
```

### Data Flow

#### WiFi Mode (Default - No Changes)
```
Sensor → Modbus → main.c → network_manager (WiFi) → Azure IoT Hub
```

#### SIM Mode (New)
```
Sensor → Modbus → main.c → network_manager (SIM/PPP) → Azure IoT Hub
                                    ↓ (if offline)
                           SD Card Logger (with RTC timestamp)
                                    ↓ (when online)
                           Replay cached data → Azure IoT Hub
```

---

## 📝 Remaining Integration Steps

### Step 1: Update main.c (High Priority)

**File:** `main.c` (lines ~200-400)

**Changes needed:**

1. **Replace direct WiFi initialization with network_manager:**

```c
// OLD CODE (REMOVE):
// connect_to_wifi();
// or web_config_start_sta_mode();

// NEW CODE (ADD):
#include "network_manager.h"
#include "sd_card_logger.h"
#include "ds3231_rtc.h"

// In app_main() after config_load_from_nvs():
system_config_t *config = get_system_config();

// Initialize RTC if enabled
if (config->rtc_config.enabled) {
    ESP_LOGI(TAG, "🕐 Initializing RTC...");
    ds3231_init(config->rtc_config.sda_pin,
                config->rtc_config.scl_pin,
                config->rtc_config.i2c_num);
}

// Initialize SD card if enabled
if (config->sd_config.enabled) {
    ESP_LOGI(TAG, "💾 Initializing SD card...");
    sd_card_config_t sd_cfg = {
        .mosi_pin = config->sd_config.mosi_pin,
        .miso_pin = config->sd_config.miso_pin,
        .clk_pin = config->sd_config.clk_pin,
        .cs_pin = config->sd_config.cs_pin,
        .spi_host = config->sd_config.spi_host
    };
    sd_card_init(&sd_cfg);
}

// Initialize network manager
ESP_LOGI(TAG, "🌐 Initializing network...");
ESP_ERROR_CHECK(network_manager_init(config));
ESP_ERROR_CHECK(network_manager_start());

// Wait for connection (60 seconds timeout)
esp_err_t ret = network_manager_wait_for_connection(60000);
if (ret == ESP_OK) {
    ESP_LOGI(TAG, "✅ Network connected!");
} else {
    ESP_LOGW(TAG, "⚠️ Network connection timeout - entering offline mode");
}
```

2. **Update telemetry sending function:**

```c
// In send_telemetry() function:
static bool send_telemetry(void) {
    // Check if network is connected
    if (!network_manager_is_connected()) {
        ESP_LOGW(TAG, "⚠️ Network not connected");

        // If SD card enabled, cache the data
        system_config_t *config = get_system_config();
        if (config->sd_config.enabled && config->sd_config.cache_on_failure) {
            ESP_LOGI(TAG, "💾 Caching telemetry to SD card...");
            esp_err_t ret = sd_card_log_telemetry(telemetry_payload);
            if (ret == ESP_OK) {
                ESP_LOGI(TAG, "✅ Telemetry cached successfully");
                return true;  // Consider it success for now
            }
        }
        return false;
    }

    // Normal telemetry send code here (unchanged)
    // ...existing MQTT publish code...

    // After successful send, try to replay cached messages
    system_config_t *config = get_system_config();
    if (config->sd_config.enabled) {
        ESP_LOGI(TAG, "📤 Checking for cached messages...");
        sd_card_replay_cached_messages();
    }

    return true;
}
```

---

### Step 2: Update web_config.c (Medium Priority)

**File:** `web_config.c`

**Changes needed:**

1. **Update NVS save/load functions to handle new fields:**

```c
// In config_save_to_nvs():
// Add after saving WiFi credentials:

// Save network mode
nvs_set_u8(nvs_handle, "net_mode", config->network_mode);

// Save SIM config
if (config->sim_config.enabled) {
    nvs_set_u8(nvs_handle, "sim_en", 1);
    nvs_set_str(nvs_handle, "sim_apn", config->sim_config.apn);
    nvs_set_str(nvs_handle, "sim_user", config->sim_config.apn_user);
    nvs_set_str(nvs_handle, "sim_pass", config->sim_config.apn_pass);
    nvs_set_u8(nvs_handle, "sim_tx", config->sim_config.uart_tx_pin);
    nvs_set_u8(nvs_handle, "sim_rx", config->sim_config.uart_rx_pin);
    nvs_set_u8(nvs_handle, "sim_pwr", config->sim_config.pwr_pin);
    nvs_set_u8(nvs_handle, "sim_rst", config->sim_config.reset_pin);
}

// Save SD card config
if (config->sd_config.enabled) {
    nvs_set_u8(nvs_handle, "sd_en", 1);
    nvs_set_u8(nvs_handle, "sd_cache", config->sd_config.cache_on_failure ? 1 : 0);
    nvs_set_u8(nvs_handle, "sd_mosi", config->sd_config.mosi_pin);
    nvs_set_u8(nvs_handle, "sd_miso", config->sd_config.miso_pin);
    nvs_set_u8(nvs_handle, "sd_clk", config->sd_config.clk_pin);
    nvs_set_u8(nvs_handle, "sd_cs", config->sd_config.cs_pin);
}

// Save RTC config
if (config->rtc_config.enabled) {
    nvs_set_u8(nvs_handle, "rtc_en", 1);
    nvs_set_u8(nvs_handle, "rtc_sda", config->rtc_config.sda_pin);
    nvs_set_u8(nvs_handle, "rtc_scl", config->rtc_config.scl_pin);
}
```

2. **Add HTTP handlers for new configuration endpoints:**

```c
// Add these new endpoint handlers:

// GET /api/network/mode
static esp_err_t network_mode_get_handler(httpd_req_t *req) {
    // Return current network mode (WiFi or SIM)
}

// POST /api/network/mode
static esp_err_t network_mode_set_handler(httpd_req_t *req) {
    // Set network mode from JSON: {"mode": "wifi"} or {"mode": "sim"}
}

// POST /api/sim/config
static esp_err_t sim_config_handler(httpd_req_t *req) {
    // Save SIM module configuration (APN, pins, etc.)
}

// POST /api/sd/config
static esp_err_t sd_config_handler(httpd_req_t *req) {
    // Save SD card configuration
}

// GET /api/sd/status
static esp_err_t sd_status_handler(httpd_req_t *req) {
    // Return SD card status (free space, cached messages count)
}
```

---

### Step 3: Update Web Interface HTML (High Priority)

**File:** `web_config.c` (HTML section around line 500-3000)

**Add new tab for Network Mode:**

```html
<!-- Add to navigation menu -->
<div class="nav-menu">
    <button onclick="showSection('wifi')">WiFi Config</button>
    <button onclick="showSection('network')">🆕 Network Mode</button>
    <button onclick="showSection('sim')">🆕 SIM Config</button>
    <button onclick="showSection('sd')">🆕 SD Card</button>
    <button onclick="showSection('azure')">Azure IoT</button>
    <button onclick="showSection('sensors')">Sensors</button>
</div>

<!-- Add Network Mode selection section -->
<div id="network-section" class="config-section" style="display:none;">
    <h2>🌐 Network Mode Selection</h2>
    <div class="form-group">
        <label>Select Network Mode:</label>
        <div class="radio-group">
            <input type="radio" id="mode-wifi" name="network_mode" value="wifi" checked>
            <label for="mode-wifi">
                📶 WiFi Mode (Default)
                <span class="help-text">Use existing WiFi network for connectivity</span>
            </label>
        </div>
        <div class="radio-group">
            <input type="radio" id="mode-sim" name="network_mode" value="sim">
            <label for="mode-sim">
                📱 SIM Module Mode (Cellular)
                <span class="help-text">Use A7670C 4G/LTE for connectivity</span>
            </label>
        </div>
    </div>
    <button onclick="saveNetworkMode()">Save Network Mode</button>
</div>

<!-- Add SIM Configuration section -->
<div id="sim-section" class="config-section" style="display:none;">
    <h2>📱 SIM Module Configuration</h2>
    <form id="sim-form">
        <div class="form-group">
            <label>APN (Access Point Name):</label>
            <input type="text" id="sim_apn" placeholder="e.g., airteliot, jionet">
            <span class="help-text">Get APN from your cellular provider</span>
        </div>
        <div class="form-group">
            <label>APN Username (optional):</label>
            <input type="text" id="sim_user" placeholder="Usually empty">
        </div>
        <div class="form-group">
            <label>APN Password (optional):</label>
            <input type="password" id="sim_pass" placeholder="Usually empty">
        </div>
        <h3>GPIO Configuration (Advanced)</h3>
        <div class="gpio-grid">
            <div>
                <label>TX Pin:</label>
                <input type="number" id="sim_tx" value="33" min="0" max="39">
            </div>
            <div>
                <label>RX Pin:</label>
                <input type="number" id="sim_rx" value="32" min="0" max="39">
            </div>
            <div>
                <label>Power Pin:</label>
                <input type="number" id="sim_pwr" value="4" min="0" max="39">
            </div>
            <div>
                <label>Reset Pin:</label>
                <input type="number" id="sim_rst" value="15" min="0" max="39">
            </div>
        </div>
        <button type="button" onclick="testSimConnection()">Test SIM Connection</button>
        <button type="submit">Save SIM Config</button>
    </form>
    <div id="sim-status" class="status-box"></div>
</div>

<!-- Add SD Card Configuration section -->
<div id="sd-section" class="config-section" style="display:none;">
    <h2>💾 SD Card Configuration</h2>
    <form id="sd-form">
        <div class="form-group">
            <input type="checkbox" id="sd_enabled">
            <label for="sd_enabled">Enable SD Card Logging</label>
        </div>
        <div class="form-group">
            <input type="checkbox" id="sd_cache">
            <label for="sd_cache">Cache data when network is unavailable</label>
        </div>
        <h3>SPI GPIO Configuration</h3>
        <div class="gpio-grid">
            <div>
                <label>MOSI Pin:</label>
                <input type="number" id="sd_mosi" value="13" min="0" max="39">
            </div>
            <div>
                <label>MISO Pin:</label>
                <input type="number" id="sd_miso" value="12" min="0" max="39">
            </div>
            <div>
                <label>CLK Pin:</label>
                <input type="number" id="sd_clk" value="14" min="0" max="39">
            </div>
            <div>
                <label>CS Pin:</label>
                <input type="number" id="sd_cs" value="5" min="0" max="39">
            </div>
        </div>
        <button type="submit">Save SD Config</button>
    </form>
    <div id="sd-status" class="status-box">
        <h3>SD Card Status</h3>
        <p>Status: <span id="sd-mount-status">Checking...</span></p>
        <p>Free Space: <span id="sd-free-space">---</span></p>
        <p>Cached Messages: <span id="sd-cached-count">---</span></p>
        <button onclick="refreshSDStatus()">Refresh Status</button>
    </div>
</div>

<!-- Add JavaScript functions -->
<script>
function saveNetworkMode() {
    const mode = document.querySelector('input[name="network_mode"]:checked').value;
    fetch('/api/network/mode', {
        method: 'POST',
        headers: {'Content-Type': 'application/json'},
        body: JSON.stringify({mode: mode})
    })
    .then(response => response.json())
    .then(data => {
        if (data.success) {
            alert('Network mode saved! Please reboot for changes to take effect.');
        }
    });
}

function testSimConnection() {
    document.getElementById('sim-status').innerHTML = 'Testing SIM connection...';
    fetch('/api/sim/test')
        .then(response => response.json())
        .then(data => {
            const status = document.getElementById('sim-status');
            if (data.success) {
                status.innerHTML = `
                    ✅ SIM Connection Successful!<br>
                    Signal Quality: ${data.signal_quality}%<br>
                    Network Type: ${data.network_type}<br>
                    Operator: ${data.operator}
                `;
            } else {
                status.innerHTML = `❌ Connection Failed: ${data.error}`;
            }
        });
}

function refreshSDStatus() {
    fetch('/api/sd/status')
        .then(response => response.json())
        .then(data => {
            document.getElementById('sd-mount-status').textContent =
                data.mounted ? '✅ Mounted' : '❌ Not Mounted';
            document.getElementById('sd-free-space').textContent =
                data.free_space_mb + ' MB';
            document.getElementById('sd-cached-count').textContent =
                data.cached_messages;
        });
}

// Auto-refresh SD status every 10 seconds
setInterval(refreshSDStatus, 10000);
</script>

<style>
.radio-group {
    margin: 10px 0;
    padding: 10px;
    border: 1px solid #ccc;
    border-radius: 5px;
}

.radio-group:hover {
    background-color: #f0f0f0;
}

.help-text {
    display: block;
    font-size: 0.9em;
    color: #666;
    margin-left: 25px;
}

.gpio-grid {
    display: grid;
    grid-template-columns: repeat(2, 1fr);
    gap: 10px;
    margin: 15px 0;
}

.status-box {
    margin-top: 20px;
    padding: 15px;
    background-color: #f9f9f9;
    border-radius: 5px;
    border: 1px solid #ddd;
}
</style>
```

---

## 📋 Configuration Defaults

### Default SIM Configuration
```c
.sim_config = {
    .enabled = false,
    .apn = "airteliot",
    .apn_user = "",
    .apn_pass = "",
    .uart_tx_pin = GPIO_NUM_33,
    .uart_rx_pin = GPIO_NUM_32,
    .pwr_pin = GPIO_NUM_4,
    .reset_pin = GPIO_NUM_15,
    .uart_num = UART_NUM_1,
    .uart_baud_rate = 115200
}
```

### Default SD Card Configuration
```c
.sd_config = {
    .enabled = false,
    .cache_on_failure = true,
    .mosi_pin = GPIO_NUM_13,
    .miso_pin = GPIO_NUM_12,
    .clk_pin = GPIO_NUM_14,
    .cs_pin = GPIO_NUM_5,
    .spi_host = SPI2_HOST,
    .max_message_size = 512,
    .min_free_space_mb = 1
}
```

### Default RTC Configuration
```c
.rtc_config = {
    .enabled = false,
    .sda_pin = GPIO_NUM_21,
    .scl_pin = GPIO_NUM_22,
    .i2c_num = I2C_NUM_0
}
```

---

## ⚠️ Important Notes

### GPIO Conflicts to Avoid

| Feature | Default Pins | Conflicts With |
|---------|-------------|----------------|
| **Modbus RS485** | TX:17, RX:16, RTS:18 | N/A - Keep unchanged |
| **SIM Module UART** | TX:33, RX:32 | None (UART1) |
| **SIM Power/Reset** | PWR:4, RST:15 | Avoid using for sensors |
| **SD Card SPI** | MOSI:13, MISO:12, CLK:14, CS:5 | Don't use for other SPI |
| **RTC I2C** | SDA:21, SCL:22 | Don't use for other I2C |

### Memory Considerations

- Total code size increase: ~50-60KB
- RAM usage increase: ~15-20KB
- SD card caching: Uses dynamic allocation
- PPP netif: Adds ~10KB to heap usage

### Boot Time Changes

- **WiFi Mode:** 5-10 seconds (unchanged)
- **SIM Mode:** 20-30 seconds (modem initialization)
- **SD Card:** +1-2 seconds (mounting filesystem)

---

## 🧪 Testing Checklist

### Phase 1: WiFi Mode (No Regression)
- [ ] Build project successfully
- [ ] Flash firmware
- [ ] Boot in WiFi mode (default)
- [ ] Connect to existing WiFi network
- [ ] Send telemetry to Azure IoT Hub
- [ ] Verify all sensors read correctly
- [ ] Verify web interface accessible

### Phase 2: SIM Mode
- [ ] Configure SIM module via web interface
- [ ] Set correct APN for carrier
- [ ] Test SIM connection from web UI
- [ ] Switch to SIM mode and reboot
- [ ] Verify PPP connection established
- [ ] Send telemetry via cellular
- [ ] Check signal strength monitoring

### Phase 3: SD Card Offline Caching
- [ ] Enable SD card in web interface
- [ ] Verify SD card mounted successfully
- [ ] Disconnect network (WiFi or SIM)
- [ ] Verify telemetry cached to SD card
- [ ] Reconnect network
- [ ] Verify cached messages replayed

### Phase 4: RTC Integration
- [ ] Enable RTC via web interface
- [ ] Verify time set from NTP (when online)
- [ ] Disconnect network and power cycle
- [ ] Verify RTC maintains time
- [ ] Verify offline-cached messages have correct timestamps

---

## 📞 Support & Troubleshooting

### Common Issues

**Issue:** SIM module not responding
- Check power supply (needs 2A peak current)
- Verify UART connections (TX→RX, RX→TX)
- Check SIM card inserted correctly
- Try different baud rate (115200 or 9600)

**Issue:** SD card not mounting
- Check SPI wiring
- Format SD card as FAT32
- Use SD card ≤32GB (SDHC)
- Check CS pin not conflicting

**Issue:** Network mode not switching
- Clear NVS: `idf.py erase-flash`
- Reconfigure via web interface
- Check logs: `idf.py monitor`

---

## 🚀 Next Steps

1. **Complete Step 1:** Update `main.c` with network_manager integration
2. **Complete Step 2:** Update `web_config.c` NVS handlers
3. **Complete Step 3:** Add web interface HTML/JS
4. **Test Phase 1:** Verify WiFi mode still works
5. **Test Phase 2:** Configure and test SIM mode
6. **Test Phase 3:** Enable and test SD card caching
7. **Document:** Create user manual for end users

---

**Last Updated:** November 8, 2025
**Integration Status:** 60% Complete
**Ready for:** Main.c modifications and testing
