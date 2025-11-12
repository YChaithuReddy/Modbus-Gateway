# ESP32 Modbus IoT Gateway - Complete Technical Reference

## Purpose
Master the ESP32 Modbus IoT Gateway architecture, components, functions, and development workflows for industrial IoT applications.

## When to Use
- Developing or modifying the ESP32 Modbus Gateway firmware
- Troubleshooting hardware or communication issues
- Adding new sensor types or protocols
- Integrating with Azure IoT Hub
- Understanding the system architecture

---

## 1. Hardware Architecture

### Core Components

#### ESP32-WROOM-32 Microcontroller
```
Specifications:
- Dual-core Xtensa LX6 @ 240MHz
- 520KB SRAM, 4MB Flash
- WiFi 802.11 b/g/n
- Bluetooth 4.2 BR/EDR and BLE
- Operating voltage: 3.3V
- Temperature range: -40°C to +85°C (industrial grade)
```

**GPIO Pin Assignments:**
```c
// RS485 Communication
#define RS485_RX_PIN      GPIO_NUM_16  // UART2 RX
#define RS485_TX_PIN      GPIO_NUM_17  // UART2 TX
#define RS485_RTS_PIN     GPIO_NUM_4   // Direction control

// Configuration Trigger
#define CONFIG_TRIGGER_PIN GPIO_NUM_34 // Pull LOW for config mode

// SD Card (SPI) - Configurable
Default: MOSI=23, MISO=19, CLK=18, CS=5

// RTC DS3231 (I2C) - Configurable
Default: SDA=21, SCL=22

// SIM Module A7670C (UART)
Default: TX=GPIO_NUM_27, RX=GPIO_NUM_26
```

### Peripheral Modules

#### 1. RS485 Transceiver (MAX485/SN75176)
```
Purpose: Industrial Modbus RTU communication
Connections:
- DI (Data In)  -> ESP32 TX (GPIO 17)
- RO (Receive Out) -> ESP32 RX (GPIO 16)
- DE/RE (Direction) -> ESP32 RTS (GPIO 4)
- A, B -> Modbus bus terminals
- VCC -> 5V, GND -> GND

Configuration:
- Baud Rate: 9600 (default, configurable)
- Data Bits: 8
- Parity: None
- Stop Bits: 1
```

#### 2. SD Card Module (SPI)
```
Purpose: Offline message caching
Pinout:
- MOSI -> GPIO 23 (configurable)
- MISO -> GPIO 19 (configurable)
- CLK  -> GPIO 18 (configurable)
- CS   -> GPIO 5 (configurable)
- VCC  -> 3.3V
- GND  -> GND

SPI Host Options:
- HSPI (SPI2) - Default
- VSPI (SPI3)

Supported Cards: microSD, microSDHC (FAT32)
```

#### 3. DS3231 RTC Module (I2C)
```
Purpose: Accurate timekeeping during network outages
Pinout:
- SDA -> GPIO 21 (configurable)
- SCL -> GPIO 22 (configurable)
- VCC -> 3.3V
- GND -> GND

I2C Address: 0x68
Features:
- Temperature-compensated crystal oscillator (TCXO)
- ±2ppm accuracy (0-40°C)
- Battery backup (CR2032)
- Automatic leap year compensation
```

#### 4. SIM Module A7670C (4G LTE)
```
Purpose: Cellular connectivity backup
Interface: UART
Pinout:
- TX -> GPIO 27 (configurable)
- RX -> GPIO 26 (configurable)
- VCC -> 4.0V (requires dedicated power supply)
- GND -> GND

Baud Rate Options: 9600, 19200, 38400, 57600, 115200
AT Commands: Standard Hayes commands
Network: 4G LTE CAT-1
```

---

## 2. Software Architecture

### Task Structure (FreeRTOS)

```c
// Core 0: Modbus and sensor reading
Task: modbus_task
Priority: 5
Stack: 4096 bytes
Function: Read Modbus sensors every telemetry_interval

// Core 1: MQTT communication
Task: mqtt_task
Priority: 4
Stack: 8192 bytes
Function: Maintain Azure IoT Hub connection

Task: telemetry_task
Priority: 3
Stack: 8192 bytes
Function: Send sensor data to Azure IoT Hub

// Web Server: Automatic core assignment
Task: httpd_server
Function: Configuration web interface
```

### File Structure

```
modbus_iot_gateway/
├── main/
│   ├── main.c                 // Entry point, task management
│   ├── modbus.c/h             // Modbus RTU implementation
│   ├── sensor_manager.c/h     // Multi-sensor coordination
│   ├── web_config.c/h         // Web interface (8,360 lines)
│   ├── json_templates.c/h     // Azure telemetry formatting
│   ├── iot_configs.h          // WiFi and Azure credentials
│   ├── azure_ca_cert.pem      // Azure root certificate
│   ├── sd_card_logger.c/h     // SD card caching
│   └── CMakeLists.txt
├── CMakeLists.txt             // Main build config
├── sdkconfig                  // ESP-IDF configuration
├── partitions_4mb.csv         // Flash partitioning
└── README.md                  // Project documentation
```

---

## 3. Modbus RTU Implementation

### Function Code Support

```c
// Supported function codes
#define MODBUS_FC_READ_HOLDING_REGISTERS  0x03
#define MODBUS_FC_READ_INPUT_REGISTERS    0x04
#define MODBUS_FC_WRITE_SINGLE_REGISTER   0x06
#define MODBUS_FC_WRITE_MULTIPLE_REGISTERS 0x10

// Register types
typedef enum {
    HOLDING_REGISTER = 0x03,  // Read/Write
    INPUT_REGISTER = 0x04     // Read-only
} register_type_t;
```

### Data Format Support (16 Formats)

#### 16-bit Formats (1 register)
```c
UINT16_BE    // Big Endian (AB)
UINT16_LE    // Little Endian (BA)
INT16_BE     // Signed Big Endian
INT16_LE     // Signed Little Endian
```

#### 32-bit Integer Formats (2 registers)
```c
UINT32_ABCD  // Big Endian (register order: AB CD)
UINT32_DCBA  // Little Endian (register order: DC BA)
UINT32_BADC  // Mid-Big Endian (register order: BA DC)
UINT32_CDAB  // Mid-Little Endian (register order: CD AB)
INT32_*      // Signed variants of above
```

#### 32-bit Float Formats (2 registers - IEEE 754)
```c
FLOAT32_ABCD // Big Endian float
FLOAT32_DCBA // Little Endian float
FLOAT32_BADC // Mid-Big Endian float
FLOAT32_CDAB // Mid-Little Endian float
```

### Modbus Communication Flow

```c
// 1. Build Modbus request frame
uint8_t frame[8];
frame[0] = slave_id;
frame[1] = function_code;
frame[2] = (register_address >> 8) & 0xFF;  // Address high
frame[3] = register_address & 0xFF;          // Address low
frame[4] = (quantity >> 8) & 0xFF;           // Quantity high
frame[5] = quantity & 0xFF;                  // Quantity low

// 2. Calculate and append CRC16
uint16_t crc = calculate_crc16(frame, 6);
frame[6] = crc & 0xFF;        // CRC low
frame[7] = (crc >> 8) & 0xFF; // CRC high

// 3. Send via UART with RTS control
gpio_set_level(RS485_RTS_PIN, 1);  // Transmit mode
uart_write_bytes(UART_NUM_2, frame, 8);
uart_wait_tx_done(UART_NUM_2, pdMS_TO_TICKS(100));
gpio_set_level(RS485_RTS_PIN, 0);  // Receive mode

// 4. Wait for response with timeout
uart_read_bytes(UART_NUM_2, response, expected_length, pdMS_TO_TICKS(1000));

// 5. Validate CRC and parse data
if (validate_crc16(response, response_length)) {
    // Extract data bytes and convert to value
}
```

### CRC16 Calculation (Modbus standard)

```c
uint16_t calculate_crc16(const uint8_t *data, size_t length) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < length; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++) {
            if (crc & 0x0001) {
                crc = (crc >> 1) ^ 0xA001;
            } else {
                crc >>= 1;
            }
        }
    }
    return crc;
}
```

---

## 4. Sensor Types and Configurations

### Supported Sensor Types

#### 1. Generic Sensors
```c
sensor_type = "GENERIC"
Parameters:
- slave_id: 1-247 (Modbus slave address)
- register_address: 0-65535
- quantity: 1-2 (number of registers)
- register_type: HOLDING or INPUT
- data_type: UINT16_BE, INT32_ABCD, FLOAT32_ABCD, etc.
- scale_factor: Multiplication factor
- offset: Addition offset
- unit_id: Logical identifier for grouping
```

#### 2. Level Sensors
```c
sensor_type = "Level"
Special Parameters:
- sensor_height: Physical height of sensor (meters)
- max_water_level: Maximum measurable level (meters)
Calculation:
  level_percent = (sensor_height - raw_value) / max_water_level * 100
```

#### 3. Rain Gauge
```c
sensor_type = "RAINGAUGE"
Special Parameters:
- scale_factor: Conversion to mm or inches
- Accumulates rainfall over time
```

#### 4. Water Quality Sensors
```c
sensor_type = "QUALITY"
Subtype options:
- "PH" - pH measurement (0-14)
- "TURBIDITY" - Water clarity (NTU)
- "CONDUCTIVITY" - Electrical conductivity (μS/cm)
- "TEMPERATURE" - Water temperature (°C or °F)
- "DO" - Dissolved Oxygen (mg/L or %)
- "ORP" - Oxidation-Reduction Potential (mV)

Special Features:
- Separate submenu in web interface
- Quality-specific telemetry formatting
```

#### 5. Energy Meters (Advanced)
```c
sensor_type = "ENERGY"
Parameters:
- energy_register_count: Number of registers to read
- Hex data capture for protocol analysis
- Supports multi-register energy values
```

---

## 5. Azure IoT Hub Integration

### Connection Configuration

```c
// iot_configs.h
#define IOT_CONFIG_WIFI_SSID "Your_WiFi_SSID"
#define IOT_CONFIG_WIFI_PASS "Your_WiFi_Password"
#define IOT_CONFIG_IOTHUB_FQDN "your-hub.azure-devices.net"
#define AZURE_DEVICE_ID "your-device-id"
#define AZURE_DEVICE_KEY "your-device-key"
```

### MQTT Connection

```c
Protocol: MQTT over TLS (port 8883)
Client ID: {device_id}
Username: {iothub_fqdn}/{device_id}/?api-version=2021-04-12
Password: SharedAccessSignature (SAS token)

SAS Token Format:
SharedAccessSignature sr={resource}&sig={signature}&se={expiry}

Topic for telemetry:
devices/{device_id}/messages/events/
```

### SAS Token Generation

```c
// 1. Create string to sign
char string_to_sign[512];
snprintf(string_to_sign, sizeof(string_to_sign),
    "%s\n%ld", resource_uri, expiry_timestamp);

// 2. HMAC-SHA256 signature
unsigned char hmac[32];
mbedtls_md_hmac(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256),
    device_key, key_len, string_to_sign, strlen(string_to_sign), hmac);

// 3. Base64 encode
char signature_b64[64];
mbedtls_base64_encode(signature_b64, sizeof(signature_b64), &olen,
    hmac, 32);

// 4. URL encode signature
char signature_encoded[128];
url_encode(signature_b64, signature_encoded);

// 5. Build SAS token
snprintf(sas_token, size,
    "SharedAccessSignature sr=%s&sig=%s&se=%ld",
    resource_uri, signature_encoded, expiry);
```

### Telemetry JSON Format

```json
{
  "deviceId": "testing_3",
  "timestamp": "2025-01-12T14:30:00Z",
  "sensors": [
    {
      "name": "Water Tank Level",
      "unitId": "TANK01",
      "value": 45.5,
      "unit": "%",
      "sensorType": "Level",
      "status": "OK"
    },
    {
      "name": "pH Sensor",
      "unitId": "WQ01",
      "value": 7.2,
      "unit": "pH",
      "sensorType": "QUALITY",
      "qualityType": "PH",
      "status": "OK"
    }
  ],
  "systemHealth": {
    "freeHeap": 180000,
    "rssi": -65,
    "uptime": 3600
  }
}
```

---

## 6. SD Card Caching System

### Purpose
Store telemetry messages when MQTT connection fails, replay when connection restored.

### File Structure
```
/sdcard/
  └── cache/
      ├── msg_0001.json  // Cached message 1
      ├── msg_0002.json  // Cached message 2
      └── ...
```

### Caching Logic

```c
// 1. Check if MQTT publish failed
if (mqtt_publish_failed) {
    if (sd_config.cache_on_failure) {
        // Save to SD card
        save_to_sd_card(telemetry_json);
    }
}

// 2. On reconnection, replay cached messages
if (mqtt_connected && has_cached_messages()) {
    replay_cached_messages();
}

// 3. File naming convention
sprintf(filename, "/sdcard/cache/msg_%04d.json", message_counter);
```

### Configuration Options
```c
typedef struct {
    bool enabled;              // Enable SD card
    bool cache_on_failure;     // Auto-cache on MQTT failure
    uint8_t mosi_pin;          // SPI MOSI
    uint8_t miso_pin;          // SPI MISO
    uint8_t clk_pin;           // SPI CLK
    uint8_t cs_pin;            // SPI CS
    uint8_t spi_host;          // HSPI or VSPI
} sd_config_t;
```

---

## 7. RTC (DS3231) Integration

### Purpose
Maintain accurate timestamps during network outages.

### I2C Communication

```c
// Read RTC time
uint8_t i2c_data[7];
i2c_cmd_handle_t cmd = i2c_cmd_link_create();
i2c_master_start(cmd);
i2c_master_write_byte(cmd, (0x68 << 1) | I2C_MASTER_WRITE, true);
i2c_master_write_byte(cmd, 0x00, true);  // Register 0x00 (seconds)
i2c_master_start(cmd);
i2c_master_write_byte(cmd, (0x68 << 1) | I2C_MASTER_READ, true);
i2c_master_read(cmd, i2c_data, 7, I2C_MASTER_LAST_NACK);
i2c_master_stop(cmd);
i2c_master_cmd_begin(I2C_NUM_0, cmd, pdMS_TO_TICKS(1000));
i2c_cmd_link_delete(cmd);

// Convert BCD to decimal
struct tm timeinfo;
timeinfo.tm_sec = bcd_to_dec(i2c_data[0] & 0x7F);
timeinfo.tm_min = bcd_to_dec(i2c_data[1] & 0x7F);
timeinfo.tm_hour = bcd_to_dec(i2c_data[2] & 0x3F);
timeinfo.tm_mday = bcd_to_dec(i2c_data[4] & 0x3F);
timeinfo.tm_mon = bcd_to_dec(i2c_data[5] & 0x1F) - 1;
timeinfo.tm_year = bcd_to_dec(i2c_data[6]) + 100;
```

### Sync Strategies

```c
// 1. Sync system time from RTC on boot
if (rtc_config.sync_on_boot) {
    read_rtc_time(&timeinfo);
    time_t now = mktime(&timeinfo);
    settimeofday(&now, NULL);
}

// 2. Update RTC from NTP when online
if (rtc_config.update_from_ntp && wifi_connected) {
    sntp_sync_time(&timeinfo);
    write_rtc_time(&timeinfo);
}
```

---

## 8. Web Configuration Interface

### Operating Modes

#### Setup Mode (CONFIG_STATE_SETUP)
```
Trigger: GPIO 34 pulled LOW
WiFi Mode: Access Point
SSID: "ModbusIoT-Config"
Password: "config123"
IP Address: 192.168.4.1
Web Server: http://192.168.4.1
Duration: Until GPIO 34 released or manual restart
```

#### Operation Mode (CONFIG_STATE_OPERATION)
```
WiFi Mode: Station (connects to configured network)
MQTT: Active connection to Azure IoT Hub
Telemetry: Sends sensor data every telemetry_interval seconds
Web Server: Disabled (for security and performance)
```

### Web Interface Sections

1. **System Overview** 📊
   - Firmware version
   - Free heap memory
   - WiFi RSSI
   - Uptime
   - Connected sensors count

2. **Network Configuration** 🌐
   - Network mode selection (WiFi/SIM)
   - WiFi SSID/password
   - Network scanning
   - SIM APN configuration

3. **SD Card Configuration** 💾
   - Enable/disable SD card
   - Offline caching toggle
   - SPI pin configuration
   - Status check and replay

4. **RTC Configuration** 🕐
   - Enable/disable RTC
   - Boot sync option
   - NTP update option
   - I2C pin configuration
   - Time sync operations

5. **Azure IoT Hub** ☁️
   - Device ID
   - Device key (password protected)
   - Telemetry interval (30-3600 seconds)

6. **Modbus Sensors** 🔌
   - Add/Edit/Delete sensors
   - Test RS485 communication
   - Separate regular and water quality sensors
   - Real-time value display

7. **Write Operations** ✏️
   - Write single register (FC 06)
   - Write multiple registers (FC 16)
   - Industrial safety warnings

8. **System Monitor** 🖥️
   - RS485 configuration
   - System status
   - Real-time updates

---

## 9. Key Functions Reference

### Modbus Functions (modbus.c)

```c
// Read holding registers
int modbus_read_holding_registers(
    uint8_t slave_id,
    uint16_t start_address,
    uint16_t quantity,
    uint16_t *data
);

// Read input registers
int modbus_read_input_registers(
    uint8_t slave_id,
    uint16_t start_address,
    uint16_t quantity,
    uint16_t *data
);

// Write single register
int modbus_write_single_register(
    uint8_t slave_id,
    uint16_t register_address,
    uint16_t value
);

// Write multiple registers
int modbus_write_multiple_registers(
    uint8_t slave_id,
    uint16_t start_address,
    uint16_t quantity,
    const uint16_t *values
);

// Data format conversion
float convert_modbus_data_to_float(
    const uint16_t *registers,
    data_format_t format
);

uint32_t convert_modbus_data_to_uint32(
    const uint16_t *registers,
    data_format_t format
);
```

### Sensor Manager Functions (sensor_manager.c)

```c
// Read all configured sensors
void read_all_sensors(
    sensor_config_t *sensors,
    int sensor_count,
    sensor_reading_t *readings
);

// Test individual sensor
esp_err_t test_sensor(
    sensor_config_t *sensor,
    char *result_buffer,
    size_t buffer_size
);

// Format test result for web display
void format_test_result(
    sensor_reading_t *reading,
    char *buffer,
    size_t size
);
```

### Azure Functions (main.c)

```c
// Generate SAS token
void generate_sas_token(
    const char *resource_uri,
    const char *device_key,
    time_t expiry,
    char *sas_token,
    size_t size
);

// MQTT event handler
void mqtt_event_handler(
    void *handler_args,
    esp_event_base_t base,
    int32_t event_id,
    void *event_data
);

// Publish telemetry
esp_err_t publish_telemetry(
    esp_mqtt_client_handle_t client,
    const char *json_payload
);
```

### Web Server Functions (web_config.c)

```c
// Main configuration page
esp_err_t config_page_handler(httpd_req_t *req);

// Save WiFi configuration
esp_err_t save_config_handler(httpd_req_t *req);

// Save Azure configuration
esp_err_t save_azure_config_handler(httpd_req_t *req);

// Add sensor
esp_err_t add_sensor_handler(httpd_req_t *req);

// Test sensor via AJAX
esp_err_t test_sensor_handler(httpd_req_t *req);

// Delete sensor
esp_err_t delete_sensor_handler(httpd_req_t *req);
```

### SD Card Functions (sd_card_logger.c)

```c
// Initialize SD card
esp_err_t sd_card_init(sd_config_t *config);

// Save message to cache
esp_err_t sd_cache_message(const char *json_payload);

// Replay cached messages
esp_err_t sd_replay_cached_messages(
    esp_mqtt_client_handle_t mqtt_client
);

// Clear cache
esp_err_t sd_clear_cache(void);

// Get SD card status
void sd_get_status(char *status_buffer, size_t size);
```

---

## 10. Build and Development

### ESP-IDF Build Commands

```bash
# Configure project
idf.py menuconfig

# Build firmware
idf.py build

# Flash to device
idf.py -p COM3 flash  # Windows
idf.py -p /dev/ttyUSB0 flash  # Linux

# Monitor serial output
idf.py monitor

# Flash and monitor
idf.py flash monitor

# Clean build
idf.py fullclean

# Check binary sizes
idf.py size

# Check component sizes
idf.py size-components

# Erase flash completely
idf.py erase-flash
```

### Partition Table (4MB Flash)

```csv
# Name,     Type, SubType, Offset,  Size,    Flags
nvs,        data, nvs,     0x9000,  0x6000,
phy_init,   data, phy,     0xf000,  0x1000,
factory,    app,  factory, 0x10000, 0x2F0000,
storage,    data, spiffs,  0x300000,0x100000,
```

### Configuration Options (menuconfig)

```
Component config → ESP32-specific
  - CPU frequency: 240MHz
  - Flash frequency: 80MHz
  - Flash mode: QIO
  - Flash size: 4MB

Component config → FreeRTOS
  - Tick rate: 1000Hz
  - Watchdog timeout: 5 seconds

Component config → LWIP
  - TCP MSS: 1436
  - TCP receive window: 5744

Component config → ESP HTTP Server
  - Max server instances: 1
  - Max URI handlers: 32
```

---

## 11. Troubleshooting Guide

### Common Issues and Solutions

#### 1. Modbus Communication Failure
```
Symptoms: "CRC error" or "Timeout" in test sensor results

Checks:
□ Verify RS485 A/B wiring (not swapped)
□ Check baud rate matches slave device
□ Verify slave ID is correct
□ Check register address and quantity
□ Measure RS485 voltage (A-B should be ~2-5V idle)
□ Add 120Ω termination resistors at bus ends
□ Check for bus contention (multiple masters)

Test command:
idf.py monitor
# Look for: "Modbus request sent" and "Response received"
```

#### 2. WiFi Connection Issues
```
Symptoms: Cannot connect to Azure, web interface slow

Checks:
□ Verify SSID and password are correct
□ Check WiFi signal strength (RSSI > -70 dBm)
□ Ensure router supports 802.11n
□ Check DHCP is enabled on router
□ Verify DNS servers are reachable
□ Check firewall allows outbound port 8883

Test command:
idf.py monitor
# Look for: "WiFi connected" and "IP address obtained"
```

#### 3. Azure IoT Hub Connection Failure
```
Symptoms: "MQTT connection refused" or "Authentication failed"

Checks:
□ Verify IoT Hub FQDN is correct
□ Check device ID matches Azure portal
□ Verify device key (primary or secondary)
□ Check SAS token expiry (regenerates every hour)
□ Verify device is enabled in Azure portal
□ Check time is synchronized (critical for SAS token)

Test command:
curl https://your-hub.azure-devices.net
# Should return 404 (means hub is reachable)
```

#### 4. SD Card Not Detected
```
Symptoms: "SD card initialization failed"

Checks:
□ Verify card is formatted as FAT32
□ Check SPI pin connections
□ Verify card is inserted correctly
□ Try different card (some cards incompatible)
□ Check power supply (3.3V stable)
□ Verify CS pin is correct

Test in web interface:
Network Config → SD Card → Check SD Card Status
```

#### 5. RTC Time Incorrect
```
Symptoms: Timestamps are wrong, time resets

Checks:
□ Verify I2C pin connections (SDA/SCL)
□ Check I2C address (should be 0x68)
□ Verify battery is installed (CR2032)
□ Check battery voltage (should be ~3V)
□ Sync from NTP first time

Test in web interface:
Network Config → RTC → Get RTC Time
```

---

## 12. Performance Optimization

### Memory Management

```c
// Use static buffers for frequently allocated data
static char mqtt_payload[2048];
static char json_buffer[4096];

// Free dynamic allocations promptly
sensor_reading_t *readings = malloc(sizeof(sensor_reading_t) * count);
// ... use readings ...
free(readings);

// Monitor heap usage
ESP_LOGI(TAG, "Free heap: %d bytes", esp_get_free_heap_size());
```

### Task Priorities

```
Priority 5: modbus_task (time-critical)
Priority 4: mqtt_task (network communication)
Priority 3: telemetry_task (data transmission)
Priority 1-2: Background tasks (WiFi, TCP/IP)
```

### Watchdog Configuration

```c
// Feed watchdog in long-running tasks
esp_task_wdt_reset();

// Adjust watchdog timeout if needed
esp_task_wdt_init(10, true);  // 10 seconds
```

---

## 13. Security Best Practices

### WiFi Security
```
✅ Use WPA2-PSK or WPA3
✅ Strong password (min 12 characters)
✅ Hide SSID if possible
❌ Never use WEP or open networks
```

### Azure IoT Hub Security
```
✅ Use device keys, not shared access policies
✅ Rotate device keys periodically
✅ Use per-device authentication
✅ Enable Azure IoT Hub diagnostics
❌ Never hardcode keys in firmware (use NVS)
```

### Web Interface Security
```
✅ Password protect sensitive sections (Azure config)
✅ Disable web server in operation mode
✅ Use HTTPS if possible (future enhancement)
❌ Never expose web interface to public internet
```

---

## 14. Extending the System

### Adding a New Sensor Type

```c
// 1. Define sensor type in sensor_manager.h
#define SENSOR_TYPE_CUSTOM "CUSTOM"

// 2. Add parsing in web_config.c
if (strcmp(sensor_type, "CUSTOM") == 0) {
    // Parse custom parameters
    config->custom_param1 = get_param("custom_param1");
}

// 3. Add reading logic in sensor_manager.c
if (strcmp(sensor->sensor_type, "CUSTOM") == 0) {
    // Custom reading logic
    reading->value = process_custom_sensor(raw_value);
}

// 4. Add telemetry formatting in json_templates.c
if (strcmp(sensor->sensor_type, "CUSTOM") == 0) {
    // Custom JSON formatting
    append_custom_telemetry(json, sensor, reading);
}
```

### Adding a New Communication Protocol

```c
// Example: Adding Modbus TCP support

// 1. Add TCP socket handling
int modbus_tcp_init(const char *ip, uint16_t port);

// 2. Implement Modbus TCP frame format (no CRC, has MBAP header)
typedef struct {
    uint16_t transaction_id;
    uint16_t protocol_id;  // Always 0 for Modbus
    uint16_t length;
    uint8_t unit_id;
    uint8_t function_code;
    uint8_t data[252];
} modbus_tcp_frame_t;

// 3. Update sensor configuration to include IP address
typedef struct {
    char ip_address[16];
    uint16_t port;
    // ... existing fields ...
} sensor_config_t;
```

---

## 15. Testing and Validation

### Unit Testing

```bash
# Test Modbus CRC calculation
idf.py monitor
# In monitor, send: test_crc

# Test sensor reading
# In web interface: Sensors → Test RS485

# Test JSON formatting
# Check serial monitor during telemetry send
```

### Integration Testing

```bash
# Full system test checklist:
□ Power on, enter config mode (GPIO 34 LOW)
□ Connect to WiFi AP "ModbusIoT-Config"
□ Access web interface http://192.168.4.1
□ Configure WiFi and Azure credentials
□ Add at least one sensor
□ Test sensor reading (should show value)
□ Save configuration
□ Release GPIO 34, restart
□ Verify WiFi connection to configured network
□ Verify MQTT connection to Azure IoT Hub
□ Check telemetry in Azure portal
□ Simulate network failure
□ Verify SD card caching (if enabled)
□ Restore network
□ Verify cached messages replay
```

### Performance Benchmarks

```
Typical metrics:
- Modbus read time: 50-200ms (depends on baud rate)
- MQTT publish time: 100-500ms (depends on network)
- Web page load time: 1-3 seconds
- Heap usage: 180-220KB free (out of 520KB)
- Task switching: ~1ms
- WiFi reconnection: 3-10 seconds
```

---

## 16. Production Deployment Checklist

### Pre-Deployment

```
□ Update WiFi credentials in iot_configs.h
□ Update Azure IoT Hub FQDN and device ID
□ Set appropriate telemetry interval (default: 120s)
□ Configure all sensors with correct parameters
□ Test all sensors in lab environment
□ Verify data appears in Azure IoT Hub
□ Flash firmware to production device
□ Test watchdog recovery
□ Test power cycle recovery
```

### Field Installation

```
□ Mount ESP32 in weatherproof enclosure
□ Connect RS485 A/B terminals to Modbus bus
□ Add 120Ω termination resistors
□ Connect power supply (stable 5V, min 2A)
□ Insert SD card (if using offline caching)
□ Install RTC battery (CR2032)
□ Route GPIO 34 to accessible switch (config mode)
□ Label all connections
□ Document installation date and location
```

### Post-Deployment Monitoring

```
□ Monitor Azure IoT Hub for regular telemetry
□ Check sensor values are within expected ranges
□ Monitor free heap memory (should be stable)
□ Check MQTT reconnection count (should be low)
□ Verify RTC time sync
□ Test config mode access
□ Schedule periodic maintenance (6-12 months)
```

---

## 17. Quick Reference Tables

### GPIO Pin Summary
| GPIO | Function | Direction | Notes |
|------|----------|-----------|-------|
| 16 | RS485 RX | Input | UART2 RX |
| 17 | RS485 TX | Output | UART2 TX |
| 4 | RS485 RTS | Output | Direction control |
| 34 | Config Trigger | Input | Pull LOW for config |
| 21 | RTC SDA | I/O | I2C data (default) |
| 22 | RTC SCL | Output | I2C clock (default) |
| 23 | SD MOSI | Output | SPI (default) |
| 19 | SD MISO | Input | SPI (default) |
| 18 | SD CLK | Output | SPI (default) |
| 5 | SD CS | Output | SPI (default) |
| 27 | SIM TX | Output | UART (default) |
| 26 | SIM RX | Input | UART (default) |

### Modbus Function Codes
| Code | Name | Description |
|------|------|-------------|
| 0x03 | Read Holding Registers | Read/write registers |
| 0x04 | Read Input Registers | Read-only registers |
| 0x06 | Write Single Register | Write one register |
| 0x10 | Write Multiple Registers | Write multiple registers |

### Data Format Quick Reference
| Format | Registers | Byte Order | Example Use Case |
|--------|-----------|------------|------------------|
| UINT16_BE | 1 | AB | Temperature sensors |
| INT32_ABCD | 2 | AB CD | Flow meters |
| FLOAT32_ABCD | 2 | AB CD | Pressure sensors |
| UINT32_DCBA | 2 | DC BA | Energy meters (little-endian) |

### Web Interface Endpoints
| Path | Method | Description |
|------|--------|-------------|
| / | GET | Main configuration page |
| /save_config | POST | Save WiFi configuration |
| /save_azure_config | POST | Save Azure credentials |
| /add_sensor | POST | Add new sensor |
| /edit_sensor | POST | Update sensor config |
| /delete_sensor | POST | Remove sensor |
| /test_sensor | POST | Test sensor reading |
| /scan_wifi | GET | Scan WiFi networks |
| /save_network_mode | POST | Change network mode |
| /save_sd_config | POST | Save SD card config |
| /save_rtc_config | POST | Save RTC config |

---

## 18. Related Documentation

- [ESP-IDF Documentation](https://docs.espressif.com/projects/esp-idf/en/latest/)
- [Modbus Protocol Specification](https://modbus.org/docs/Modbus_Application_Protocol_V1_1b3.pdf)
- [Azure IoT Hub MQTT Support](https://docs.microsoft.com/en-us/azure/iot-hub/iot-hub-mqtt-support)
- [DS3231 RTC Datasheet](https://datasheets.maximintegrated.com/en/ds/DS3231.pdf)
- [MAX485 RS485 Transceiver](https://www.analog.com/media/en/technical-documentation/data-sheets/MAX1487-MAX491.pdf)

---

## Conclusion

This reference covers the complete ESP32 Modbus IoT Gateway system. Use it as a quick reference during development, troubleshooting, and deployment. For specific implementation details, refer to the source code and inline comments.

**Key Takeaways:**
- Modular architecture with FreeRTOS tasks
- Industrial-grade RS485 Modbus communication
- Azure IoT Hub cloud integration
- Offline caching with SD card
- Accurate timekeeping with RTC
- Professional web configuration interface
- Supports 16 data formats and multiple sensor types
- Production-ready with comprehensive error handling

**Version**: 1.1.0-final
**Last Updated**: 2025-01-12
**Maintained By**: Fluxgen - Building a Water Positive Future
