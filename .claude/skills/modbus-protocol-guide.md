# Modbus RTU Protocol - Complete Implementation Guide

## Purpose
Master Modbus RTU protocol implementation for industrial sensor communication over RS485 with the ESP32.

## When to Use
- Implementing Modbus master/client functionality
- Adding support for new Modbus devices
- Troubleshooting communication errors
- Understanding data format conversions
- Optimizing Modbus performance

---

## 1. Modbus RTU Fundamentals

### Protocol Overview

**Modbus RTU** (Remote Terminal Unit) is a serial communication protocol widely used in industrial automation.

```
Key Characteristics:
- Master-Slave architecture (one master, multiple slaves)
- Half-duplex communication (RS485)
- Binary data format
- CRC16 error checking
- Maximum frame size: 256 bytes
- Typical baud rates: 9600, 19200, 38400, 57600, 115200
```

### Frame Structure

```
┌────────┬──────────┬────────┬─────────┬──────┐
│ Slave  │ Function │  Data  │   CRC   │      │
│   ID   │   Code   │  (N)   │ (2 bytes│ RTU  │
│(1 byte)│ (1 byte) │ bytes  │    )    │Frame │
└────────┴──────────┴────────┴─────────┴──────┘
```

**Frame Components:**
1. **Slave ID** (1 byte): Device address (1-247, 0 = broadcast)
2. **Function Code** (1 byte): Operation to perform (0x01-0x7F)
3. **Data** (N bytes): Register addresses, values, quantity
4. **CRC** (2 bytes): Error checking (CRC-16-ANSI, polynomial 0xA001)

---

## 2. Function Codes

### Read Operations

#### Function Code 0x03: Read Holding Registers
```
Request Frame:
┌───────┬────┬──────────┬──────────┬─────┐
│ Slave │ FC │ Start    │ Quantity │ CRC │
│  ID   │0x03│ Address  │          │     │
│  (1)  │ (1)│   (2)    │   (2)    │ (2) │
└───────┴────┴──────────┴──────────┴─────┘

Response Frame:
┌───────┬────┬──────┬────────────┬─────┐
│ Slave │ FC │ Byte │   Data     │ CRC │
│  ID   │0x03│Count │  (N bytes) │     │
│  (1)  │ (1)│  (1) │            │ (2) │
└───────┴────┴──────┴────────────┴─────┘

Example: Read 2 registers starting at address 0x0064 from slave 1
Request:  01 03 00 64 00 02 [CRC]
Response: 01 03 04 12 34 56 78 [CRC]
         └─┬─┘ └─┬┘ └──┬──┘ └──┬──┘
          Slave  4 bytes  Reg1   Reg2
                 (2 regs)
```

#### Function Code 0x04: Read Input Registers
```
Same format as 0x03, but reads input registers (read-only)

Request:  01 04 00 64 00 02 [CRC]
Response: 01 04 04 12 34 56 78 [CRC]
```

### Write Operations

#### Function Code 0x06: Write Single Register
```
Request Frame:
┌───────┬────┬──────────┬──────────┬─────┐
│ Slave │ FC │ Register │  Value   │ CRC │
│  ID   │0x06│ Address  │ (2 bytes)│     │
│  (1)  │ (1)│   (2)    │          │ (2) │
└───────┴────┴──────────┴──────────┴─────┘

Response Frame (Echo):
Same as request

Example: Write value 0x1234 to register 0x0064 of slave 1
Request:  01 06 00 64 12 34 [CRC]
Response: 01 06 00 64 12 34 [CRC] (echo)
```

#### Function Code 0x10: Write Multiple Registers
```
Request Frame:
┌───────┬────┬──────────┬──────────┬──────┬────────────┬─────┐
│ Slave │ FC │ Start    │ Quantity │ Byte │   Data     │ CRC │
│  ID   │0x10│ Address  │          │Count │  (N bytes) │     │
│  (1)  │ (1)│   (2)    │   (2)    │  (1) │            │ (2) │
└───────┴────┴──────────┴──────────┴──────┴────────────┴─────┘

Response Frame:
┌───────┬────┬──────────┬──────────┬─────┐
│ Slave │ FC │ Start    │ Quantity │ CRC │
│  ID   │0x10│ Address  │          │     │
│  (1)  │ (1)│   (2)    │   (2)    │ (2) │
└───────┴────┴──────────┴──────────┴─────┘

Example: Write 2 registers starting at 0x0064
Request:  01 10 00 64 00 02 04 12 34 56 78 [CRC]
         └─┬─┘ └──┬──┘ └─┬─┘ └┬┘ └──┬──┘ └──┬──┘
          Slave   Start   Qty  4 bytes  Val1   Val2
Response: 01 10 00 64 00 02 [CRC]
```

### Error Responses (Exception Codes)

```
Exception Frame:
┌───────┬──────────┬──────────┬─────┐
│ Slave │ Function │Exception │ CRC │
│  ID   │Code + 0x80│  Code   │     │
│  (1)  │   (1)    │   (1)    │ (2) │
└───────┴──────────┴──────────┴─────┘

Common Exception Codes:
0x01 - Illegal Function
0x02 - Illegal Data Address
0x03 - Illegal Data Value
0x04 - Slave Device Failure
0x05 - Acknowledge (long operation in progress)
0x06 - Slave Device Busy
0x08 - Memory Parity Error

Example: Slave 1 returns exception for illegal address
Request:  01 03 FF FF 00 01 [CRC]  (read invalid address)
Response: 01 83 02 [CRC]           (FC=0x83, Exception=0x02)
         └─┬─┘ └┬┘ └┬┘
          Slave  0x03+0x80  Illegal Address
```

---

## 3. CRC16 Calculation

### Algorithm (Modbus Standard)

```c
uint16_t calculate_crc16(const uint8_t *data, size_t length) {
    uint16_t crc = 0xFFFF;  // Initial value

    for (size_t i = 0; i < length; i++) {
        crc ^= data[i];     // XOR byte into CRC

        for (int j = 0; j < 8; j++) {
            if (crc & 0x0001) {
                crc = (crc >> 1) ^ 0xA001;  // Polynomial
            } else {
                crc >>= 1;
            }
        }
    }

    return crc;
}
```

### CRC Properties

```
Polynomial: 0xA001 (reversed 0x8005)
Initial value: 0xFFFF
Final XOR: None
Byte order: Low byte first, high byte second

Example:
Data: 01 03 00 64 00 02
CRC = 0x850B
Frame: 01 03 00 64 00 02 0B 85
                         └─┬─┘
                          CRC
```

### Optimized CRC (Lookup Table)

```c
// Pre-calculated CRC table
static const uint16_t crc_table[256] = {
    0x0000, 0xC0C1, 0xC181, 0x0140, 0xC301, 0x03C0, 0x0280, 0xC241,
    // ... (256 entries)
};

uint16_t calculate_crc16_fast(const uint8_t *data, size_t length) {
    uint16_t crc = 0xFFFF;

    for (size_t i = 0; i < length; i++) {
        uint8_t index = crc ^ data[i];
        crc = (crc >> 8) ^ crc_table[index];
    }

    return crc;
}
```

---

## 4. Data Format Conversions

### 16-bit Formats (1 Register)

```c
// UINT16_BE (Big Endian) - Most common
uint16_t value_be = (registers[0] & 0xFFFF);

// UINT16_LE (Little Endian) - Swap bytes
uint16_t value_le = ((registers[0] & 0xFF) << 8) | ((registers[0] >> 8) & 0xFF);

// INT16 (Signed) - Two's complement
int16_t value_signed = (int16_t)registers[0];

Example:
Register: 0x1234
UINT16_BE = 4660 (0x1234)
UINT16_LE = 13330 (0x3412)
INT16_BE  = 4660 (positive)
```

### 32-bit Integer Formats (2 Registers)

```c
// UINT32_ABCD (Big Endian)
// Registers: [0x1234] [0x5678]
uint32_t value_abcd = ((uint32_t)registers[0] << 16) | registers[1];
// Result: 0x12345678

// UINT32_DCBA (Little Endian)
// Registers: [0x1234] [0x5678]
uint32_t reg0_swap = ((registers[0] & 0xFF) << 8) | ((registers[0] >> 8) & 0xFF);
uint32_t reg1_swap = ((registers[1] & 0xFF) << 8) | ((registers[1] >> 8) & 0xFF);
uint32_t value_dcba = (reg1_swap << 16) | reg0_swap;
// Result: 0x78563412

// UINT32_BADC (Mid-Big Endian)
// Registers: [0x1234] [0x5678]
uint32_t value_badc = ((uint32_t)registers[1] << 16) | registers[0];
// Result: 0x56781234

// UINT32_CDAB (Mid-Little Endian)
// Registers: [0x1234] [0x5678]
uint32_t reg0_swap = ((registers[0] & 0xFF) << 8) | ((registers[0] >> 8) & 0xFF);
uint32_t reg1_swap = ((registers[1] & 0xFF) << 8) | ((registers[1] >> 8) & 0xFF);
uint32_t value_cdab = (reg0_swap << 16) | reg1_swap;
// Result: 0x34127856
```

### 32-bit Float Formats (2 Registers - IEEE 754)

```c
// FLOAT32_ABCD (Big Endian)
uint32_t raw = ((uint32_t)registers[0] << 16) | registers[1];
float value;
memcpy(&value, &raw, sizeof(float));

// FLOAT32_DCBA (Little Endian)
uint32_t reg0_swap = ((registers[0] & 0xFF) << 8) | ((registers[0] >> 8) & 0xFF);
uint32_t reg1_swap = ((registers[1] & 0xFF) << 8) | ((registers[1] >> 8) & 0xFF);
uint32_t raw = (reg1_swap << 16) | reg0_swap;
float value;
memcpy(&value, &raw, sizeof(float));

// FLOAT32_BADC (Mid-Big Endian)
uint32_t raw = ((uint32_t)registers[1] << 16) | registers[0];
float value;
memcpy(&value, &raw, sizeof(float));

// FLOAT32_CDAB (Mid-Little Endian)
uint32_t reg0_swap = ((registers[0] & 0xFF) << 8) | ((registers[0] >> 8) & 0xFF);
uint32_t reg1_swap = ((registers[1] & 0xFF) << 8) | ((registers[1] >> 8) & 0xFF);
uint32_t raw = (reg0_swap << 16) | reg1_swap;
float value;
memcpy(&value, &raw, sizeof(float));
```

### Scaling and Offset

```c
// Apply scale factor and offset
float final_value = (raw_value * scale_factor) + offset;

// Example: Temperature sensor
// Raw = 235, Scale = 0.1, Offset = -50
float temperature = (235 * 0.1) + (-50) = -26.5°C
```

---

## 5. RS485 Hardware Control

### Direction Control (RTS)

```c
#define RS485_RTS_PIN GPIO_NUM_4

// Transmit mode (before sending)
gpio_set_level(RS485_RTS_PIN, 1);
uart_write_bytes(UART_NUM_2, frame, frame_length);
uart_wait_tx_done(UART_NUM_2, pdMS_TO_TICKS(100));

// Receive mode (after sending)
gpio_set_level(RS485_RTS_PIN, 0);
```

### UART Configuration

```c
uart_config_t uart_config = {
    .baud_rate = 9600,              // Typical Modbus baud rate
    .data_bits = UART_DATA_8_BITS,  // Always 8 for Modbus
    .parity = UART_PARITY_DISABLE,  // Usually no parity
    .stop_bits = UART_STOP_BITS_1,  // 1 or 2 stop bits
    .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
    .source_clk = UART_SCLK_DEFAULT,
};

uart_param_config(UART_NUM_2, &uart_config);
uart_set_pin(UART_NUM_2, RS485_TX_PIN, RS485_RX_PIN,
             UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
uart_driver_install(UART_NUM_2, 1024, 1024, 0, NULL, 0);

// Enable RS485 half-duplex mode
uart_set_mode(UART_NUM_2, UART_MODE_RS485_HALF_DUPLEX);
```

### Timing Considerations

```
Character time (T): Time to transmit one character (11 bits at baud rate)
T = 11 bits / baud_rate

Inter-character timeout: 1.5T (max gap between characters in frame)
Inter-frame delay: 3.5T (min gap between frames)

Example at 9600 baud:
T = 11 / 9600 = 1.146ms
Inter-character timeout = 1.7ms
Inter-frame delay = 4.0ms
```

---

## 6. Complete Implementation Example

### Modbus Master Implementation

```c
#include "driver/uart.h"
#include "driver/gpio.h"

#define UART_NUM          UART_NUM_2
#define RS485_TX_PIN      GPIO_NUM_17
#define RS485_RX_PIN      GPIO_NUM_16
#define RS485_RTS_PIN     GPIO_NUM_4
#define BUF_SIZE          256
#define MODBUS_TIMEOUT_MS 1000

// CRC16 calculation
uint16_t modbus_crc16(const uint8_t *data, size_t length) {
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

// Read holding registers (FC 0x03)
int modbus_read_holding_registers(uint8_t slave_id, uint16_t start_address,
                                   uint16_t quantity, uint16_t *data) {
    // Build request frame
    uint8_t request[8];
    request[0] = slave_id;
    request[1] = 0x03;  // Function code
    request[2] = (start_address >> 8) & 0xFF;
    request[3] = start_address & 0xFF;
    request[4] = (quantity >> 8) & 0xFF;
    request[5] = quantity & 0xFF;

    // Calculate CRC
    uint16_t crc = modbus_crc16(request, 6);
    request[6] = crc & 0xFF;
    request[7] = (crc >> 8) & 0xFF;

    // Clear UART buffer
    uart_flush(UART_NUM);

    // Transmit request
    gpio_set_level(RS485_RTS_PIN, 1);
    uart_write_bytes(UART_NUM, request, 8);
    uart_wait_tx_done(UART_NUM, pdMS_TO_TICKS(100));
    gpio_set_level(RS485_RTS_PIN, 0);

    // Wait for response
    uint8_t response[BUF_SIZE];
    int expected_length = 5 + (quantity * 2);  // Header + data + CRC
    int len = uart_read_bytes(UART_NUM, response, expected_length,
                              pdMS_TO_TICKS(MODBUS_TIMEOUT_MS));

    if (len < expected_length) {
        return -1;  // Timeout or incomplete response
    }

    // Validate CRC
    uint16_t received_crc = response[len-2] | (response[len-1] << 8);
    uint16_t calculated_crc = modbus_crc16(response, len - 2);

    if (received_crc != calculated_crc) {
        return -2;  // CRC error
    }

    // Check for exception
    if (response[1] & 0x80) {
        return -3 - response[2];  // Exception code (negative)
    }

    // Extract data
    uint8_t byte_count = response[2];
    for (int i = 0; i < quantity; i++) {
        data[i] = (response[3 + i*2] << 8) | response[4 + i*2];
    }

    return quantity;  // Success
}

// Write single register (FC 0x06)
int modbus_write_single_register(uint8_t slave_id, uint16_t register_address,
                                  uint16_t value) {
    // Build request frame
    uint8_t request[8];
    request[0] = slave_id;
    request[1] = 0x06;  // Function code
    request[2] = (register_address >> 8) & 0xFF;
    request[3] = register_address & 0xFF;
    request[4] = (value >> 8) & 0xFF;
    request[5] = value & 0xFF;

    // Calculate CRC
    uint16_t crc = modbus_crc16(request, 6);
    request[6] = crc & 0xFF;
    request[7] = (crc >> 8) & 0xFF;

    // Transmit request
    uart_flush(UART_NUM);
    gpio_set_level(RS485_RTS_PIN, 1);
    uart_write_bytes(UART_NUM, request, 8);
    uart_wait_tx_done(UART_NUM, pdMS_TO_TICKS(100));
    gpio_set_level(RS485_RTS_PIN, 0);

    // Wait for response (echo)
    uint8_t response[8];
    int len = uart_read_bytes(UART_NUM, response, 8,
                              pdMS_TO_TICKS(MODBUS_TIMEOUT_MS));

    if (len < 8) {
        return -1;  // Timeout
    }

    // Validate CRC
    uint16_t received_crc = response[6] | (response[7] << 8);
    uint16_t calculated_crc = modbus_crc16(response, 6);

    if (received_crc != calculated_crc) {
        return -2;  // CRC error
    }

    // Check for exception
    if (response[1] & 0x80) {
        return -3 - response[2];  // Exception code
    }

    // Verify echo
    if (memcmp(request, response, 8) != 0) {
        return -4;  // Invalid response
    }

    return 0;  // Success
}
```

---

## 7. Troubleshooting Modbus Issues

### No Response from Slave

```
Checklist:
□ Verify RS485 A/B wiring (try swapping if unsure)
□ Check slave ID is correct
□ Verify baud rate matches slave device
□ Check parity and stop bits configuration
□ Ensure RS485 transceiver is powered
□ Measure voltage on A-B terminals (should be 2-5V idle)
□ Check RTS timing (must switch before/after transmission)
□ Add delay after RTS switch (2-5ms)
□ Verify UART TX/RX pins are correct
□ Check for bus contention (only one master)
```

### CRC Errors

```
Checklist:
□ Verify CRC calculation algorithm
□ Check byte order (CRC low byte first)
□ Verify polynomial (0xA001 for Modbus)
□ Check for noise on RS485 bus
□ Add termination resistors (120Ω at both ends)
□ Reduce cable length if possible
□ Use shielded twisted pair cable
□ Verify UART baud rate accuracy
□ Check for buffer overruns
```

### Timeouts

```
Checklist:
□ Increase timeout value (try 2-3 seconds)
□ Check slave device response time
□ Verify inter-frame delay (3.5T minimum)
□ Check for slow slave devices
□ Reduce Modbus request frequency
□ Verify slave device is powered and operational
□ Check for bus loading (too many devices)
```

### Exception Responses

```
0x01 - Illegal Function
  → Verify function code is supported by device
  → Check device manual for supported functions

0x02 - Illegal Data Address
  → Verify register address exists in device
  → Check address range in device documentation
  → Some devices use 0-based addressing, others 1-based

0x03 - Illegal Data Value
  → Verify quantity is within allowed range
  → Check value range for write operations
  → Quantity for 32-bit values should be 2 registers

0x04 - Slave Device Failure
  → Check device status and error indicators
  → Power cycle the slave device
  → Check device logs if available
```

---

## 8. Best Practices

### Communication Optimization

```c
// 1. Batch register reads when possible
// Bad: Read 4 registers with 4 requests
modbus_read_holding_registers(1, 100, 1, &data1);
modbus_read_holding_registers(1, 101, 1, &data2);
modbus_read_holding_registers(1, 102, 1, &data3);
modbus_read_holding_registers(1, 103, 1, &data4);

// Good: Read 4 registers with 1 request
modbus_read_holding_registers(1, 100, 4, data_array);

// 2. Add inter-frame delay
void modbus_delay(void) {
    vTaskDelay(pdMS_TO_TICKS(5));  // 5ms delay
}

// 3. Retry on failure with exponential backoff
int retries = 3;
int delay_ms = 100;
for (int i = 0; i < retries; i++) {
    int result = modbus_read_holding_registers(...);
    if (result >= 0) break;
    vTaskDelay(pdMS_TO_TICKS(delay_ms));
    delay_ms *= 2;  // Exponential backoff
}
```

### Error Handling

```c
// Always check return values
int result = modbus_read_holding_registers(slave_id, address, qty, data);

if (result < 0) {
    if (result == -1) {
        ESP_LOGE(TAG, "Modbus timeout");
    } else if (result == -2) {
        ESP_LOGE(TAG, "Modbus CRC error");
    } else if (result <= -3) {
        ESP_LOGE(TAG, "Modbus exception: 0x%02X", -(result + 3));
    }
    return ESP_FAIL;
}

// Log statistics
ESP_LOGI(TAG, "Modbus read success: %d registers", result);
```

### Performance Metrics

```
Typical Modbus RTU Performance:
- 9600 baud: ~15-20 requests/second
- 19200 baud: ~30-40 requests/second
- 38400 baud: ~60-80 requests/second
- 115200 baud: ~150-200 requests/second

Frame overhead:
- Minimum frame: 8 bytes (slave + FC + address + qty + CRC)
- Per register: 2 bytes (data)
- Typical read (2 registers): 13 bytes total

Time calculation:
Frame time = (bytes * 11 bits) / baud_rate
```

---

## 9. Advanced Topics

### Broadcast Messages (Slave ID 0)

```c
// Write to all slaves (no response expected)
modbus_write_single_register(0, 100, 0x1234);

// Note: Do not wait for response
// Use for synchronization or reset commands
```

### Multi-Drop Networks

```
Best practices for multiple slaves:
- Assign unique slave IDs (1-247)
- Use termination resistors at both ends
- Keep total cable length < 1200m at 9600 baud
- Maximum 32 devices per segment
- Use repeaters for larger networks
- Poll slaves in round-robin fashion
- Implement slave priority if needed
```

### Gateway/Bridge Implementation

```c
// Forward Modbus requests from TCP to RTU
void modbus_tcp_to_rtu_bridge(void) {
    // 1. Receive Modbus TCP frame (no CRC, has MBAP header)
    // 2. Extract Modbus PDU (function code + data)
    // 3. Add slave ID and calculate CRC
    // 4. Send via RS485
    // 5. Wait for response
    // 6. Remove CRC, add MBAP header
    // 7. Send back to TCP client
}
```

---

## Conclusion

This guide provides complete coverage of Modbus RTU implementation for industrial applications. Use it as a reference when:
- Implementing Modbus communication
- Debugging protocol issues
- Optimizing performance
- Understanding data formats
- Training team members

**Key Takeaways:**
- Modbus RTU uses CRC16 for error detection
- 16 different data formats supported
- RS485 requires direction control (RTS)
- Always validate CRC before processing data
- Batch reads for better performance
- Handle exceptions and timeouts gracefully

**Related Resources:**
- [Modbus Protocol Specification](https://modbus.org/docs/Modbus_Application_Protocol_V1_1b3.pdf)
- [RS485 Standard (TIA-485-A)](https://www.tiaonline.org/)
- ESP32 Modbus Gateway Technical Reference

---

**Version**: 1.0
**Last Updated**: 2025-01-12
**Project**: ESP32 Modbus IoT Gateway
