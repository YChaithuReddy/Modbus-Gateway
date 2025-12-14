# Missing Web UI Features - Complete Integration Guide

**Status**: Your firmware has SIM/SD/RTC fully integrated in backend, but **Web UI is missing 60%** of configuration panels.

---

## CURRENT SITUATION

### ✅ What's Working (Backend - 95%)
- Network manager with WiFi/SIM switching ✅
- SD card caching and storage ✅
- RTC timekeeping ✅
- JSON telemetry with signal strength ✅
- NVS configuration persistence ✅

### ❌ What's Missing (Frontend - 40%)
- ❌ Network Mode selector (WiFi vs SIM radio buttons)
- ❌ SIM Configuration panel (APN, credentials, pins)
- ❌ SD Card Configuration panel (pins, caching options)
- ❌ RTC Configuration panel (I2C pins, sync options)
- ❌ 10 REST API endpoints for new features
- ❌ JavaScript to show/hide panels based on selection

---

## GAP #1: NETWORK MODE SELECTOR (Missing)

**Location**: Add to `web_config.c` around line 1100 (in `generate_config_html`)

**Missing HTML** (~80 lines):

```html
<!-- Network Mode Selection (MISSING IN CURRENT CODE) -->
<div class="section">
    <h3>Network Configuration</h3>
    <p class="helper">Choose your network connectivity method</p>

    <div class="form-group">
        <label for="network_mode">Network Mode:</label>
        <div class="radio-group">
            <label class="radio-label">
                <input type="radio" name="network_mode" value="0" id="mode_wifi" onchange="toggleNetworkMode()">
                <span>WiFi</span>
            </label>
            <label class="radio-label">
                <input type="radio" name="network_mode" value="1" id="mode_sim" onchange="toggleNetworkMode()">
                <span>SIM Module (4G)</span>
            </label>
        </div>
    </div>
</div>

<!-- WiFi Configuration Panel (EXISTS but needs conditional display) -->
<div id="wifi_panel" class="section">
    <h3>WiFi Settings</h3>
    <!-- Existing WiFi HTML stays here -->
</div>
```

---

## GAP #2: SIM CONFIGURATION PANEL (Missing)

**Location**: Add after WiFi panel in `web_config.c`

**Missing HTML** (~120 lines):

```html
<!-- SIM Configuration Panel (MISSING) -->
<div id="sim_panel" class="section" style="display:none;">
    <h3>SIM Module Configuration (A7670C)</h3>
    <p class="helper">Configure 4G cellular connectivity</p>

    <div class="form-group">
        <label for="sim_apn">APN:</label>
        <input type="text" id="sim_apn" name="sim_apn" placeholder="internet" maxlength="63">
        <small>Your carrier's Access Point Name (e.g., "internet", "hologram")</small>
    </div>

    <div class="form-group">
        <label for="sim_apn_user">APN Username (optional):</label>
        <input type="text" id="sim_apn_user" name="sim_apn_user" placeholder="username" maxlength="63">
    </div>

    <div class="form-group">
        <label for="sim_apn_pass">APN Password (optional):</label>
        <input type="password" id="sim_apn_pass" name="sim_apn_pass" maxlength="63">
    </div>

    <div class="form-group">
        <label for="sim_uart">UART Port:</label>
        <select id="sim_uart" name="sim_uart">
            <option value="1">UART1</option>
            <option value="2" selected>UART2</option>
        </select>
    </div>

    <div class="form-row">
        <div class="form-group">
            <label for="sim_tx_pin">TX Pin:</label>
            <input type="number" id="sim_tx_pin" name="sim_tx_pin" min="0" max="39" value="17">
        </div>
        <div class="form-group">
            <label for="sim_rx_pin">RX Pin:</label>
            <input type="number" id="sim_rx_pin" name="sim_rx_pin" min="0" max="39" value="16">
        </div>
    </div>

    <div class="form-row">
        <div class="form-group">
            <label for="sim_pwr_pin">Power Pin:</label>
            <input type="number" id="sim_pwr_pin" name="sim_pwr_pin" min="0" max="39" value="4">
        </div>
        <div class="form-group">
            <label for="sim_reset_pin">Reset Pin:</label>
            <input type="number" id="sim_reset_pin" name="sim_reset_pin" min="-1" max="39" value="-1">
            <small>-1 to disable</small>
        </div>
    </div>

    <div class="form-group">
        <label for="sim_baud">Baud Rate:</label>
        <select id="sim_baud" name="sim_baud">
            <option value="9600">9600</option>
            <option value="19200">19200</option>
            <option value="38400">38400</option>
            <option value="57600">57600</option>
            <option value="115200" selected>115200</option>
        </select>
    </div>

    <button type="button" class="btn-test" onclick="testSIMConnection()">
        Test SIM Connection
    </button>
    <div id="sim_test_result" class="test-result"></div>
</div>
```

---

## GAP #3: SD CARD CONFIGURATION PANEL (Missing)

**Location**: Add after SIM panel in `web_config.c`

**Missing HTML** (~130 lines):

```html
<!-- SD Card Configuration Panel (MISSING) -->
<div id="sd_panel" class="section">
    <h3>SD Card Configuration</h3>
    <p class="helper">Enable offline message caching during network outages</p>

    <div class="form-group">
        <label class="checkbox-label">
            <input type="checkbox" id="sd_enabled" name="sd_enabled" onchange="toggleSDOptions()">
            <span>Enable SD Card</span>
        </label>
    </div>

    <div id="sd_options" style="display:none;">
        <div class="form-group">
            <label class="checkbox-label">
                <input type="checkbox" id="sd_cache_on_failure" name="sd_cache_on_failure" checked>
                <span>Cache Messages When Network Unavailable</span>
            </label>
        </div>

        <h4>SPI Pin Configuration</h4>
        <div class="form-row">
            <div class="form-group">
                <label for="sd_mosi">MOSI Pin:</label>
                <input type="number" id="sd_mosi" name="sd_mosi" min="0" max="39" value="13">
            </div>
            <div class="form-group">
                <label for="sd_miso">MISO Pin:</label>
                <input type="number" id="sd_miso" name="sd_miso" min="0" max="39" value="12">
            </div>
        </div>

        <div class="form-row">
            <div class="form-group">
                <label for="sd_clk">CLK Pin:</label>
                <input type="number" id="sd_clk" name="sd_clk" min="0" max="39" value="14">
            </div>
            <div class="form-group">
                <label for="sd_cs">CS Pin:</label>
                <input type="number" id="sd_cs" name="sd_cs" min="0" max="39" value="5">
            </div>
        </div>

        <div class="form-group">
            <label for="sd_spi_host">SPI Host:</label>
            <select id="sd_spi_host" name="sd_spi_host">
                <option value="1">HSPI (SPI2)</option>
                <option value="2" selected>VSPI (SPI3)</option>
            </select>
        </div>

        <button type="button" class="btn-test" onclick="checkSDStatus()">
            Check SD Card Status
        </button>
        <div id="sd_status_result" class="test-result"></div>

        <button type="button" class="btn-secondary" onclick="replayCachedMessages()">
            Replay Cached Messages
        </button>

        <button type="button" class="btn-danger" onclick="clearCachedMessages()">
            Clear All Cached Messages
        </button>
    </div>
</div>
```

---

## GAP #4: RTC CONFIGURATION PANEL (Missing)

**Location**: Add after SD panel in `web_config.c`

**Missing HTML** (~120 lines):

```html
<!-- RTC Configuration Panel (MISSING) -->
<div id="rtc_panel" class="section">
    <h3>Real-Time Clock (DS3231)</h3>
    <p class="helper">Maintain accurate time even during network outages</p>

    <div class="form-group">
        <label class="checkbox-label">
            <input type="checkbox" id="rtc_enabled" name="rtc_enabled" onchange="toggleRTCOptions()">
            <span>Enable RTC</span>
        </label>
    </div>

    <div id="rtc_options" style="display:none;">
        <h4>I2C Pin Configuration</h4>
        <div class="form-row">
            <div class="form-group">
                <label for="rtc_sda">SDA Pin:</label>
                <input type="number" id="rtc_sda" name="rtc_sda" min="0" max="39" value="21">
            </div>
            <div class="form-group">
                <label for="rtc_scl">SCL Pin:</label>
                <input type="number" id="rtc_scl" name="rtc_scl" min="0" max="39" value="22">
            </div>
        </div>

        <div class="form-group">
            <label for="rtc_i2c_num">I2C Port:</label>
            <select id="rtc_i2c_num" name="rtc_i2c_num">
                <option value="0" selected>I2C_NUM_0</option>
                <option value="1">I2C_NUM_1</option>
            </select>
        </div>

        <div class="form-group">
            <label class="checkbox-label">
                <input type="checkbox" id="rtc_sync_on_boot" name="rtc_sync_on_boot" checked>
                <span>Sync System Time from RTC on Boot</span>
            </label>
        </div>

        <div class="form-group">
            <label class="checkbox-label">
                <input type="checkbox" id="rtc_update_from_ntp" name="rtc_update_from_ntp" checked>
                <span>Update RTC from NTP When Online</span>
            </label>
        </div>

        <button type="button" class="btn-test" onclick="getRTCTime()">
            Get RTC Time
        </button>
        <div id="rtc_time_result" class="test-result"></div>

        <button type="button" class="btn-secondary" onclick="syncRTCFromNTP()">
            Sync RTC from NTP Now
        </button>

        <button type="button" class="btn-secondary" onclick="syncSystemFromRTC()">
            Sync System Time from RTC
        </button>
    </div>
</div>
```

---

## GAP #5: JAVASCRIPT TOGGLE LOGIC (Missing)

**Location**: Add to `<script>` section in `web_config.c`

**Missing JavaScript** (~50 lines):

```javascript
// Network Mode Toggle (MISSING)
function toggleNetworkMode() {
    const wifiMode = document.getElementById('mode_wifi').checked;
    const simMode = document.getElementById('mode_sim').checked;

    document.getElementById('wifi_panel').style.display = wifiMode ? 'block' : 'none';
    document.getElementById('sim_panel').style.display = simMode ? 'block' : 'none';
}

// SD Card Options Toggle (MISSING)
function toggleSDOptions() {
    const enabled = document.getElementById('sd_enabled').checked;
    document.getElementById('sd_options').style.display = enabled ? 'block' : 'none';
}

// RTC Options Toggle (MISSING)
function toggleRTCOptions() {
    const enabled = document.getElementById('rtc_enabled').checked;
    document.getElementById('rtc_options').style.display = enabled ? 'block' : 'none';
}

// SIM Test Function (MISSING)
function testSIMConnection() {
    const result = document.getElementById('sim_test_result');
    result.innerHTML = 'Testing SIM connection...';

    fetch('/api/sim_test', { method: 'POST' })
        .then(r => r.json())
        .then(data => {
            result.innerHTML = data.success ?
                '<span class="success">✓ SIM connected! Signal: ' + data.signal + ' dBm, Operator: ' + data.operator + '</span>' :
                '<span class="error">✗ SIM connection failed: ' + data.error + '</span>';
        })
        .catch(err => {
            result.innerHTML = '<span class="error">✗ Test failed: ' + err + '</span>';
        });
}

// SD Status Check (MISSING)
function checkSDStatus() {
    const result = document.getElementById('sd_status_result');
    result.innerHTML = 'Checking SD card...';

    fetch('/api/sd_status')
        .then(r => r.json())
        .then(data => {
            result.innerHTML = data.mounted ?
                '<span class="success">✓ SD Card: ' + data.size_mb + ' MB total, ' + data.free_mb + ' MB free, ' + data.cached_messages + ' messages cached</span>' :
                '<span class="error">✗ SD Card not mounted</span>';
        })
        .catch(err => {
            result.innerHTML = '<span class="error">✗ Failed to check SD: ' + err + '</span>';
        });
}

// RTC Time Get (MISSING)
function getRTCTime() {
    const result = document.getElementById('rtc_time_result');
    result.innerHTML = 'Reading RTC...';

    fetch('/api/rtc_time')
        .then(r => r.json())
        .then(data => {
            result.innerHTML = data.success ?
                '<span class="success">✓ RTC Time: ' + data.time + ', Temperature: ' + data.temp + '°C</span>' :
                '<span class="error">✗ RTC not available</span>';
        })
        .catch(err => {
            result.innerHTML = '<span class="error">✗ Failed to read RTC: ' + err + '</span>';
        });
}
```

---

## GAP #6: MISSING REST API ENDPOINTS

These 10 endpoints need to be implemented in `web_config.c`:

### 1. `/api/network_mode` - POST
```c
static esp_err_t network_mode_post_handler(httpd_req_t *req) {
    char buf[100];
    httpd_req_recv(req, buf, sizeof(buf));

    // Parse JSON: {"mode": 0|1}
    // Update config->network_mode
    // Save to NVS

    httpd_resp_send(req, "{\"success\":true}", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}
```

### 2-10. Similar handlers for:
- `/api/sim_config` (POST) - Save SIM settings
- `/api/sim_status` (GET) - Get signal strength, operator
- `/api/sim_test` (POST) - Test SIM connection
- `/api/sd_status` (GET) - Get SD card info
- `/api/sd_clear` (POST) - Clear cached messages
- `/api/sd_replay` (POST) - Replay cached messages
- `/api/rtc_time` (GET) - Get RTC time
- `/api/rtc_sync` (POST) - Sync RTC from NTP
- `/api/rtc_set` (POST) - Set RTC time manually

---

## QUICK FIX IMPLEMENTATION PLAN

I can generate all missing code for you in ~30 minutes. Would you like me to:

1. ✅ Generate complete HTML for all 4 panels (500 lines)
2. ✅ Generate all 10 REST API handlers (430 lines)
3. ✅ Generate JavaScript functions (50 lines)
4. ✅ Show exact insertion points in web_config.c

**Total Addition**: ~980 lines to make your Web UI 100% complete

---

## WORKAROUND (Temporary)

Until Web UI is added, you can configure via NVS manually:

```bash
# Enable SIM mode
idf.py menuconfig  # Set network_mode = 1

# Or modify iot_configs.h temporarily:
#define DEFAULT_NETWORK_MODE NETWORK_MODE_SIM
#define DEFAULT_SIM_APN "internet"
```

Then reflash. But **proper Web UI is needed for production**.

---

**Next Step**: Would you like me to generate all missing Web UI code now?
