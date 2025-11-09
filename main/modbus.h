// modbus.h - Modbus communication functions
// Production-ready RS485 Modbus implementation for Azure IoT

#ifndef MODBUS_H
#define MODBUS_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

// Modbus Function Codes
#define MODBUS_READ_HOLDING_REGISTERS 0x03
#define MODBUS_READ_INPUT_REGISTERS 0x04
#define MODBUS_WRITE_SINGLE_REGISTER 0x06
#define MODBUS_WRITE_MULTIPLE_REGISTERS 0x10

// Modbus Constants
#define MODBUS_MAX_REGISTERS 125
#define MODBUS_MAX_BUFFER_SIZE 256

// Hardware Configuration
#define RS485_UART_PORT UART_NUM_2
#define RS485_BAUD_RATE 9600
#define RS485_BUF_SIZE 2048
#define MODBUS_RESPONSE_TIMEOUT_MS 1000
#define RXD2 GPIO_NUM_16
#define TXD2 GPIO_NUM_17
#define RS485_RTS_PIN GPIO_NUM_18  // Changed from GPIO_NUM_32 to avoid conflict with SIM RX pin

// Modbus Result Codes
typedef enum {
    MODBUS_SUCCESS = 0,
    MODBUS_ILLEGAL_FUNCTION = 0x01,
    MODBUS_ILLEGAL_DATA_ADDRESS = 0x02,
    MODBUS_ILLEGAL_DATA_VALUE = 0x03,
    MODBUS_SLAVE_DEVICE_FAILURE = 0x04,
    MODBUS_ACKNOWLEDGE = 0x05,
    MODBUS_SLAVE_DEVICE_BUSY = 0x06,
    MODBUS_INVALID_RESPONSE = 0xE0,
    MODBUS_TIMEOUT = 0xE1,
    MODBUS_INVALID_CRC = 0xE2
} modbus_result_t;

// Flow Meter Configuration
typedef struct {
    int slave_id;
    int register_address;
    int register_length;
    char data_type[16];
    char sensor_type[32];
    char unit_id[32];
    float scale_factor;
    int multiplier_register;
} meter_config_t;

// Flow Meter Data Structure
typedef struct {
    double totalizer_value;
    double flow_rate;
    uint32_t raw_totalizer;
    float raw_flow_rate;
    int multiplier;
    char timestamp[32];
    bool data_valid;
    uint32_t last_read_time;
} flow_meter_data_t;

// Statistics Structure
typedef struct {
    uint32_t total_requests;
    uint32_t successful_requests;
    uint32_t failed_requests;
    uint32_t timeout_errors;
    uint32_t crc_errors;
    uint32_t last_error_code;
} modbus_stats_t;

// Function Prototypes
esp_err_t modbus_init(void);
esp_err_t modbus_set_baud_rate(int baud_rate);
void modbus_deinit(void);

// Read Functions
modbus_result_t modbus_read_holding_registers(uint8_t slave_id, uint16_t start_addr, uint16_t num_regs);
modbus_result_t modbus_read_input_registers(uint8_t slave_id, uint16_t start_addr, uint16_t num_regs);

// Write Functions
modbus_result_t modbus_write_single_register(uint8_t slave_id, uint16_t addr, uint16_t value);
modbus_result_t modbus_write_multiple_registers(uint8_t slave_id, uint16_t start_addr, uint16_t num_regs, const uint16_t* values);

// Response Buffer Functions
uint16_t modbus_get_response_buffer(uint8_t index);
uint8_t modbus_get_response_length(void);
void modbus_clear_response_buffer(void);

// Utility Functions
uint16_t modbus_calculate_crc(const uint8_t* data, size_t length);
bool modbus_verify_crc(const uint8_t* data, size_t length);

// Statistics Functions
void modbus_get_statistics(modbus_stats_t* stats);
void modbus_reset_statistics(void);

// Flow Meter Functions
esp_err_t flow_meter_read_data(const meter_config_t* config, flow_meter_data_t* data);
void flow_meter_print_data(const flow_meter_data_t* data);

#endif // MODBUS_H