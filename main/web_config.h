// web_config.h - Web-based configuration interface for multi-sensor Modbus system

#ifndef WEB_CONFIG_H
#define WEB_CONFIG_H

#include "esp_err.h"
#include "esp_http_server.h"
#include <stdbool.h>
#include "driver/gpio.h"
#include "driver/uart.h"

// Configuration states
typedef enum {
    CONFIG_STATE_SETUP,      // Initial setup mode (STA for configuration)
    CONFIG_STATE_OPERATION   // Normal operation mode (AP mode)
} config_state_t;

// Network mode selection
typedef enum {
    NETWORK_MODE_WIFI,       // Use WiFi connectivity (default)
    NETWORK_MODE_SIM         // Use SIM module (A7670C) connectivity
} network_mode_t;

// ============================================================================
// CALCULATION ENGINE - User-friendly calculated fields for complex sensors
// ============================================================================

// Calculation types - user selects from dropdown, no coding required
typedef enum {
    CALC_NONE = 0,                    // Direct value, no calculation
    CALC_COMBINE_REGISTERS,           // HIGH × multiplier + LOW (e.g., Vortex flowmeter)
    CALC_SCALE_OFFSET,                // (value × scale) + offset
    CALC_LEVEL_PERCENTAGE,            // ((max - value) / range) × 100
    CALC_CYLINDER_VOLUME,             // π × r² × level (cylindrical tank)
    CALC_RECTANGLE_VOLUME,            // L × W × level (rectangular tank)
    CALC_DIFFERENCE,                  // value1 - value2 (differential)
    CALC_FLOW_RATE_PULSE,             // pulses × factor / time
    CALC_LINEAR_INTERPOLATION,        // Map input range to output range
    CALC_POLYNOMIAL,                  // ax² + bx + c (for non-linear sensors)
    CALC_FLOW_INT_DECIMAL,            // Integer + Decimal from two registers (flow meters)
    CALC_TYPE_MAX
} calculation_type_t;

// Human-readable names for calculation types (for web UI)
// Defined in sensor_manager.c
extern const char* CALC_TYPE_NAMES[];
extern const char* CALC_TYPE_DESCRIPTIONS[];

// Calculation parameters structure - fields used depend on calc_type
typedef struct {
    calculation_type_t calc_type;     // Type of calculation to apply

    // For CALC_COMBINE_REGISTERS (e.g., Vortex: cumulative = HIGH×100 + LOW)
    int high_register_offset;         // Offset from base register for HIGH value
    int low_register_offset;          // Offset from base register for LOW value
    float combine_multiplier;         // Multiplier for HIGH value (default: 100)

    // For CALC_SCALE_OFFSET
    float scale;                      // Multiplier
    float offset;                     // Added after scaling

    // For CALC_LEVEL_PERCENTAGE
    float tank_empty_value;           // Sensor reading when tank is empty
    float tank_full_value;            // Sensor reading when tank is full
    bool invert_level;                // true if sensor reads higher when empty

    // For CALC_CYLINDER_VOLUME / CALC_RECTANGLE_VOLUME
    float tank_diameter;              // Diameter in meters (cylinder)
    float tank_length;                // Length in meters (rectangle)
    float tank_width;                 // Width in meters (rectangle)
    float tank_height;                // Total tank height in meters
    int volume_unit;                  // 0=liters, 1=m³, 2=gallons

    // For CALC_DIFFERENCE
    int secondary_sensor_index;       // Index of second sensor for difference calc

    // For CALC_FLOW_RATE_PULSE
    float pulses_per_unit;            // Pulses per liter/m³

    // For CALC_LINEAR_INTERPOLATION
    float input_min;                  // Input range minimum
    float input_max;                  // Input range maximum
    float output_min;                 // Output range minimum
    float output_max;                 // Output range maximum

    // For CALC_POLYNOMIAL (y = ax² + bx + c)
    float poly_a;                     // Quadratic coefficient
    float poly_b;                     // Linear coefficient
    float poly_c;                     // Constant

    // Output configuration
    char output_unit[16];             // Unit for display (m³, L, %, etc.)
    int decimal_places;               // Number of decimal places (0-6)

} calculation_params_t;

// Sub-sensor for water quality parameters
typedef struct {
    bool enabled;
    char parameter_name[32];   // pH, Temperature, TDS, etc.
    char json_key[16];         // JSON field name: "pH", "temp", "tds", etc.
    int slave_id;
    int register_address;
    int quantity;
    char data_type[32];        // INT32, UINT16, FLOAT32, FLOAT64_78563412, etc. - increased for 64-bit formats
    char register_type[16];    // HOLDING, INPUT
    float scale_factor;
    char byte_order[16];       // BIG_ENDIAN, LITTLE_ENDIAN, etc.
    char units[16];            // pH, degC, ppm, etc.
} sub_sensor_t;

// Sensor configuration structure
typedef struct {
    bool enabled;
    char name[32];
    char unit_id[16];
    int slave_id;
    int baud_rate;
    char parity[8];            // none, even, odd
    int register_address;
    int quantity;
    char data_type[32];        // INT32, UINT16, FLOAT32, FLOAT64_78563412, etc. - increased for 64-bit formats
    char register_type[16];    // HOLDING, INPUT
    float scale_factor;
    char byte_order[16];       // BIG_ENDIAN, LITTLE_ENDIAN, etc.
    char description[64];
    
    // Sensor type and Level-specific fields
    char sensor_type[24];      // "Flow-Meter", "Level", "ENERGY", "QUALITY", "Hydrostatic_Level", etc.
    float sensor_height;       // For Level sensors: physical sensor height
    float max_water_level;     // For Level sensors: maximum water level for calculation
    char meter_type[32];       // For ENERGY sensors: meter type identifier
    
    // Sub-sensors for water quality sensors only
    sub_sensor_t sub_sensors[8]; // Up to 8 sub-sensors per water quality sensor
    int sub_sensor_count;

    // Calculation engine - user-friendly calculations without coding
    calculation_params_t calculation;  // Calculation settings for this sensor
} sensor_config_t;

// SIM module configuration (A7670C)
typedef struct {
    bool enabled;              // Enable/disable SIM module
    char apn[64];             // APN for cellular carrier (e.g., "airteliot")
    char apn_user[32];        // APN username (usually empty)
    char apn_pass[32];        // APN password (usually empty)
    gpio_num_t uart_tx_pin;   // UART TX pin - ESP32 TX -> A7670C RX (default: GPIO 33)
    gpio_num_t uart_rx_pin;   // UART RX pin - ESP32 RX <- A7670C TX (default: GPIO 32)
    gpio_num_t pwr_pin;       // Power control pin (default: GPIO 4)
    gpio_num_t reset_pin;     // Reset pin (default: GPIO 15)
    uart_port_t uart_num;     // UART port number (default: UART_NUM_1)
    int uart_baud_rate;       // Baud rate (default: 115200)
} sim_module_config_t;

// SD card configuration
typedef struct {
    bool enabled;              // Enable/disable SD card logging
    bool cache_on_failure;     // Cache data to SD when network fails
    gpio_num_t mosi_pin;       // SPI MOSI pin (default: GPIO 13)
    gpio_num_t miso_pin;       // SPI MISO pin (default: GPIO 12)
    gpio_num_t clk_pin;        // SPI CLK pin (default: GPIO 14)
    gpio_num_t cs_pin;         // SPI CS pin (default: GPIO 5)
    int spi_host;              // SPI host (default: SPI2_HOST)
    int max_message_size;      // Maximum message size (default: 512)
    int min_free_space_mb;     // Minimum free space in MB (default: 1)
} sd_card_config_t;

// RTC configuration
typedef struct {
    bool enabled;              // Enable/disable RTC
    gpio_num_t sda_pin;       // I2C SDA pin (default: GPIO 21)
    gpio_num_t scl_pin;       // I2C SCL pin (default: GPIO 22)
    int i2c_num;              // I2C port number (default: I2C_NUM_0)
    bool sync_on_boot;        // Sync RTC from NTP on boot (if network available)
    bool update_from_ntp;     // Periodically update RTC from NTP
} rtc_config_t;

// Telegram Bot configuration
typedef struct {
    bool enabled;              // Enable/disable Telegram bot
    char bot_token[64];       // Bot API token from @BotFather
    char chat_id[32];         // Your Telegram chat ID (user or group)
    bool alerts_enabled;      // Enable/disable automatic alerts
    bool startup_notification; // Send notification on system startup
    int poll_interval;        // Polling interval in seconds (default: 10)
} telegram_config_t;

// System configuration
typedef struct {
    // Network configuration
    network_mode_t network_mode;  // WiFi or SIM mode
    char wifi_ssid[32];
    char wifi_password[64];
    sim_module_config_t sim_config;

    // Cloud configuration
    char azure_hub_fqdn[128];
    char azure_device_id[32];
    char azure_device_key[128];
    int telemetry_interval;    // in seconds

    // Sensor configuration
    sensor_config_t sensors[10]; // Reduced from 15 to 10 sensors to save ~7.8KB heap
    int sensor_count;

    // Optional features
    sd_card_config_t sd_config;
    rtc_config_t rtc_config;
    telegram_config_t telegram_config;

    // System flags
    bool config_complete;
    bool modem_reset_enabled;  // Enable/disable modem reset on MQTT disconnect
    int modem_boot_delay;      // Delay in seconds to wait for modem boot after reset
    int modem_reset_gpio_pin;  // GPIO pin for modem reset control
    int trigger_gpio_pin;      // GPIO pin for configuration mode trigger (default: 34)

    // Telemetry options
    bool batch_telemetry;      // Send all sensors in single JSON message (default: true)

    // Modbus retry settings
    int modbus_retry_count;    // Number of retries on failure (0-3, default: 1)
    int modbus_retry_delay;    // Delay between retries in ms (default: 50)

    // Device Twin settings
    int device_twin_version;   // Track last applied desired properties version
} system_config_t;

// Function prototypes
esp_err_t web_config_init(void);
esp_err_t web_config_start_sta_mode(void);
esp_err_t web_config_start_ap_mode(void);
esp_err_t web_config_stop(void);
esp_err_t web_config_start_server_only(void);

// Configuration management
esp_err_t config_load_from_nvs(system_config_t *config);
esp_err_t config_save_to_nvs(const system_config_t *config);
esp_err_t config_reset_sensor_calculations(void);
esp_err_t config_reset_to_defaults(void);

// Sensor testing
esp_err_t test_sensor_connection(const sensor_config_t *sensor, char *result_buffer, size_t buffer_size);

// Sub-sensor management for water quality sensors
esp_err_t add_sub_sensor_to_quality_sensor(int sensor_index, const sub_sensor_t *sub_sensor);
esp_err_t delete_sub_sensor_from_quality_sensor(int sensor_index, int sub_sensor_index);

// Get current configuration
system_config_t* get_system_config(void);
config_state_t get_config_state(void);
void set_config_state(config_state_t state);
bool web_config_needs_auto_start(void);

// Calculation engine functions (defined in sensor_manager.c)
double apply_calculation(const sensor_config_t *sensor, double raw_value,
                        const uint16_t *all_registers, int register_count);
const char* get_calculation_type_name(calculation_type_t type);
const char* get_calculation_type_description(calculation_type_t type);
void init_default_calculation_params(calculation_params_t *params);

// Modem GPIO control
esp_err_t update_modem_gpio_pin(int new_gpio_pin);

// WiFi network connection
esp_err_t connect_to_wifi_network(void);

// WiFi periodic reconnection (called from telemetry task when network unavailable)
bool wifi_trigger_reconnect(void);

// Telemetry history (for web interface display)
int get_telemetry_history_json(char *buffer, size_t buffer_size);

#endif // WEB_CONFIG_H