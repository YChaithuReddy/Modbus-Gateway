#!/bin/bash

# Script to add NVS defaults for SIM, SD, and RTC configs

cd "/c/Users/chath/OneDrive/Documents/Python code/modbus_iot_gateway/main"

# Backup original
cp web_config.c web_config.c.nvs_backup

# Create temp file with new defaults code
cat > temp_defaults.txt << 'EOF'

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
EOF

# Split file at line 6765 and insert new code
head -6765 web_config.c > temp_part1.c
tail -n +6766 web_config.c > temp_part2.c

# Combine: part1 + new defaults + part2
cat temp_part1.c temp_defaults.txt temp_part2.c > web_config_new.c

# Replace original
mv web_config_new.c web_config.c

echo "Added NVS default values for SIM, SD, and RTC configs"
echo "Backup saved as web_config.c.nvs_backup"

# Clean up temp files
rm temp_defaults.txt temp_part1.c temp_part2.c

# Verify insertion
echo ""
echo "Verifying insertion (lines 6766-6770):"
sed -n '6766,6770p' web_config.c

echo ""
echo "Done!"
