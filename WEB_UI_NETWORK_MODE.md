# Web UI - Network Mode Section Implementation

## Overview

This document describes the new **Network Mode** section that replaces the standalone WiFi Configuration section and adds SIM module support.

---

## New Section Structure

### 1. Network Mode Selection
- Radio buttons for WiFi / SIM mode selection
- Current network status display
- Signal strength indicator

### 2. WiFi Configuration (when WiFi mode selected)
- Network scanning
- SSID and password inputs
- Test connection button

### 3. SIM Configuration (when SIM mode selected)
- APN, username, password inputs
- GPIO pin configuration
- Test SIM connection button
- Signal strength display (CSQ)

### 4. SD Card Configuration
- Enable/disable offline caching
- GPIO pin configuration
- Status display (mount, free space, cached messages)
- Clear cache button

### 5. RTC Configuration
- Enable/disable RTC
- GPIO pin configuration
- Sync with NTP button
- Current time display

### 6. System Controls
- Modem reset controls (moved from separate section)
- Reboot to operation mode button
- Reboot device button

---

## HTML Structure

```html
<div id='network' class='section'>
    <h2 class='section-title'>
        <i class='fas fa-network-wired'></i>Network Mode Configuration
    </h2>

    <!-- Network Mode Selection -->
    <div class='sensor-card'>
        <h3>Network Mode</h3>
        <div class='mode-selector'>
            <label class='radio-card'>
                <input type='radio' name='network_mode' value='wifi' checked>
                <div class='radio-content'>
                    <i class='fas fa-wifi'></i>
                    <span>WiFi Mode</span>
                </div>
            </label>
            <label class='radio-card'>
                <input type='radio' name='network_mode' value='sim'>
                <div class='radio-content'>
                    <i class='fas fa-signal'></i>
                    <span>SIM Mode (4G LTE)</span>
                </div>
            </label>
        </div>

        <!-- Network Status Display -->
        <div id='network-status' class='status-box'>
            <div class='status-item'>
                <span class='status-label'>Current Mode:</span>
                <span class='status-value' id='current-mode'>WiFi</span>
            </div>
            <div class='status-item'>
                <span class='status-label'>Connection:</span>
                <span class='status-value' id='connection-status'>Connected</span>
            </div>
            <div class='status-item'>
                <span class='status-label'>Signal Strength:</span>
                <span class='status-value' id='signal-strength'>-65 dBm (Good)</span>
            </div>
        </div>
    </div>

    <!-- WiFi Configuration Panel -->
    <div id='wifi-panel' class='config-panel'>
        <form id='wifi-form' onsubmit='return saveWiFiConfig()'>
            <div class='sensor-card'>
                <h3><i class='fas fa-wifi'></i> WiFi Settings</h3>
                <button type='button' onclick='scanWiFi()' class='scan-button'>
                    Scan Networks
                </button>
                <div id='networks'></div>

                <label>Network SSID:</label>
                <input type='text' id='wifi_ssid' name='wifi_ssid' required>

                <label>Password:</label>
                <input type='password' id='wifi_password' name='wifi_password'>

                <div class='button-group'>
                    <button type='button' onclick='testWiFi()' class='test-button'>
                        Test Connection
                    </button>
                    <button type='submit' class='save-button'>
                        Save WiFi Config
                    </button>
                </div>
                <div id='wifi-result' class='result-box'></div>
            </div>
        </form>
    </div>

    <!-- SIM Configuration Panel -->
    <div id='sim-panel' class='config-panel' style='display:none'>
        <form id='sim-form' onsubmit='return saveSIMConfig()'>
            <div class='sensor-card'>
                <h3><i class='fas fa-sim-card'></i> SIM Module Settings</h3>

                <label>APN:</label>
                <input type='text' id='sim_apn' name='sim_apn' placeholder='e.g., airteliot, internet'>

                <label>APN Username (optional):</label>
                <input type='text' id='sim_user' name='sim_user'>

                <label>APN Password (optional):</label>
                <input type='password' id='sim_pass' name='sim_pass'>

                <details>
                    <summary style='cursor:pointer;font-weight:600;margin:15px 0'>
                        Advanced GPIO Configuration
                    </summary>
                    <div class='gpio-grid'>
                        <label>UART TX Pin:</label>
                        <input type='number' id='sim_tx' value='33' min='0' max='39'>

                        <label>UART RX Pin:</label>
                        <input type='number' id='sim_rx' value='32' min='0' max='39'>

                        <label>Power Pin:</label>
                        <input type='number' id='sim_pwr' value='4' min='0' max='39'>

                        <label>Reset Pin:</label>
                        <input type='number' id='sim_rst' value='15' min='0' max='39'>
                    </div>
                </details>

                <div class='button-group'>
                    <button type='button' onclick='testSIM()' class='test-button'>
                        Test SIM Connection
                    </button>
                    <button type='submit' class='save-button'>
                        Save SIM Config
                    </button>
                </div>
                <div id='sim-result' class='result-box'></div>

                <!-- SIM Status Display -->
                <div id='sim-status' class='status-box'>
                    <div class='status-item'>
                        <span class='status-label'>CSQ:</span>
                        <span class='status-value' id='sim-csq'>--</span>
                    </div>
                    <div class='status-item'>
                        <span class='status-label'>Operator:</span>
                        <span class='status-value' id='sim-operator'>--</span>
                    </div>
                </div>
            </div>
        </form>
    </div>

    <!-- SD Card Configuration -->
    <div class='sensor-card'>
        <h3><i class='fas fa-sd-card'></i> SD Card (Offline Caching)</h3>
        <form id='sd-form' onsubmit='return saveSDConfig()'>
            <label>
                <input type='checkbox' id='sd_enabled' name='sd_enabled'>
                Enable SD Card Offline Caching
            </label>
            <small>Cache telemetry when network unavailable and replay when reconnected</small>

            <details>
                <summary style='cursor:pointer;font-weight:600;margin:15px 0'>
                    GPIO Configuration
                </summary>
                <div class='gpio-grid'>
                    <label>MOSI Pin:</label>
                    <input type='number' id='sd_mosi' value='13' min='0' max='39'>

                    <label>MISO Pin:</label>
                    <input type='number' id='sd_miso' value='12' min='0' max='39'>

                    <label>CLK Pin:</label>
                    <input type='number' id='sd_clk' value='14' min='0' max='39'>

                    <label>CS Pin:</label>
                    <input type='number' id='sd_cs' value='5' min='0' max='39'>
                </div>
            </details>

            <button type='submit' class='save-button'>Save SD Config</button>
        </form>

        <!-- SD Card Status -->
        <div id='sd-status' class='status-box'>
            <div class='status-item'>
                <span class='status-label'>Mount Status:</span>
                <span class='status-value' id='sd-mount'>--</span>
            </div>
            <div class='status-item'>
                <span class='status-label'>Free Space:</span>
                <span class='status-value' id='sd-space'>--</span>
            </div>
            <div class='status-item'>
                <span class='status-label'>Cached Messages:</span>
                <span class='status-value' id='sd-cached'>--</span>
            </div>
        </div>

        <button type='button' onclick='clearSDCache()' class='danger-button'>
            Clear Cache
        </button>
    </div>

    <!-- RTC Configuration -->
    <div class='sensor-card'>
        <h3><i class='fas fa-clock'></i> Real-Time Clock (DS3231)</h3>
        <form id='rtc-form' onsubmit='return saveRTCConfig()'>
            <label>
                <input type='checkbox' id='rtc_enabled' name='rtc_enabled'>
                Enable RTC for Accurate Timestamps
            </label>

            <details>
                <summary style='cursor:pointer;font-weight:600;margin:15px 0'>
                    I2C Configuration
                </summary>
                <div class='gpio-grid'>
                    <label>SDA Pin:</label>
                    <input type='number' id='rtc_sda' value='21' min='0' max='39'>

                    <label>SCL Pin:</label>
                    <input type='number' id='rtc_scl' value='22' min='0' max='39'>
                </div>
            </details>

            <button type='submit' class='save-button'>Save RTC Config</button>
        </form>

        <!-- RTC Status -->
        <div id='rtc-status' class='status-box'>
            <div class='status-item'>
                <span class='status-label'>Current Time:</span>
                <span class='status-value' id='rtc-time'>--</span>
            </div>
        </div>

        <button type='button' onclick='syncRTC()' class='action-button'>
            Sync with NTP
        </button>
    </div>

    <!-- System Controls -->
    <div class='sensor-card'>
        <h3><i class='fas fa-cog'></i> System Controls</h3>

        <!-- Modem Reset (moved here) -->
        <form id='modem-form' onsubmit='return saveModemConfig()'>
            <label>
                <input type='checkbox' id='modem_reset_enabled'>
                Enable Modem Reset on MQTT Disconnect
            </label>

            <div class='form-row'>
                <div>
                    <label>GPIO Pin:</label>
                    <input type='number' id='modem_gpio' value='2' min='2' max='39'>
                </div>
                <div>
                    <label>Boot Delay (sec):</label>
                    <input type='number' id='modem_delay' value='15' min='5' max='60'>
                </div>
            </div>

            <button type='submit' class='save-button'>Save Modem Settings</button>
        </form>

        <!-- System Reboot Buttons -->
        <div class='button-group' style='margin-top:20px'>
            <button type='button' onclick='rebootToOperation()' class='warning-button'>
                <i class='fas fa-sync'></i> Reboot to Operation Mode
            </button>
            <button type='button' onclick='rebootDevice()' class='danger-button'>
                <i class='fas fa-power-off'></i> Reboot Device
            </button>
        </div>
    </div>
</div>
```

---

## CSS Styles

```css
/* Network Mode Radio Cards */
.mode-selector {
    display: grid;
    grid-template-columns: 1fr 1fr;
    gap: 15px;
    margin: 15px 0;
}

.radio-card {
    border: 2px solid #ddd;
    border-radius: 8px;
    padding: 20px;
    cursor: pointer;
    transition: all 0.3s ease;
    text-align: center;
}

.radio-card:hover {
    border-color: #007bff;
    background: #f8f9fa;
}

.radio-card input[type='radio'] {
    display: none;
}

.radio-card input[type='radio']:checked + .radio-content {
    color: #007bff;
}

.radio-card input[type='radio']:checked ~ .radio-content {
    border-color: #007bff;
    background: #e7f3ff;
}

.radio-content {
    display: flex;
    flex-direction: column;
    align-items: center;
    gap: 10px;
}

.radio-content i {
    font-size: 32px;
}

.radio-content span {
    font-weight: 600;
    font-size: 14px;
}

/* Config Panels */
.config-panel {
    margin-top: 15px;
}

/* Status Box */
.status-box {
    background: #f8f9fa;
    border: 1px solid #ddd;
    border-radius: 6px;
    padding: 15px;
    margin: 15px 0;
}

.status-item {
    display: flex;
    justify-content: space-between;
    padding: 8px 0;
    border-bottom: 1px solid #e9ecef;
}

.status-item:last-child {
    border-bottom: none;
}

.status-label {
    font-weight: 600;
    color: #495057;
}

.status-value {
    color: #212529;
    font-family: monospace;
}

/* GPIO Grid */
.gpio-grid {
    display: grid;
    grid-template-columns: 1fr 1fr;
    gap: 15px;
    margin: 15px 0;
}

/* Button Styles */
.button-group {
    display: flex;
    gap: 10px;
    margin-top: 15px;
}

.save-button {
    background: #28a745;
    color: white;
    padding: 10px 20px;
    border: none;
    border-radius: 5px;
    font-weight: 600;
    cursor: pointer;
}

.test-button {
    background: #007bff;
    color: white;
    padding: 10px 20px;
    border: none;
    border-radius: 5px;
    font-weight: 600;
    cursor: pointer;
}

.action-button {
    background: #17a2b8;
    color: white;
    padding: 10px 20px;
    border: none;
    border-radius: 5px;
    font-weight: 600;
    cursor: pointer;
}

.warning-button {
    background: #ffc107;
    color: #212529;
    padding: 10px 20px;
    border: none;
    border-radius: 5px;
    font-weight: 600;
    cursor: pointer;
}

.danger-button {
    background: #dc3545;
    color: white;
    padding: 10px 20px;
    border: none;
    border-radius: 5px;
    font-weight: 600;
    cursor: pointer;
}

/* Result Box */
.result-box {
    margin-top: 15px;
    padding: 10px;
    border-radius: 5px;
    display: none;
}

.result-box.success {
    background: #d4edda;
    border: 1px solid #c3e6cb;
    color: #155724;
}

.result-box.error {
    background: #f8d7da;
    border: 1px solid #f5c6cb;
    color: #721c24;
}
```

---

## JavaScript Functions

```javascript
// Network Mode Selection
function selectNetworkMode(mode) {
    if (mode === 'wifi') {
        document.getElementById('wifi-panel').style.display = 'block';
        document.getElementById('sim-panel').style.display = 'none';
    } else {
        document.getElementById('wifi-panel').style.display = 'none';
        document.getElementById('sim-panel').style.display = 'block';
    }
}

// Event listener for mode selection
document.querySelectorAll('input[name="network_mode"]').forEach(radio => {
    radio.addEventListener('change', function() {
        selectNetworkMode(this.value);
    });
});

// WiFi Configuration
function saveWiFiConfig() {
    const formData = new FormData(document.getElementById('wifi-form'));

    fetch('/api/network/wifi', {
        method: 'POST',
        body: formData
    })
    .then(response => response.json())
    .then(data => {
        showResult('wifi-result', data.success, data.message);
    })
    .catch(error => {
        showResult('wifi-result', false, 'Failed to save WiFi config');
    });

    return false;
}

function testWiFi() {
    const ssid = document.getElementById('wifi_ssid').value;
    const password = document.getElementById('wifi_password').value;

    fetch('/api/network/wifi/test', {
        method: 'POST',
        headers: {'Content-Type': 'application/json'},
        body: JSON.stringify({ssid, password})
    })
    .then(response => response.json())
    .then(data => {
        showResult('wifi-result', data.success, data.message);
        if (data.signal_strength) {
            document.getElementById('signal-strength').textContent =
                `${data.signal_strength} dBm (${data.quality})`;
        }
    });
}

// SIM Configuration
function saveSIMConfig() {
    const formData = new FormData(document.getElementById('sim-form'));

    fetch('/api/network/sim', {
        method: 'POST',
        body: formData
    })
    .then(response => response.json())
    .then(data => {
        showResult('sim-result', data.success, data.message);
    });

    return false;
}

function testSIM() {
    fetch('/api/network/sim/test', {
        method: 'POST'
    })
    .then(response => response.json())
    .then(data => {
        showResult('sim-result', data.success, data.message);
        if (data.csq) {
            document.getElementById('sim-csq').textContent = data.csq;
        }
        if (data.operator) {
            document.getElementById('sim-operator').textContent = data.operator;
        }
    });
}

// SD Card Functions
function saveSDConfig() {
    const formData = new FormData(document.getElementById('sd-form'));

    fetch('/api/sd/config', {
        method: 'POST',
        body: formData
    })
    .then(response => response.json())
    .then(data => {
        showResult('sd-result', data.success, data.message);
        updateSDStatus();
    });

    return false;
}

function updateSDStatus() {
    fetch('/api/sd/status')
    .then(response => response.json())
    .then(data => {
        document.getElementById('sd-mount').textContent = data.mounted ? 'Mounted' : 'Not Mounted';
        document.getElementById('sd-space').textContent = data.free_space + ' MB';
        document.getElementById('sd-cached').textContent = data.cached_messages;
    });
}

function clearSDCache() {
    if (confirm('Clear all cached messages from SD card?')) {
        fetch('/api/sd/clear', {method: 'POST'})
        .then(response => response.json())
        .then(data => {
            alert(data.message);
            updateSDStatus();
        });
    }
}

// RTC Functions
function saveRTCConfig() {
    const formData = new FormData(document.getElementById('rtc-form'));

    fetch('/api/rtc/config', {
        method: 'POST',
        body: formData
    })
    .then(response => response.json())
    .then(data => {
        showResult('rtc-result', data.success, data.message);
    });

    return false;
}

function syncRTC() {
    fetch('/api/rtc/sync', {method: 'POST'})
    .then(response => response.json())
    .then(data => {
        alert(data.message);
        if (data.time) {
            document.getElementById('rtc-time').textContent = data.time;
        }
    });
}

// System Control
function saveModemConfig() {
    const formData = new FormData(document.getElementById('modem-form'));

    fetch('/api/modem/config', {
        method: 'POST',
        body: formData
    })
    .then(response => response.json())
    .then(data => {
        showResult('modem-result', data.success, data.message);
    });

    return false;
}

function rebootToOperation() {
    if (confirm('Reboot to normal operation mode?')) {
        fetch('/api/system/reboot_operation', {method: 'POST'})
        .then(() => {
            alert('Rebooting to operation mode...');
        });
    }
}

function rebootDevice() {
    if (confirm('Reboot the entire device?')) {
        fetch('/api/system/reboot', {method: 'POST'})
        .then(() => {
            alert('Device rebooting...');
        });
    }
}

// Utility function
function showResult(elementId, success, message) {
    const resultBox = document.getElementById(elementId);
    resultBox.className = 'result-box ' + (success ? 'success' : 'error');
    resultBox.textContent = message;
    resultBox.style.display = 'block';
    setTimeout(() => {
        resultBox.style.display = 'none';
    }, 5000);
}
```

---

## Implementation Strategy

Due to the size of this update, I recommend:

1. **Create new HTML handlers** for each API endpoint
2. **Update existing handlers** to integrate with new UI
3. **Test incrementally** - start with Network Mode selection, then add each panel

This is a large update that will require careful integration with the existing web_config.c code.

---

**Next Steps:**
1. Create API endpoint handlers in C
2. Integrate HTML/CSS/JS into web_config.c
3. Test each section individually
