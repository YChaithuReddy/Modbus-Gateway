# NVS Configuration Defaults Update

## File: `main/web_config.c`

### Function to Update: `config_reset_to_defaults()` (Line 6749)

**Current code ends at line 6765**. Add the following default values **after line 6765** (after `g_system_config.modem_reset_gpio_pin = 2;`):

```c
    // Network Mode defaults (NEW)
    g_system_config.network_mode = NETWORK_MODE_WIFI;  // Default to WiFi mode

    // SIM Module defaults (NEW)
    g_system_config.sim_config.enabled = false;
    strcpy(g_system_config.sim_config.apn, "");
    strcpy(g_system_config.sim_config.apn_user, "");
    strcpy(g_system_config.sim_config.apn_pass, "");
    g_system_config.sim_config.uart_tx_pin = GPIO_NUM_33;
    g_system_config.sim_config.uart_rx_pin = GPIO_NUM_32;
    g_system_config.sim_config.pwr_pin = GPIO_NUM_4;
    g_system_config.sim_config.reset_pin = GPIO_NUM_15;
    g_system_config.sim_config.uart_num = UART_NUM_1;
    g_system_config.sim_config.uart_baud_rate = 115200;

    // SD Card defaults (NEW)
    g_system_config.sd_config.enabled = false;
    g_system_config.sd_config.cache_on_failure = true;  // Auto-cache when network fails
    g_system_config.sd_config.mosi_pin = GPIO_NUM_13;
    g_system_config.sd_config.miso_pin = GPIO_NUM_12;
    g_system_config.sd_config.clk_pin = GPIO_NUM_14;
    g_system_config.sd_config.cs_pin = GPIO_NUM_5;
    g_system_config.sd_config.spi_host = SPI2_HOST;
    g_system_config.sd_config.max_message_size = 1024;
    g_system_config.sd_config.min_free_space_mb = 10;

    // RTC (DS3231) defaults (NEW)
    g_system_config.rtc_config.enabled = false;
    g_system_config.rtc_config.sda_pin = GPIO_NUM_21;
    g_system_config.rtc_config.scl_pin = GPIO_NUM_22;
    g_system_config.rtc_config.i2c_num = I2C_NUM_0;
```

---

## Insert Location

```c
esp_err_t config_reset_to_defaults(void)
{
    memset(&g_system_config, 0, sizeof(system_config_t));

    // Set default values
    strcpy(g_system_config.wifi_ssid, "");
    strcpy(g_system_config.wifi_password, "");
    strcpy(g_system_config.azure_hub_fqdn, "your-hub.azure-devices.net");
    strcpy(g_system_config.azure_device_id, "your-device-id");
    strcpy(g_system_config.azure_device_key, "");
    g_system_config.telemetry_interval = 120;
    g_system_config.sensor_count = 0;
    g_system_config.config_complete = false;
    g_system_config.modem_reset_enabled = false;
    g_system_config.modem_boot_delay = 15;
    g_system_config.modem_reset_gpio_pin = 2;

    // ========== INSERT NEW DEFAULTS HERE (AFTER LINE 6765) ==========

    // Network Mode defaults (NEW)
    g_system_config.network_mode = NETWORK_MODE_WIFI;

    // SIM Module defaults (NEW)
    g_system_config.sim_config.enabled = false;
    strcpy(g_system_config.sim_config.apn, "");
    strcpy(g_system_config.sim_config.apn_user, "");
    strcpy(g_system_config.sim_config.apn_pass, "");
    g_system_config.sim_config.uart_tx_pin = GPIO_NUM_33;
    g_system_config.sim_config.uart_rx_pin = GPIO_NUM_32;
    g_system_config.sim_config.pwr_pin = GPIO_NUM_4;
    g_system_config.sim_config.reset_pin = GPIO_NUM_15;
    g_system_config.sim_config.uart_num = UART_NUM_1;
    g_system_config.sim_config.uart_baud_rate = 115200;

    // SD Card defaults (NEW)
    g_system_config.sd_config.enabled = false;
    g_system_config.sd_config.cache_on_failure = true;
    g_system_config.sd_config.mosi_pin = GPIO_NUM_13;
    g_system_config.sd_config.miso_pin = GPIO_NUM_12;
    g_system_config.sd_config.clk_pin = GPIO_NUM_14;
    g_system_config.sd_config.cs_pin = GPIO_NUM_5;
    g_system_config.sd_config.spi_host = SPI2_HOST;
    g_system_config.sd_config.max_message_size = 1024;
    g_system_config.sd_config.min_free_space_mb = 10;

    // RTC (DS3231) defaults (NEW)
    g_system_config.rtc_config.enabled = false;
    g_system_config.rtc_config.sda_pin = GPIO_NUM_21;
    g_system_config.rtc_config.scl_pin = GPIO_NUM_22;
    g_system_config.rtc_config.i2c_num = I2C_NUM_0;

    // ========== END OF NEW DEFAULTS ==========

    // Initialize all sensors with default values
    for (int i = 0; i < 8; i++) {
        g_system_config.sensors[i].enabled = false;
        strcpy(g_system_config.sensors[i].data_type, "UINT16_HI");
        // ... rest of sensor initialization
    }

    ESP_LOGI(TAG, "Configuration reset to defaults");
    return ESP_OK;
}
```

---

## GPIO Pin Assignments Summary

**IMPORTANT:** Verify these pins don't conflict with existing Modbus RS485 pins:

### Modbus RS485 (DO NOT CHANGE):
- GPIO 16: RX2 (Modbus RX)
- GPIO 17: TX2 (Modbus TX)
- GPIO 18: RTS (Modbus control)
- GPIO 34: Config trigger (pull low for setup mode)

### SIM Module (A7670C):
- GPIO 33: UART TX
- GPIO 32: UART RX
- GPIO 4: Power pin
- GPIO 15: Reset pin

### SD Card (SPI):
- GPIO 13: MOSI
- GPIO 12: MISO
- GPIO 14: CLK
- GPIO 5: CS

### RTC (DS3231 - I2C):
- GPIO 21: SDA
- GPIO 22: SCL

---

## Notes

1. **NVS Storage is Automatic**: Since `config_save_to_nvs()` uses `nvs_set_blob()` with the entire `system_config_t` structure, the new fields will be automatically saved and loaded.

2. **Migration**: Old configurations (without the new fields) will be loaded with zeroed values for the new fields. The defaults above ensure proper initialization when resetting configuration.

3. **Default Network Mode**: Set to `NETWORK_MODE_WIFI` to preserve backward compatibility with existing deployments.

4. **SD Card Auto-Cache**: Enabled by default (`cache_on_failure = true`) for reliability.

5. **Optional Features**: SIM, SD, and RTC are all disabled by default (`enabled = false`), so they won't interfere with existing WiFi-only deployments.

---

## Testing After Update

After adding these defaults, test:

1. **First Boot** (no NVS data):
   ```bash
   idf.py erase-flash
   idf.py flash monitor
   ```
   - Should initialize with WiFi mode
   - SIM/SD/RTC disabled

2. **Configuration Reset**:
   - Trigger config reset from web UI
   - Verify defaults are applied correctly

3. **NVS Migration**:
   - Flash old firmware (without new fields)
   - Configure and save
   - Flash new firmware
   - Verify old config loads correctly with new fields defaulted

---

**Status:** Ready to implement
**File:** `main/web_config.c`
**Function:** `config_reset_to_defaults()`
**Lines to modify:** After line 6765
