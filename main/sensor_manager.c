// sensor_manager.c - Multi-sensor management implementation

#include "sensor_manager.h"
#include "modbus.h"
#include "web_config.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <time.h>
#include <math.h>
#include <inttypes.h>

static const char *TAG = "SENSOR_MGR";

esp_err_t sensor_manager_init(void)
{
    ESP_LOGI(TAG, "Initializing sensor manager");
    return ESP_OK;
}

esp_err_t convert_modbus_data(uint16_t *registers, int reg_count, 
                             const char* data_type, const char* byte_order,
                             double scale_factor, double *result, uint32_t *raw_value)
{
    if (!registers || !data_type || !byte_order || !result || !raw_value) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Converting data: Type=%s, Order=%s, Scale=%.6f", data_type, byte_order, scale_factor);

    // Handle specific format names from web interface
    const char* actual_data_type = data_type;
    const char* actual_byte_order = byte_order;
    
    // 32-bit Integer formats - support both numeric (1234) and letter (ABCD) patterns
    if (strstr(data_type, "INT32_1234") || strstr(data_type, "UINT32_1234") ||
        strstr(data_type, "INT32_ABCD") || strstr(data_type, "UINT32_ABCD")) {
        actual_data_type = strstr(data_type, "UINT32") ? "UINT32" : "INT32";
        actual_byte_order = "BIG_ENDIAN";     // 1234 = ABCD = BIG_ENDIAN
    } else if (strstr(data_type, "INT32_4321") || strstr(data_type, "UINT32_4321") ||
               strstr(data_type, "INT32_DCBA") || strstr(data_type, "UINT32_DCBA")) {
        actual_data_type = strstr(data_type, "UINT32") ? "UINT32" : "INT32";
        actual_byte_order = "LITTLE_ENDIAN";  // 4321 = DCBA = LITTLE_ENDIAN
    } else if (strstr(data_type, "INT32_3412") || strstr(data_type, "UINT32_3412") ||
               strstr(data_type, "INT32_CDAB") || strstr(data_type, "UINT32_CDAB")) {
        actual_data_type = strstr(data_type, "UINT32") ? "UINT32" : "INT32";
        actual_byte_order = "LITTLE_ENDIAN";  // 3412 = CDAB (word swap) = reg[1]<<16 | reg[0]
    } else if (strstr(data_type, "INT32_2143") || strstr(data_type, "UINT32_2143") ||
               strstr(data_type, "INT32_BADC") || strstr(data_type, "UINT32_BADC")) {
        actual_data_type = strstr(data_type, "UINT32") ? "UINT32" : "INT32";
        actual_byte_order = "MIXED_BADC";     // 2143 = BADC = MIXED_BADC
    }
    
    // 32-bit Float formats
    else if (strstr(data_type, "FLOAT32_1234")) {
        actual_data_type = "FLOAT32";
        actual_byte_order = "BIG_ENDIAN";     // 1234 = ABCD
    } else if (strstr(data_type, "FLOAT32_4321")) {
        actual_data_type = "FLOAT32";
        actual_byte_order = "LITTLE_ENDIAN";  // 4321 = DCBA
    } else if (strstr(data_type, "FLOAT32_3412")) {
        actual_data_type = "FLOAT32";
        actual_byte_order = "LITTLE_ENDIAN";  // 3412 = DCBA (word swap)
    } else if (strstr(data_type, "FLOAT32_2143")) {
        actual_data_type = "FLOAT32";
        actual_byte_order = "MIXED_BADC";     // 2143 = BADC
    }
    
    // 64-bit formats - Handle both full and truncated format names
    else if (strstr(data_type, "INT64_12345678") || strstr(data_type, "UINT64_12345678") || strstr(data_type, "FLOAT64_12345678") ||
             strstr(data_type, "INT64_1234567") || strstr(data_type, "UINT64_1234567") || strstr(data_type, "FLOAT64_1234567")) {
        actual_data_type = strstr(data_type, "UINT64") ? "UINT64" : strstr(data_type, "FLOAT64") ? "FLOAT64" : "INT64";
        actual_byte_order = "BIG_ENDIAN";     // 12345678/1234567 = standard order
    } else if (strstr(data_type, "INT64_87654321") || strstr(data_type, "UINT64_87654321") || strstr(data_type, "FLOAT64_87654321") ||
               strstr(data_type, "INT64_8765432") || strstr(data_type, "UINT64_8765432") || strstr(data_type, "FLOAT64_8765432")) {
        actual_data_type = strstr(data_type, "UINT64") ? "UINT64" : strstr(data_type, "FLOAT64") ? "FLOAT64" : "INT64";
        actual_byte_order = "LITTLE_ENDIAN";  // 87654321/8765432 = reversed
    } else if (strstr(data_type, "INT64_78563412") || strstr(data_type, "UINT64_78563412") || strstr(data_type, "FLOAT64_78563412") ||
               strstr(data_type, "INT64_7856341") || strstr(data_type, "UINT64_7856341") || strstr(data_type, "FLOAT64_7856341")) {
        actual_data_type = strstr(data_type, "UINT64") ? "UINT64" : strstr(data_type, "FLOAT64") ? "FLOAT64" : "INT64";
        actual_byte_order = "MIXED_BADC";     // 78563412/7856341 = mixed order
    }
    
    ESP_LOGI(TAG, "Mapped to: Type=%s, Order=%s", actual_data_type, actual_byte_order);

    if (strcmp(actual_data_type, "UINT16") == 0 && reg_count >= 1) {
        *raw_value = registers[0];
        *result = (double)(*raw_value) * scale_factor;
        ESP_LOGI(TAG, "UINT16: Raw=0x%04" PRIX32 " (%" PRIu32 ") -> %.6f", *raw_value, *raw_value, *result);
        
    } else if (strcmp(actual_data_type, "INT16") == 0 && reg_count >= 1) {
        int16_t signed_val = (int16_t)registers[0];
        *raw_value = registers[0];
        *result = (double)signed_val * scale_factor;
        ESP_LOGI(TAG, "INT16: Raw=0x%04" PRIX32 " (%d) -> %.6f", *raw_value, signed_val, *result);
        
    } else if ((strcmp(actual_data_type, "UINT32") == 0 || strcmp(actual_data_type, "INT32") == 0) && reg_count >= 2) {
        uint32_t combined_value;
        
        if (strcmp(actual_byte_order, "BIG_ENDIAN") == 0) {
            // ABCD - reg[0] is high word, reg[1] is low word
            combined_value = ((uint32_t)registers[0] << 16) | registers[1];
        } else if (strcmp(actual_byte_order, "LITTLE_ENDIAN") == 0) {
            // CDAB - reg[1] is high word, reg[0] is low word
            combined_value = ((uint32_t)registers[1] << 16) | registers[0];
        } else if (strcmp(actual_byte_order, "MIXED_BADC") == 0) {
            // BADC - byte swap within each register
            uint16_t reg0_swapped = ((registers[0] & 0xFF) << 8) | ((registers[0] >> 8) & 0xFF);
            uint16_t reg1_swapped = ((registers[1] & 0xFF) << 8) | ((registers[1] >> 8) & 0xFF);
            combined_value = ((uint32_t)reg0_swapped << 16) | reg1_swapped;
        } else if (strcmp(actual_byte_order, "MIXED_DCBA") == 0) {
            // DCBA - completely reversed
            uint16_t reg0_swapped = ((registers[0] & 0xFF) << 8) | ((registers[0] >> 8) & 0xFF);
            uint16_t reg1_swapped = ((registers[1] & 0xFF) << 8) | ((registers[1] >> 8) & 0xFF);
            combined_value = ((uint32_t)reg1_swapped << 16) | reg0_swapped;
        } else {
            ESP_LOGE(TAG, "Unknown byte order: %s", actual_byte_order);
            return ESP_ERR_INVALID_ARG;
        }

        *raw_value = combined_value;
        
        if (strcmp(actual_data_type, "INT32") == 0) {
            int32_t signed_val = (int32_t)combined_value;
            *result = (double)signed_val * scale_factor;
            ESP_LOGI(TAG, "INT32: Raw=0x%08" PRIX32 " (%" PRId32 ") -> %.6f", combined_value, signed_val, *result);
        } else {
            *result = (double)combined_value * scale_factor;
            ESP_LOGI(TAG, "UINT32: Raw=0x%08" PRIX32 " (%" PRIu32 ") -> %.6f", combined_value, combined_value, *result);
        }

    } else if ((strcmp(actual_data_type, "UINT32") == 0 || strcmp(actual_data_type, "INT32") == 0) && reg_count == 1) {
        // Fallback: INT32/UINT32 with only 1 register - treat as 16-bit value
        ESP_LOGW(TAG, "INT32/UINT32 requested but only 1 register available - using 16-bit interpretation");
        if (strcmp(actual_data_type, "INT32") == 0) {
            int16_t signed_val = (int16_t)registers[0];
            *raw_value = registers[0];
            *result = (double)signed_val * scale_factor;
            ESP_LOGI(TAG, "INT32->INT16 fallback: Raw=0x%04" PRIX32 " (%d) -> %.6f", *raw_value, signed_val, *result);
        } else {
            *raw_value = registers[0];
            *result = (double)(*raw_value) * scale_factor;
            ESP_LOGI(TAG, "UINT32->UINT16 fallback: Raw=0x%04" PRIX32 " (%" PRIu32 ") -> %.6f", *raw_value, *raw_value, *result);
        }

    } else if (strcmp(actual_data_type, "HEX") == 0) {
        // HEX type - concatenate all registers as hex value
        *raw_value = 0;
        for (int i = 0; i < reg_count && i < 2; i++) {
            *raw_value = (*raw_value << 16) | registers[i];
        }
        *result = (double)(*raw_value) * scale_factor;
        ESP_LOGI(TAG, "HEX: Raw=0x%08" PRIX32 " -> %.6f", *raw_value, *result);
        
    } else if (strcmp(actual_data_type, "FLOAT32") == 0 && reg_count >= 2) {
        uint32_t combined_value;

        if (strcmp(actual_byte_order, "BIG_ENDIAN") == 0) {
            combined_value = ((uint32_t)registers[0] << 16) | registers[1];
        } else if (strcmp(actual_byte_order, "LITTLE_ENDIAN") == 0) {
            combined_value = ((uint32_t)registers[1] << 16) | registers[0];
        } else if (strcmp(actual_byte_order, "MIXED_BADC") == 0) {
            // BADC - byte swap within each register
            uint16_t reg0_swapped = ((registers[0] & 0xFF) << 8) | ((registers[0] >> 8) & 0xFF);
            uint16_t reg1_swapped = ((registers[1] & 0xFF) << 8) | ((registers[1] >> 8) & 0xFF);
            combined_value = ((uint32_t)reg0_swapped << 16) | reg1_swapped;
        } else if (strcmp(actual_byte_order, "MIXED_DCBA") == 0) {
            // DCBA - byte swap within each register + word swap
            uint16_t reg0_swapped = ((registers[0] & 0xFF) << 8) | ((registers[0] >> 8) & 0xFF);
            uint16_t reg1_swapped = ((registers[1] & 0xFF) << 8) | ((registers[1] >> 8) & 0xFF);
            combined_value = ((uint32_t)reg1_swapped << 16) | reg0_swapped;
        } else {
            ESP_LOGE(TAG, "FLOAT32 unsupported byte order: %s", actual_byte_order);
            return ESP_ERR_INVALID_ARG;
        }
        
        // Convert to IEEE 754 float
        union {
            uint32_t i;
            float f;
        } converter;
        converter.i = combined_value;
        
        *raw_value = combined_value;
        *result = (double)converter.f * scale_factor;
        ESP_LOGI(TAG, "FLOAT32: Raw=0x%08" PRIX32 " (%.6f) -> %.6f", combined_value, converter.f, *result);
        
    } else if (strcmp(actual_data_type, "FLOAT64") == 0 && reg_count >= 4) {
        // FLOAT64 handling - 4 registers (64-bit double precision)
        uint64_t combined_value64 = 0;
        
        if (strcmp(actual_byte_order, "BIG_ENDIAN") == 0) {
            // FLOAT64_12345678 (ABCDEFGH) - Standard big endian
            combined_value64 = ((uint64_t)registers[0] << 48) | ((uint64_t)registers[1] << 32) | 
                              ((uint64_t)registers[2] << 16) | registers[3];
        } else if (strcmp(actual_byte_order, "LITTLE_ENDIAN") == 0) {
            // FLOAT64_87654321 (HGFEDCBA) - Full little endian
            combined_value64 = ((uint64_t)registers[3] << 48) | ((uint64_t)registers[2] << 32) | 
                              ((uint64_t)registers[1] << 16) | registers[0];
        } else if (strcmp(actual_byte_order, "MIXED_BADC") == 0) {
            // FLOAT64_78563412 (GHEFCDAB) - Mixed byte order
            uint64_t val64_78563412 = ((uint64_t)(((registers[3] & 0xFF) << 8) | ((registers[3] >> 8) & 0xFF)) << 48) |
                                     ((uint64_t)(((registers[2] & 0xFF) << 8) | ((registers[2] >> 8) & 0xFF)) << 32) |
                                     ((uint64_t)(((registers[1] & 0xFF) << 8) | ((registers[1] >> 8) & 0xFF)) << 16) |
                                     (((registers[0] & 0xFF) << 8) | ((registers[0] >> 8) & 0xFF));
            combined_value64 = val64_78563412;
        } else {
            ESP_LOGE(TAG, "FLOAT64 unsupported byte order: %s", actual_byte_order);
            return ESP_ERR_INVALID_ARG;
        }
        
        // Convert to IEEE 754 double precision
        union {
            uint64_t i;
            double d;
        } converter64;
        converter64.i = combined_value64;
        
        *raw_value = (uint32_t)(combined_value64 & 0xFFFFFFFF);  // Store lower 32 bits for compatibility
        *result = converter64.d * scale_factor;
        ESP_LOGI(TAG, "FLOAT64: Raw=0x%016" PRIX64 " (%.6f) -> %.6f", combined_value64, converter64.d, *result);
        
    } else {
        // Calculate expected register count for better error message
        int expected_regs = 1;  // Default for INT16/UINT16
        if (strcmp(actual_data_type, "UINT32") == 0 || strcmp(actual_data_type, "INT32") == 0 || strcmp(actual_data_type, "FLOAT32") == 0) {
            expected_regs = 2;
        } else if (strcmp(actual_data_type, "FLOAT64") == 0 || strcmp(actual_data_type, "INT64") == 0 || strcmp(actual_data_type, "UINT64") == 0) {
            expected_regs = 4;
        }
        
        ESP_LOGE(TAG, "Unsupported data type or insufficient registers: %s -> %s (need %d, have %d)", 
                 data_type, actual_data_type, expected_regs, reg_count);
        return ESP_ERR_INVALID_ARG;
    }

    return ESP_OK;
}

esp_err_t sensor_test_live(const sensor_config_t *sensor, sensor_test_result_t *result)
{
    if (!sensor || !result) {
        return ESP_ERR_INVALID_ARG;
    }

    // Clear result
    memset(result, 0, sizeof(sensor_test_result_t));
    
    ESP_LOGI(TAG, "Testing sensor: %s (Unit: %s, Slave: %d)", 
             sensor->name, sensor->unit_id, sensor->slave_id);

    uint32_t start_time = esp_timer_get_time() / 1000;
    
    // Perform Modbus read based on register type
    modbus_result_t modbus_result;
    
    // Default to HOLDING if register_type is empty or invalid
    const char* reg_type = sensor->register_type;
    if (!reg_type || strlen(reg_type) == 0) {
        reg_type = "HOLDING";
    } else if (strcmp(reg_type, "INPUT") == 0 || strcmp(reg_type, "INPUT_REGISTER") == 0) {
        reg_type = "INPUT";  // Normalize to short form
    } else if (strcmp(reg_type, "HOLDING") == 0 || strcmp(reg_type, "HOLDING_REGISTER") == 0 || strncmp(reg_type, "HOLDING", 7) == 0) {
        reg_type = "HOLDING";  // Normalize to short form
    } else {
        ESP_LOGW(TAG, "Unrecognized register type '%s', defaulting to HOLDING", reg_type);
        reg_type = "HOLDING";
    }
    
    // For special sensor types, override register quantity
    int quantity_to_read = sensor->quantity;
    if (strcmp(sensor->sensor_type, "Aquadax_Quality") == 0) {
        quantity_to_read = 12;
        ESP_LOGI(TAG, "Aquadax_Quality sensor detected, reading 12 registers for 5x FLOAT32_ABCD (COD,BOD,TSS,pH,Temp)");
    } else if (strcmp(sensor->sensor_type, "Flow-Meter") == 0) {
        quantity_to_read = 4;
        ESP_LOGI(TAG, "Flow-Meter sensor detected, reading 4 registers for UINT32_BADC + FLOAT32_BADC interpretation");
    } else if (strcmp(sensor->sensor_type, "ZEST") == 0) {
        quantity_to_read = 4;
        ESP_LOGI(TAG, "ZEST sensor detected, reading 4 registers for UINT32_CDAB + FLOAT32_ABCD interpretation");
    } else if (strcmp(sensor->sensor_type, "Panda_USM") == 0) {
        quantity_to_read = 4;
        ESP_LOGI(TAG, "Panda USM sensor detected, reading 4 registers for DOUBLE64 (Net Volume)");
    }
    
    // Set the baud rate for this sensor
    int baud_rate = sensor->baud_rate > 0 ? sensor->baud_rate : 9600;
    ESP_LOGI(TAG, "Setting baud rate to %d bps for sensor '%s'", baud_rate, sensor->name);
    esp_err_t baud_err = modbus_set_baud_rate(baud_rate);
    if (baud_err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set baud rate for sensor '%s': %s", sensor->name, esp_err_to_name(baud_err));
        // Continue anyway with current baud rate
    }
    
    // Get retry settings from system config
    system_config_t *sys_config = get_system_config();
    int retry_count = sys_config ? sys_config->modbus_retry_count : 1;
    int retry_delay_ms = sys_config ? sys_config->modbus_retry_delay : 50;

    // Perform Modbus read with retry logic
    int attempt = 0;
    do {
        if (attempt > 0) {
            ESP_LOGW(TAG, "Retry %d/%d for sensor '%s' after %d ms delay",
                     attempt, retry_count, sensor->name, retry_delay_ms);
            vTaskDelay(pdMS_TO_TICKS(retry_delay_ms));
        }

        if (strcmp(reg_type, "HOLDING") == 0) {
            modbus_result = modbus_read_holding_registers(sensor->slave_id,
                                                         sensor->register_address,
                                                         quantity_to_read);
        } else if (strcmp(reg_type, "INPUT") == 0) {
            modbus_result = modbus_read_input_registers(sensor->slave_id,
                                                       sensor->register_address,
                                                       quantity_to_read);
        } else {
            snprintf(result->error_message, sizeof(result->error_message),
                    "Unknown register type: %s", reg_type);
            return ESP_ERR_INVALID_ARG;
        }

        attempt++;
    } while (modbus_result != MODBUS_SUCCESS && attempt <= retry_count);

    result->response_time_ms = (esp_timer_get_time() / 1000) - start_time;

    if (modbus_result != MODBUS_SUCCESS) {
        result->success = false;
        snprintf(result->error_message, sizeof(result->error_message),
                "Modbus error: %d after %d attempt(s)", modbus_result, attempt);
        ESP_LOGE(TAG, "Modbus read failed after %d attempt(s): %d", attempt, modbus_result);
        return ESP_FAIL;
    }

    if (attempt > 1) {
        ESP_LOGI(TAG, "Modbus read succeeded on attempt %d/%d", attempt, retry_count + 1);
    }

    // Get the raw register values
    // Use larger buffer to handle all sensor types (max 8 registers for 64-bit values)
    uint16_t registers[16];
    int reg_count = modbus_get_response_length();

    // Validate register count against our buffer size
    if (reg_count > 16) {
        ESP_LOGW(TAG, "Register count %d exceeds buffer, limiting to 16", reg_count);
        reg_count = 16;
    }

    if (reg_count < sensor->quantity) {
        result->success = false;
        snprintf(result->error_message, sizeof(result->error_message),
                "Insufficient registers received: got %d, expected %d", reg_count, sensor->quantity);
        return ESP_FAIL;
    }

    for (int i = 0; i < reg_count; i++) {
        registers[i] = modbus_get_response_buffer(i);
    }

    // Create hex representation (5 chars per register: "XXXX ")
    char hex_buf[96] = {0};  // Enough for 16 registers (16 * 5 = 80) + safety margin
    int hex_pos = 0;
    for (int i = 0; i < reg_count && hex_pos < (int)(sizeof(hex_buf) - 6); i++) {
        hex_pos += snprintf(hex_buf + hex_pos, sizeof(hex_buf) - hex_pos, "%04X ", registers[i]);
    }
    strncpy(result->raw_hex, hex_buf, sizeof(result->raw_hex) - 1);
    result->raw_hex[sizeof(result->raw_hex) - 1] = '\0';  // Ensure null termination

    // Special handling for Flow-Meter sensors (4 registers: UINT32_BADC + FLOAT32_BADC)
    if (strcmp(sensor->sensor_type, "Flow-Meter") == 0 && reg_count >= 4) {
        // Flow-Meter reads 4 registers:
        // Registers [0-1]: Cumulative Flow Integer part (32-bit UINT, BADC word-swapped)
        // Registers [2-3]: Cumulative Flow Decimal part (32-bit FLOAT, BADC word-swapped)
        // Example: 33073.865 m³ = 33073 (registers[0-1]) + 0.865 (float in registers[2-3])

        // Integer part: BADC format (word-swapped) = (reg[1] << 16) | reg[0]
        uint32_t integer_part_raw = ((uint32_t)registers[1] << 16) | registers[0];
        double integer_part = (double)integer_part_raw;

        // Decimal part: BADC format (word-swapped) = (reg[3] << 16) | reg[2]
        uint32_t float_bits = ((uint32_t)registers[3] << 16) | registers[2];
        float decimal_part_float;
        memcpy(&decimal_part_float, &float_bits, sizeof(float));
        double decimal_part = (double)decimal_part_float;

        // Sum integer and decimal parts, then apply scale factor
        result->scaled_value = (integer_part + decimal_part) * sensor->scale_factor;
        result->raw_value = integer_part_raw; // Store integer part as raw value

        ESP_LOGI(TAG, "Flow-Meter Calculation: Integer=0x%08lX(%lu) + Decimal(FLOAT)=0x%08lX(%.6f) = %.6f",
                 (unsigned long)integer_part_raw, (unsigned long)integer_part_raw,
                 (unsigned long)float_bits, decimal_part, result->scaled_value);
    }
    // Special handling for ZEST sensors (AquaGen Flow Meter format)
    else if (strcmp(sensor->sensor_type, "ZEST") == 0 && reg_count >= 4) {
        // ZEST reads 4 registers starting at 0x1019:
        // Actual format observed from device:
        // Register [0]: Usually 0x0000 (or integer high word if value > 65535)
        // Register [1]: Integer part OR float high word
        // Register [2]: Float low word
        // Register [3]: Usually 0x0000
        // The float value spans registers[1] and registers[2] as IEEE 754 Big Endian (ABCD)

        // Check if this is a pure float value (Register[0] is 0 and Register[3] is 0)
        // Float is in registers[1] and registers[2] as Big Endian
        uint32_t float_bits = ((uint32_t)registers[1] << 16) | registers[2];
        float float_value;
        memcpy(&float_value, &float_bits, sizeof(float));

        // If Register[0] has a value, add it as integer part (for larger values)
        uint32_t integer_part_raw = (uint32_t)registers[0];
        double integer_part = (double)integer_part_raw;
        double decimal_part = (double)float_value;

        // Final value = integer part + float value
        result->scaled_value = (integer_part + decimal_part) * sensor->scale_factor;
        result->raw_value = integer_part_raw;

        ESP_LOGI(TAG, "ZEST Calculation: Integer=0x%04X(%lu) + Float=0x%08lX(%.6f) = %.6f",
                 (unsigned int)integer_part_raw, (unsigned long)integer_part_raw,
                 (unsigned long)float_bits, decimal_part, result->scaled_value);
    }
    // Special handling for Panda USM sensors (64-bit double format)
    else if (strcmp(sensor->sensor_type, "Panda_USM") == 0 && reg_count >= 4) {
        // Panda USM stores net volume as 64-bit double at register 4
        // Big-endian format: registers[0] = MSW, registers[3] = LSW
        uint64_t combined_value64 = ((uint64_t)registers[0] << 48) |
                                   ((uint64_t)registers[1] << 32) |
                                   ((uint64_t)registers[2] << 16) |
                                   registers[3];

        // Convert to double
        double net_volume;
        memcpy(&net_volume, &combined_value64, sizeof(double));

        // Apply scale factor
        result->scaled_value = net_volume * sensor->scale_factor;
        result->raw_value = (uint32_t)(combined_value64 >> 32); // Store upper 32 bits as raw value

        ESP_LOGI(TAG, "Panda USM Calculation: DOUBLE64=0x%016llX = %.6f m³",
                 (unsigned long long)combined_value64, result->scaled_value);
    }
    // Special handling for Clampon flow meters (4 registers: UINT32_BADC + FLOAT32_BADC)
    else if (strcmp(sensor->sensor_type, "Clampon") == 0 && reg_count >= 4) {
        // Clampon reads 4 registers:
        // Registers [0-1]: Cumulative Flow Integer part (32-bit UINT, BADC word-swapped)
        // Registers [2-3]: Cumulative Flow Decimal part (32-bit FLOAT, BADC word-swapped)
        // Example: 33073.865 m³ = 33073 (registers[0-1]) + 0.865 (float in registers[2-3])

        // Integer part: BADC format (word-swapped) = (reg[1] << 16) | reg[0]
        uint32_t integer_part_raw = ((uint32_t)registers[1] << 16) | registers[0];
        double integer_part = (double)integer_part_raw;

        // Decimal part: BADC format (word-swapped) = (reg[3] << 16) | reg[2]
        uint32_t float_bits = ((uint32_t)registers[3] << 16) | registers[2];
        float decimal_part_float;
        memcpy(&decimal_part_float, &float_bits, sizeof(float));
        double decimal_part = (double)decimal_part_float;

        // Sum integer and decimal parts, then apply scale factor
        result->scaled_value = (integer_part + decimal_part) * sensor->scale_factor;
        result->raw_value = integer_part_raw; // Store integer part as raw value

        ESP_LOGI(TAG, "Clampon Calculation: Integer=0x%08lX(%lu) + Decimal(FLOAT)=0x%08lX(%.6f) = %.6f",
                 (unsigned long)integer_part_raw, (unsigned long)integer_part_raw,
                 (unsigned long)float_bits, decimal_part, result->scaled_value);
    }
    // Special handling for Dailian EMF flow meters (2 registers: UINT32 word-swapped totaliser)
    else if (strcmp(sensor->sensor_type, "Dailian_EMF") == 0 && reg_count >= 2) {
        // Dailian EMF reads 2 registers at address 0x07D6 (2006):
        // Registers [0-1]: Totaliser value (32-bit UINT, word-swapped)
        // Format: (reg[1] << 16) | reg[0]

        // Totaliser: word-swapped = (reg[1] << 16) | reg[0]
        uint32_t totaliser_raw = ((uint32_t)registers[1] << 16) | registers[0];

        // Apply scale factor
        result->scaled_value = (double)totaliser_raw * sensor->scale_factor;
        result->raw_value = totaliser_raw;

        ESP_LOGI(TAG, "Dailian_EMF Calculation: Totaliser=0x%08lX(%lu) * %.6f = %.6f",
                 (unsigned long)totaliser_raw, (unsigned long)totaliser_raw,
                 sensor->scale_factor, result->scaled_value);
    }
    // Special handling for Panda EMF flow meters (4 registers: INT32_BE + FLOAT32_BE)
    else if (strcmp(sensor->sensor_type, "Panda_EMF") == 0 && reg_count >= 4) {
        // Panda EMF reads 4 registers at address 0x1012 (4114):
        // Registers [0-1]: Totalizer integer part (32-bit INT, big-endian)
        // Registers [2-3]: Totalizer decimal part (32-bit FLOAT, big-endian)
        // Total = integer_part + float_decimal

        // Integer part: Big-endian = (reg[0] << 16) | reg[1]
        int32_t integer_part = (int32_t)(((uint32_t)registers[0] << 16) | registers[1]);
        double integer_value = (double)integer_part;

        // Decimal part: Big-endian = (reg[2] << 16) | reg[3]
        uint32_t float_bits = ((uint32_t)registers[2] << 16) | registers[3];
        float decimal_part_float;
        memcpy(&decimal_part_float, &float_bits, sizeof(float));
        double decimal_value = (double)decimal_part_float;

        // Sum integer and decimal parts, then apply scale factor
        result->scaled_value = (integer_value + decimal_value) * sensor->scale_factor;
        result->raw_value = (uint32_t)integer_part; // Store integer part as raw value

        ESP_LOGI(TAG, "Panda_EMF Calculation: Integer=0x%08lX(%ld) + Decimal(FLOAT)=0x%08lX(%.6f) = %.6f",
                 (unsigned long)((uint32_t)integer_part), (long)integer_part,
                 (unsigned long)float_bits, decimal_value, result->scaled_value);
    }
    // Special handling for Panda Level sensors (1 register: UINT16 level value)
    else if (strcmp(sensor->sensor_type, "Panda_Level") == 0 && reg_count >= 1) {
        // Panda Level reads 1 register at address 0x0001 (1):
        // Register [0]: Level value (distance from sensor to water surface)
        // Calculation: Level % = ((Sensor Height - Raw Value) / Tank Height) * 100

        // Raw level value (distance reading)
        uint16_t raw_level = registers[0];
        double level_value = (double)raw_level;

        // Apply the level calculation if sensor_height and max_water_level are set
        if (sensor->max_water_level > 0) {
            // Level % = ((Sensor Height - Raw Value) / Tank Height) * 100
            result->scaled_value = ((sensor->sensor_height - level_value) / sensor->max_water_level) * 100.0;
            // Clamp to 0-100% range
            if (result->scaled_value < 0) result->scaled_value = 0.0;
            if (result->scaled_value > 100) result->scaled_value = 100.0;
        } else {
            // If no tank height set, just return raw value scaled
            result->scaled_value = level_value * sensor->scale_factor;
        }
        result->raw_value = raw_level;

        ESP_LOGI(TAG, "Panda_Level Calculation: Raw=%u, SensorHeight=%.2f, TankHeight=%.2f, Level%%=%.2f",
                 raw_level, sensor->sensor_height, sensor->max_water_level, result->scaled_value);
    }
    // Special handling for Hydrostatic Level sensors (Aquagen) - 1 register at address 0x0004
    // Formula: Level % = (Raw Value / Tank Height) * 100
    // Unlike Panda_Level, the raw value IS the water level (not distance from sensor)
    else if (strcmp(sensor->sensor_type, "Hydrostatic_Level") == 0 && reg_count >= 1) {
        // Raw level value (actual water level reading)
        uint16_t raw_level = registers[0];
        double level_value = (double)raw_level;

        // Apply the level calculation if max_water_level (tank height) is set
        if (sensor->max_water_level > 0) {
            // Hydrostatic: Level % = (Raw / Tank Height) * 100
            result->scaled_value = (level_value / sensor->max_water_level) * 100.0;
            // Clamp to 0-100% range
            if (result->scaled_value < 0) result->scaled_value = 0.0;
            if (result->scaled_value > 100) result->scaled_value = 100.0;
        } else {
            // If no tank height set, just return raw value scaled
            result->scaled_value = level_value * sensor->scale_factor;
        }
        result->raw_value = raw_level;

        ESP_LOGI(TAG, "Hydrostatic_Level Calculation: Raw=%u, TankHeight=%.2f, Level%%=%.2f",
                 raw_level, sensor->max_water_level, result->scaled_value);
    }
    // Special handling for Aquadax Quality sensors (12 registers: 5x FLOAT32 BIG_ENDIAN)
    else if (strcmp(sensor->sensor_type, "Aquadax_Quality") == 0 && reg_count >= 10) {
        // Aquadax Quality reads 12 registers starting at address 1280:
        // Registers [0-1]:  COD (mg/L)       - FLOAT32 BIG_ENDIAN (ABCD)
        // Registers [2-3]:  BOD (mg/L)       - FLOAT32 BIG_ENDIAN (ABCD)
        // Registers [4-5]:  TSS (mg/L)       - FLOAT32 BIG_ENDIAN (ABCD)
        // Registers [6-7]:  pH               - FLOAT32 BIG_ENDIAN (ABCD)
        // Registers [8-9]:  Temperature (°C) - FLOAT32 BIG_ENDIAN (ABCD)
        // Registers [10-11]: Reserved (ignored)

        // Parse first parameter (COD) as primary display value for test
        uint32_t float_bits = ((uint32_t)registers[0] << 16) | registers[1];
        float cod_value;
        memcpy(&cod_value, &float_bits, sizeof(float));
        result->scaled_value = (double)cod_value * sensor->scale_factor;
        result->raw_value = float_bits;

        ESP_LOGI(TAG, "Aquadax_Quality Test: COD=%.3f (primary display value)", result->scaled_value);

        // Log all 5 parameters for debugging
        const char *param_names[] = {"COD", "BOD", "TSS", "pH", "Temp"};
        for (int p = 0; p < 5 && (p * 2 + 1) < reg_count; p++) {
            uint32_t fb = ((uint32_t)registers[p * 2] << 16) | registers[p * 2 + 1];
            float fv;
            memcpy(&fv, &fb, sizeof(float));
            ESP_LOGI(TAG, "  %s = %.3f (raw: 0x%08lX)", param_names[p], fv, (unsigned long)fb);
        }
    } else {
        // Convert the data using standard conversion
        esp_err_t conv_result = convert_modbus_data(registers, reg_count,
                                                   sensor->data_type, sensor->byte_order,
                                                   sensor->scale_factor,
                                                   &result->scaled_value, &result->raw_value);

        if (conv_result != ESP_OK) {
            result->success = false;
            snprintf(result->error_message, sizeof(result->error_message), 
                    "Data conversion failed");
            return conv_result;
        }
    }

    // Apply calculation engine if configured
    if (sensor->calculation.calc_type != CALC_NONE) {
        double pre_calc_value = result->scaled_value;
        result->scaled_value = apply_calculation(sensor, pre_calc_value, registers, reg_count);
        ESP_LOGI(TAG, "Calculation applied: %.6f -> %.6f (type: %s)",
                 pre_calc_value, result->scaled_value,
                 get_calculation_type_name(sensor->calculation.calc_type));
    }

    result->success = true;
    ESP_LOGI(TAG, "Test successful: %.6f (Response: %lu ms)",
             result->scaled_value, result->response_time_ms);

    return ESP_OK;
}

esp_err_t sensor_read_single(const sensor_config_t *sensor, sensor_reading_t *reading)
{
    if (!sensor || !reading) {
        return ESP_ERR_INVALID_ARG;
    }

    // For water quality sensors, use specialized multi-parameter reading
    if (strcmp(sensor->sensor_type, "QUALITY") == 0) {
        return sensor_read_quality(sensor, reading);
    }

    // For Aquadax Quality sensors, use single bulk read for all 5 parameters
    if (strcmp(sensor->sensor_type, "Aquadax_Quality") == 0) {
        return sensor_read_aquadax_quality(sensor, reading);
    }

    // Clear reading
    memset(reading, 0, sizeof(sensor_reading_t));
    
    // Copy basic info
    strncpy(reading->unit_id, sensor->unit_id, sizeof(reading->unit_id) - 1);
    strncpy(reading->sensor_name, sensor->name, sizeof(reading->sensor_name) - 1);
    
    // Get timestamp
    time_t now;
    struct tm timeinfo;
    time(&now);
    gmtime_r(&now, &timeinfo);
    strftime(reading->timestamp, sizeof(reading->timestamp), "%Y-%m-%dT%H:%M:%SZ", &timeinfo);

    // Test the sensor
    sensor_test_result_t test_result;
    esp_err_t ret = sensor_test_live(sensor, &test_result);
    
    if (ret == ESP_OK && test_result.success) {
        // Apply sensor type-specific calculations
        if (strcmp(sensor->sensor_type, "Level") == 0) {
            // Level sensor calculation: (Sensor Height - Raw Value) / Maximum Water Level * 100
            double raw_scaled_value = test_result.scaled_value;
            double level_percentage = 0.0;
            
            if (sensor->max_water_level > 0) {
                level_percentage = ((sensor->sensor_height - raw_scaled_value) / sensor->max_water_level) * 100.0;
                // Ensure percentage is within reasonable bounds
                if (level_percentage < 0) level_percentage = 0.0;
                if (level_percentage > 100) level_percentage = 100.0;
            }
            
            reading->value = level_percentage;
            ESP_LOGI(TAG, "Level Sensor %s: Raw=%.6f, Height=%.2f, MaxLevel=%.2f -> %.2f%%", 
                     reading->unit_id, raw_scaled_value, sensor->sensor_height, sensor->max_water_level, level_percentage);
        } else if (strcmp(sensor->sensor_type, "Radar Level") == 0) {
            // Radar Level sensor calculation: (Raw Value / Maximum Water Level) * 100
            double raw_scaled_value = test_result.scaled_value;
            double level_percentage = 0.0;
            
            if (sensor->max_water_level > 0) {
                level_percentage = (raw_scaled_value / sensor->max_water_level) * 100.0;
                // Ensure percentage is not negative (but allow over 100% to show overflow)
                if (level_percentage < 0) level_percentage = 0.0;
            }
            
            reading->value = level_percentage;
            ESP_LOGI(TAG, "Radar Level Sensor %s: Raw=%.6f, MaxLevel=%.2f -> %.2f%%", 
                     reading->unit_id, raw_scaled_value, sensor->max_water_level, level_percentage);
        } else if (strcmp(sensor->sensor_type, "ZEST") == 0) {
            // ZEST sensor uses the sensor_test_live function which handles the special format
            // The test_result.scaled_value already contains the combined integer + decimal value
            reading->value = test_result.scaled_value;
            ESP_LOGI(TAG, "ZEST Sensor %s: %.6f", reading->unit_id, reading->value);
        } else {
            // Flow-Meter or other sensor types use direct scaled value
            reading->value = test_result.scaled_value;
            ESP_LOGI(TAG, "Sensor %s: %.6f", reading->unit_id, reading->value);
        }
        
        reading->valid = true;
        reading->raw_value = test_result.raw_value;
        strncpy(reading->raw_hex, test_result.raw_hex, sizeof(reading->raw_hex) - 1);
        strncpy(reading->data_source, "modbus_rs485", sizeof(reading->data_source) - 1);
        reading->data_source[sizeof(reading->data_source) - 1] = '\0';
    } else {
        reading->valid = false;
        strncpy(reading->data_source, "error", sizeof(reading->data_source) - 1);
        reading->data_source[sizeof(reading->data_source) - 1] = '\0';
        ESP_LOGE(TAG, "Failed to read sensor %s: %s", reading->unit_id, test_result.error_message);
    }

    return ret;
}

// Read water quality sensor with multiple sub-parameters
esp_err_t sensor_read_quality(const sensor_config_t *sensor, sensor_reading_t *reading)
{
    if (!sensor || !reading) {
        return ESP_ERR_INVALID_ARG;
    }

    // Check if this is a water quality sensor
    if (strcmp(sensor->sensor_type, "QUALITY") != 0) {
        ESP_LOGE(TAG, "Sensor %s is not a QUALITY sensor", sensor->name);
        return ESP_ERR_INVALID_ARG;
    }

    // Clear reading
    memset(reading, 0, sizeof(sensor_reading_t));
    
    // Copy basic info
    strncpy(reading->unit_id, sensor->unit_id, sizeof(reading->unit_id) - 1);
    strncpy(reading->sensor_name, sensor->name, sizeof(reading->sensor_name) - 1);
    
    // Get timestamp
    time_t now;
    struct tm timeinfo;
    time(&now);
    gmtime_r(&now, &timeinfo);
    strftime(reading->timestamp, sizeof(reading->timestamp), "%Y-%m-%dT%H:%M:%SZ", &timeinfo);

    // Initialize default parameter values and validity flags
    reading->quality_params.ph_value = 0.0;
    reading->quality_params.tds_value = 0.0;
    reading->quality_params.temp_value = 0.0;
    reading->quality_params.humidity_value = 0.0;
    reading->quality_params.tss_value = 0.0;
    reading->quality_params.bod_value = 0.0;
    reading->quality_params.cod_value = 0.0;
    // All validity flags start as false
    reading->quality_params.ph_valid = false;
    reading->quality_params.tds_valid = false;
    reading->quality_params.temp_valid = false;
    reading->quality_params.humidity_valid = false;
    reading->quality_params.tss_valid = false;
    reading->quality_params.bod_valid = false;
    reading->quality_params.cod_valid = false;

    bool any_success = false;
    
    // Read each sub-sensor
    for (int i = 0; i < sensor->sub_sensor_count && i < 8; i++) {
        const sub_sensor_t *sub_sensor = &sensor->sub_sensors[i];
        
        if (!sub_sensor->enabled) {
            continue;
        }

        ESP_LOGI(TAG, "Reading sub-sensor %d: %s (Slave:%d, Reg:%d)", 
                 i, sub_sensor->parameter_name, sub_sensor->slave_id, sub_sensor->register_address);

        // Create a temporary sensor config for this sub-sensor
        sensor_config_t temp_sensor = *sensor;  // Copy main sensor config
        temp_sensor.slave_id = sub_sensor->slave_id;
        temp_sensor.register_address = sub_sensor->register_address;
        temp_sensor.quantity = sub_sensor->quantity;
        strncpy(temp_sensor.data_type, sub_sensor->data_type, sizeof(temp_sensor.data_type) - 1);
        strncpy(temp_sensor.register_type, sub_sensor->register_type, sizeof(temp_sensor.register_type) - 1);
        temp_sensor.scale_factor = sub_sensor->scale_factor;
        strncpy(temp_sensor.byte_order, sub_sensor->byte_order, sizeof(temp_sensor.byte_order) - 1);

        // Test this sub-sensor
        sensor_test_result_t test_result;
        esp_err_t ret = sensor_test_live(&temp_sensor, &test_result);
        
        if (ret == ESP_OK && test_result.success) {
            any_success = true;
            double scaled_value = test_result.scaled_value;
            
            // Map parameter to the correct field based on parameter_name
            // Use case-insensitive comparison for flexibility
            if (strcasecmp(sub_sensor->parameter_name, "pH") == 0 || strcasecmp(sub_sensor->parameter_name, "PH") == 0) {
                reading->quality_params.ph_value = scaled_value;
                reading->quality_params.ph_valid = true;
                ESP_LOGI(TAG, "pH: %.2f", scaled_value);
            } else if (strcasecmp(sub_sensor->parameter_name, "TDS") == 0 || strcasecmp(sub_sensor->parameter_name, "Conductivity") == 0) {
                reading->quality_params.tds_value = scaled_value;
                reading->quality_params.tds_valid = true;
                ESP_LOGI(TAG, "TDS/Conductivity: %.2f ppm", scaled_value);
            } else if (strcasecmp(sub_sensor->parameter_name, "Temp") == 0 || strcasecmp(sub_sensor->parameter_name, "Temperature") == 0) {
                reading->quality_params.temp_value = scaled_value;
                reading->quality_params.temp_valid = true;
                ESP_LOGI(TAG, "Temperature: %.2f°C", scaled_value);
            } else if (strcasecmp(sub_sensor->parameter_name, "HUMIDITY") == 0 || strcasecmp(sub_sensor->parameter_name, "Humidity") == 0) {
                reading->quality_params.humidity_value = scaled_value;
                reading->quality_params.humidity_valid = true;
                ESP_LOGI(TAG, "Humidity: %.2f%%", scaled_value);
            } else if (strcasecmp(sub_sensor->parameter_name, "TSS") == 0) {
                reading->quality_params.tss_value = scaled_value;
                reading->quality_params.tss_valid = true;
                ESP_LOGI(TAG, "TSS: %.2f mg/L", scaled_value);
            } else if (strcasecmp(sub_sensor->parameter_name, "BOD") == 0) {
                reading->quality_params.bod_value = scaled_value;
                reading->quality_params.bod_valid = true;
                ESP_LOGI(TAG, "BOD: %.2f mg/L", scaled_value);
            } else if (strcasecmp(sub_sensor->parameter_name, "COD") == 0) {
                reading->quality_params.cod_value = scaled_value;
                reading->quality_params.cod_valid = true;
                ESP_LOGI(TAG, "COD: %.2f mg/L", scaled_value);
            } else {
                ESP_LOGW(TAG, "Unknown parameter: %s (value=%.2f)", sub_sensor->parameter_name, scaled_value);
            }
        } else {
            ESP_LOGE(TAG, "Failed to read sub-sensor %s: %s", 
                     sub_sensor->parameter_name, test_result.error_message);
        }
    }

    if (any_success) {
        reading->valid = true;
        reading->value = reading->quality_params.ph_value; // Use pH as primary value
        strncpy(reading->data_source, "modbus_rs485_multi", sizeof(reading->data_source) - 1);
        reading->data_source[sizeof(reading->data_source) - 1] = '\0';
        ESP_LOGI(TAG, "Water Quality Sensor %s: pH=%.2f, TDS=%.2f, Temp=%.2fdegC, Humidity=%.2f%%, TSS=%.2f, BOD=%.2f, COD=%.2f",
                 reading->unit_id,
                 reading->quality_params.ph_value, reading->quality_params.tds_value,
                 reading->quality_params.temp_value, reading->quality_params.humidity_value,
                 reading->quality_params.tss_value, reading->quality_params.bod_value,
                 reading->quality_params.cod_value);
    } else {
        reading->valid = false;
        strncpy(reading->data_source, "error", sizeof(reading->data_source) - 1);
        reading->data_source[sizeof(reading->data_source) - 1] = '\0';
        ESP_LOGE(TAG, "All sub-sensors failed for water quality sensor %s", reading->unit_id);
    }

    return any_success ? ESP_OK : ESP_FAIL;
}

// Read Aquadax Quality sensor - single bulk read of 12 registers for 5 FLOAT32 parameters
esp_err_t sensor_read_aquadax_quality(const sensor_config_t *sensor, sensor_reading_t *reading)
{
    if (!sensor || !reading) {
        return ESP_ERR_INVALID_ARG;
    }

    if (strcmp(sensor->sensor_type, "Aquadax_Quality") != 0) {
        ESP_LOGE(TAG, "Sensor %s is not an Aquadax_Quality sensor", sensor->name);
        return ESP_ERR_INVALID_ARG;
    }

    // Clear reading
    memset(reading, 0, sizeof(sensor_reading_t));

    // Copy basic info
    strncpy(reading->unit_id, sensor->unit_id, sizeof(reading->unit_id) - 1);
    strncpy(reading->sensor_name, sensor->name, sizeof(reading->sensor_name) - 1);

    // Get timestamp
    time_t now;
    struct tm timeinfo;
    time(&now);
    gmtime_r(&now, &timeinfo);
    strftime(reading->timestamp, sizeof(reading->timestamp), "%Y-%m-%dT%H:%M:%SZ", &timeinfo);

    // Set baud rate
    int baud_rate = sensor->baud_rate > 0 ? sensor->baud_rate : 9600;
    modbus_set_baud_rate(baud_rate);

    // Get retry settings
    system_config_t *sys_config = get_system_config();
    int retry_count = sys_config ? sys_config->modbus_retry_count : 1;
    int retry_delay_ms = sys_config ? sys_config->modbus_retry_delay : 50;

    // Read 12 holding registers in a single bulk read
    modbus_result_t modbus_result;
    int attempt = 0;
    do {
        if (attempt > 0) {
            ESP_LOGW(TAG, "Retry %d/%d for Aquadax_Quality sensor '%s'",
                     attempt, retry_count, sensor->name);
            vTaskDelay(pdMS_TO_TICKS(retry_delay_ms));
        }
        modbus_result = modbus_read_holding_registers(sensor->slave_id,
                                                       sensor->register_address, 12);
        attempt++;
    } while (modbus_result != MODBUS_SUCCESS && attempt <= retry_count);

    if (modbus_result != MODBUS_SUCCESS) {
        reading->valid = false;
        strncpy(reading->data_source, "error", sizeof(reading->data_source) - 1);
        ESP_LOGE(TAG, "Aquadax_Quality: Modbus read failed after %d attempts", attempt);
        return ESP_FAIL;
    }

    // Get register values
    int reg_count = modbus_get_response_length();
    if (reg_count < 10) {
        reading->valid = false;
        strncpy(reading->data_source, "error", sizeof(reading->data_source) - 1);
        ESP_LOGE(TAG, "Aquadax_Quality: Insufficient registers (got %d, need 10)", reg_count);
        return ESP_FAIL;
    }

    uint16_t registers[12];
    for (int i = 0; i < reg_count && i < 12; i++) {
        registers[i] = modbus_get_response_buffer(i);
    }

    // Parse 5 FLOAT32 BIG_ENDIAN (ABCD) values: (reg[n] << 16) | reg[n+1]
    // Register map: COD(0-1), BOD(2-3), TSS(4-5), pH(6-7), Temperature(8-9)
    float params[5];
    const char *param_names[] = {"COD", "BOD", "TSS", "pH", "Temp"};
    for (int p = 0; p < 5; p++) {
        uint32_t float_bits = ((uint32_t)registers[p * 2] << 16) | registers[p * 2 + 1];
        memcpy(&params[p], &float_bits, sizeof(float));
        ESP_LOGI(TAG, "Aquadax_Quality %s: %.3f (raw: 0x%08lX)",
                 param_names[p], params[p], (unsigned long)float_bits);
    }

    // Map to quality_params_t
    reading->quality_params.cod_value = (double)params[0];
    reading->quality_params.cod_valid = true;
    reading->quality_params.bod_value = (double)params[1];
    reading->quality_params.bod_valid = true;
    reading->quality_params.tss_value = (double)params[2];
    reading->quality_params.tss_valid = true;
    reading->quality_params.ph_value = (double)params[3];
    reading->quality_params.ph_valid = true;
    reading->quality_params.temp_value = (double)params[4];
    reading->quality_params.temp_valid = true;

    // Primary value = COD (first parameter)
    reading->value = reading->quality_params.cod_value;
    reading->valid = true;
    strncpy(reading->data_source, "modbus_rs485_multi", sizeof(reading->data_source) - 1);
    reading->data_source[sizeof(reading->data_source) - 1] = '\0';

    ESP_LOGI(TAG, "Aquadax_Quality %s: COD=%.2f, BOD=%.2f, TSS=%.2f, pH=%.2f, Temp=%.2f",
             reading->unit_id,
             reading->quality_params.cod_value, reading->quality_params.bod_value,
             reading->quality_params.tss_value, reading->quality_params.ph_value,
             reading->quality_params.temp_value);

    return ESP_OK;
}

esp_err_t sensor_read_all_configured(sensor_reading_t *readings, int max_readings, int *actual_count)
{
    if (!readings || !actual_count || max_readings <= 0) {
        return ESP_ERR_INVALID_ARG;
    }

    system_config_t *config = get_system_config();
    *actual_count = 0;

    ESP_LOGI(TAG, "Reading all configured sensors (%d total)", config->sensor_count);

    for (int i = 0; i < config->sensor_count && *actual_count < max_readings; i++) {
        if (config->sensors[i].enabled) {
            ESP_LOGI(TAG, "Reading sensor %d: %s (Unit: %s, Slave: %d)",
                     i + 1, config->sensors[i].name, config->sensors[i].unit_id, config->sensors[i].slave_id);

            // Retry logic - try up to 3 times before giving up
            const int MAX_RETRIES = 3;
            const int RETRY_DELAY_MS = 500;  // Wait 500ms between retries
            bool read_success = false;

            for (int retry = 0; retry < MAX_RETRIES && !read_success; retry++) {
                if (retry > 0) {
                    ESP_LOGW(TAG, "Retry %d/%d for sensor %s...", retry, MAX_RETRIES - 1, config->sensors[i].unit_id);
                    vTaskDelay(pdMS_TO_TICKS(RETRY_DELAY_MS));
                }

                esp_err_t ret = sensor_read_single(&config->sensors[i], &readings[*actual_count]);
                if (ret == ESP_OK && readings[*actual_count].valid) {
                    ESP_LOGI(TAG, "Sensor %s read successfully: %.2f%s",
                             config->sensors[i].unit_id, readings[*actual_count].value,
                             retry > 0 ? " (after retry)" : "");
                    (*actual_count)++;
                    read_success = true;
                } else if (retry < MAX_RETRIES - 1) {
                    ESP_LOGW(TAG, "Sensor %s read attempt %d failed, will retry",
                             config->sensors[i].unit_id, retry + 1);
                }
            }

            if (!read_success) {
                ESP_LOGE(TAG, "Failed to read sensor %s after %d attempts",
                         config->sensors[i].unit_id, MAX_RETRIES);
            }
        } else {
            ESP_LOGW(TAG, "Sensor %d (%s) is disabled", i + 1, config->sensors[i].name);
        }
    }

    ESP_LOGI(TAG, "Successfully read %d/%d sensors", *actual_count, config->sensor_count);
    return ESP_OK;
}

// Utility functions
const char* get_register_type_description(const char* reg_type)
{
    if (strcmp(reg_type, "HOLDING") == 0) return "Holding Registers (0x03)";
    if (strcmp(reg_type, "INPUT") == 0) return "Input Registers (0x04)";
    return "Unknown";
}

const char* get_data_type_description(const char* data_type)
{
    if (strcmp(data_type, "UINT16") == 0) return "16-bit Unsigned Integer";
    if (strcmp(data_type, "INT16") == 0) return "16-bit Signed Integer";
    if (strcmp(data_type, "UINT32") == 0) return "32-bit Unsigned Integer";
    if (strcmp(data_type, "INT32") == 0) return "32-bit Signed Integer";
    if (strcmp(data_type, "FLOAT32") == 0) return "32-bit IEEE 754 Float";
    return "Unknown";
}

const char* get_byte_order_description(const char* byte_order)
{
    if (strcmp(byte_order, "BIG_ENDIAN") == 0) return "Big Endian (ABCD)";
    if (strcmp(byte_order, "LITTLE_ENDIAN") == 0) return "Little Endian (CDAB)";
    if (strcmp(byte_order, "MIXED_BADC") == 0) return "Mixed Byte Swap (BADC)";
    if (strcmp(byte_order, "MIXED_DCBA") == 0) return "Mixed Full Reverse (DCBA)";
    return "Unknown";
}

// ============================================================================
// CALCULATION ENGINE - User-friendly calculated fields for complex sensors
// ============================================================================

// Human-readable names for calculation types (for web UI dropdown)
const char* CALC_TYPE_NAMES[] = {
    "None (Direct Value)",                    // CALC_NONE
    "Combine Two Registers (HIGH×N + LOW)",   // CALC_COMBINE_REGISTERS
    "Scale and Offset",                       // CALC_SCALE_OFFSET
    "Level to Percentage",                    // CALC_LEVEL_PERCENTAGE
    "Cylinder Tank Volume",                   // CALC_CYLINDER_VOLUME
    "Rectangle Tank Volume",                  // CALC_RECTANGLE_VOLUME
    "Difference (A - B)",                     // CALC_DIFFERENCE
    "Flow Rate from Pulses",                  // CALC_FLOW_RATE_PULSE
    "Linear Interpolation",                   // CALC_LINEAR_INTERPOLATION
    "Polynomial (ax² + bx + c)",              // CALC_POLYNOMIAL
    "Integer + Decimal (Flow Meter)"          // CALC_FLOW_INT_DECIMAL
};

// Descriptions for each calculation type (for web UI help text)
const char* CALC_TYPE_DESCRIPTIONS[] = {
    "Use the raw sensor value without any calculation",
    "For sensors like Vortex flowmeter: Total = (HIGH register × multiplier) + LOW register",
    "Apply linear transformation: Result = (Raw × Scale) + Offset",
    "Convert sensor reading to tank fill percentage (0-100%)",
    "Calculate volume in a cylindrical tank from level reading",
    "Calculate volume in a rectangular tank from level reading",
    "Subtract one sensor value from another (e.g., inlet - outlet)",
    "Calculate flow rate from pulse count: Flow = Pulses / Pulses_per_unit",
    "Map input range to output range (e.g., 4-20mA to 0-100%)",
    "Apply quadratic formula for non-linear sensor calibration",
    "Combine integer and decimal parts: Total = Integer + (Decimal × scale)"
};

// Get calculation type name for display
const char* get_calculation_type_name(calculation_type_t type)
{
    if (type >= 0 && type < CALC_TYPE_MAX) {
        return CALC_TYPE_NAMES[type];
    }
    return "Unknown";
}

// Get calculation type description
const char* get_calculation_type_description(calculation_type_t type)
{
    if (type >= 0 && type < CALC_TYPE_MAX) {
        return CALC_TYPE_DESCRIPTIONS[type];
    }
    return "";
}

// Initialize calculation parameters with sensible defaults
void init_default_calculation_params(calculation_params_t *params)
{
    if (!params) return;

    memset(params, 0, sizeof(calculation_params_t));

    params->calc_type = CALC_NONE;

    // CALC_COMBINE_REGISTERS defaults (for Vortex-style sensors)
    params->high_register_offset = 0;
    params->low_register_offset = 2;
    params->combine_multiplier = 100.0f;

    // CALC_SCALE_OFFSET defaults
    params->scale = 1.0f;
    params->offset = 0.0f;

    // CALC_LEVEL_PERCENTAGE defaults
    params->tank_empty_value = 0.0f;
    params->tank_full_value = 100.0f;
    params->invert_level = false;

    // Tank volume defaults (1m x 1m x 1m = 1000 liters)
    params->tank_diameter = 1.0f;
    params->tank_length = 1.0f;
    params->tank_width = 1.0f;
    params->tank_height = 1.0f;
    params->volume_unit = 0;  // liters

    // CALC_DIFFERENCE default
    params->secondary_sensor_index = -1;

    // CALC_FLOW_RATE_PULSE default
    params->pulses_per_unit = 1.0f;

    // CALC_LINEAR_INTERPOLATION defaults (4-20mA to 0-100%)
    params->input_min = 4.0f;
    params->input_max = 20.0f;
    params->output_min = 0.0f;
    params->output_max = 100.0f;

    // CALC_POLYNOMIAL defaults (y = x, no transformation)
    params->poly_a = 0.0f;
    params->poly_b = 1.0f;
    params->poly_c = 0.0f;

    // Output defaults
    strcpy(params->output_unit, "");
    params->decimal_places = 2;
}

// Helper function to extract float from registers with byte order support
static float extract_float_from_registers(const uint16_t *registers, int offset, const char *byte_order)
{
    uint32_t combined;

    if (strcmp(byte_order, "BIG_ENDIAN") == 0 || strcmp(byte_order, "1234") == 0) {
        // ABCD - reg[0] is high word
        combined = ((uint32_t)registers[offset] << 16) | registers[offset + 1];
    } else if (strcmp(byte_order, "LITTLE_ENDIAN") == 0 || strcmp(byte_order, "4321") == 0) {
        // DCBA - reg[1] is high word
        combined = ((uint32_t)registers[offset + 1] << 16) | registers[offset];
    } else if (strcmp(byte_order, "MIXED_BADC") == 0 || strcmp(byte_order, "2143") == 0) {
        // BADC - byte swap within each register
        uint16_t reg0_swapped = ((registers[offset] & 0xFF) << 8) | ((registers[offset] >> 8) & 0xFF);
        uint16_t reg1_swapped = ((registers[offset + 1] & 0xFF) << 8) | ((registers[offset + 1] >> 8) & 0xFF);
        combined = ((uint32_t)reg0_swapped << 16) | reg1_swapped;
    } else if (strcmp(byte_order, "3412") == 0) {
        // CDAB - word swap (common in many sensors like Vortex)
        combined = ((uint32_t)registers[offset + 1] << 16) | registers[offset];
    } else {
        // Default to big endian
        combined = ((uint32_t)registers[offset] << 16) | registers[offset + 1];
    }

    float result;
    memcpy(&result, &combined, sizeof(float));
    return result;
}

// Main calculation function - applies the configured calculation to raw sensor data
double apply_calculation(const sensor_config_t *sensor, double raw_value,
                        const uint16_t *all_registers, int register_count)
{
    if (!sensor) {
        return raw_value;
    }

    const calculation_params_t *calc = &sensor->calculation;
    double result = raw_value;

    switch (calc->calc_type) {
        case CALC_NONE:
            // No calculation, return raw value (already scaled by convert_modbus_data)
            result = raw_value;
            break;

        case CALC_COMBINE_REGISTERS:
            // Combine two register values: HIGH × multiplier + LOW
            // Example: Vortex flowmeter cumulative = (reg[8-9] × 100) + reg[10-11]
            if (all_registers && register_count >= (calc->low_register_offset + 2)) {
                float high_value = extract_float_from_registers(
                    all_registers, calc->high_register_offset, sensor->byte_order);
                float low_value = extract_float_from_registers(
                    all_registers, calc->low_register_offset, sensor->byte_order);

                result = (high_value * calc->combine_multiplier) + low_value;

                ESP_LOGI(TAG, "CALC_COMBINE: HIGH[%d]=%.4f × %.1f + LOW[%d]=%.4f = %.4f",
                         calc->high_register_offset, high_value, calc->combine_multiplier,
                         calc->low_register_offset, low_value, result);
            } else {
                ESP_LOGW(TAG, "CALC_COMBINE: Insufficient registers (have %d, need %d)",
                         register_count, calc->low_register_offset + 2);
                result = raw_value;
            }
            break;

        case CALC_SCALE_OFFSET:
            // Linear transformation: (raw × scale) + offset
            result = (raw_value * calc->scale) + calc->offset;
            ESP_LOGI(TAG, "CALC_SCALE_OFFSET: %.4f × %.4f + %.4f = %.4f",
                     raw_value, calc->scale, calc->offset, result);
            break;

        case CALC_LEVEL_PERCENTAGE:
            // Convert level reading to percentage
            // Handles both normal (full = high reading) and inverted (full = low reading) sensors
            if (calc->tank_full_value != calc->tank_empty_value) {
                if (calc->invert_level) {
                    // Inverted: sensor reads higher when tank is empty (e.g., ultrasonic distance)
                    result = ((calc->tank_empty_value - raw_value) /
                              (calc->tank_empty_value - calc->tank_full_value)) * 100.0;
                } else {
                    // Normal: sensor reads higher when tank is full (e.g., pressure sensor)
                    result = ((raw_value - calc->tank_empty_value) /
                              (calc->tank_full_value - calc->tank_empty_value)) * 100.0;
                }
                // Clamp to 0-100%
                if (result < 0) result = 0;
                if (result > 100) result = 100;

                ESP_LOGI(TAG, "CALC_LEVEL_PCT: raw=%.4f, empty=%.4f, full=%.4f, invert=%d -> %.2f%%",
                         raw_value, calc->tank_empty_value, calc->tank_full_value,
                         calc->invert_level, result);
            }
            break;

        case CALC_CYLINDER_VOLUME:
            // Volume = π × r² × level
            // level is calculated from raw_value as percentage, then converted to actual height
            {
                double level_percent = raw_value;  // Assume raw_value is already percentage
                double level_height = (level_percent / 100.0) * calc->tank_height;
                double radius = calc->tank_diameter / 2.0;
                double volume_m3 = M_PI * radius * radius * level_height;

                // Convert to requested unit
                switch (calc->volume_unit) {
                    case 0: result = volume_m3 * 1000.0; break;  // liters
                    case 1: result = volume_m3; break;           // m³
                    case 2: result = volume_m3 * 264.172; break; // US gallons
                    default: result = volume_m3 * 1000.0; break;
                }

                ESP_LOGI(TAG, "CALC_CYLINDER: level=%.2f%%, height=%.2fm, dia=%.2fm -> %.2f %s",
                         level_percent, calc->tank_height, calc->tank_diameter, result,
                         calc->volume_unit == 0 ? "L" : (calc->volume_unit == 1 ? "m³" : "gal"));
            }
            break;

        case CALC_RECTANGLE_VOLUME:
            // Volume = L × W × level
            {
                double level_percent = raw_value;  // Assume raw_value is already percentage
                double level_height = (level_percent / 100.0) * calc->tank_height;
                double volume_m3 = calc->tank_length * calc->tank_width * level_height;

                // Convert to requested unit
                switch (calc->volume_unit) {
                    case 0: result = volume_m3 * 1000.0; break;  // liters
                    case 1: result = volume_m3; break;           // m³
                    case 2: result = volume_m3 * 264.172; break; // US gallons
                    default: result = volume_m3 * 1000.0; break;
                }

                ESP_LOGI(TAG, "CALC_RECTANGLE: level=%.2f%%, L=%.2f, W=%.2f, H=%.2f -> %.2f %s",
                         level_percent, calc->tank_length, calc->tank_width, calc->tank_height, result,
                         calc->volume_unit == 0 ? "L" : (calc->volume_unit == 1 ? "m³" : "gal"));
            }
            break;

        case CALC_DIFFERENCE:
            // This requires access to another sensor's value
            // For now, just return raw value - full implementation needs sensor array access
            ESP_LOGW(TAG, "CALC_DIFFERENCE: Secondary sensor reference not implemented in this context");
            result = raw_value;
            break;

        case CALC_FLOW_RATE_PULSE:
            // Convert pulse count to flow units
            if (calc->pulses_per_unit > 0) {
                result = raw_value / calc->pulses_per_unit;
                ESP_LOGI(TAG, "CALC_PULSE: %.4f pulses / %.4f = %.4f units",
                         raw_value, calc->pulses_per_unit, result);
            }
            break;

        case CALC_LINEAR_INTERPOLATION:
            // Map input range to output range
            // Example: 4-20mA -> 0-100%
            if (calc->input_max != calc->input_min) {
                double normalized = (raw_value - calc->input_min) / (calc->input_max - calc->input_min);
                result = calc->output_min + normalized * (calc->output_max - calc->output_min);

                ESP_LOGI(TAG, "CALC_LINEAR_INTERP: %.4f [%.1f-%.1f] -> %.4f [%.1f-%.1f]",
                         raw_value, calc->input_min, calc->input_max,
                         result, calc->output_min, calc->output_max);
            }
            break;

        case CALC_POLYNOMIAL:
            // Quadratic: y = ax² + bx + c
            result = (calc->poly_a * raw_value * raw_value) +
                     (calc->poly_b * raw_value) +
                     calc->poly_c;
            ESP_LOGI(TAG, "CALC_POLY: %.4f × %.4f² + %.4f × %.4f + %.4f = %.4f",
                     calc->poly_a, raw_value, calc->poly_b, raw_value, calc->poly_c, result);
            break;

        case CALC_FLOW_INT_DECIMAL:
            // Combine integer and decimal parts: Total = Integer + (Decimal × scale)
            // Example: Integer=12345, Decimal=678, Scale=0.001 -> 12345 + 0.678 = 12345.678
            if (all_registers && register_count >= (calc->low_register_offset + 2)) {
                float int_value = extract_float_from_registers(
                    all_registers, calc->high_register_offset, sensor->byte_order);
                float dec_value = extract_float_from_registers(
                    all_registers, calc->low_register_offset, sensor->byte_order);

                // Scale defaults to 0.001 if not specified (to handle 3-digit decimals)
                float dec_scale = (calc->scale > 0) ? calc->scale : 0.001;
                result = int_value + (dec_value * dec_scale);

                ESP_LOGI(TAG, "CALC_FLOW_INT_DEC: Int[%d]=%.0f + Dec[%d]=%.0f × %.6f = %.4f",
                         calc->high_register_offset, int_value,
                         calc->low_register_offset, dec_value, dec_scale, result);
            } else {
                ESP_LOGW(TAG, "CALC_FLOW_INT_DEC: Insufficient registers (have %d, need %d)",
                         register_count, calc->low_register_offset + 2);
                result = raw_value;
            }
            break;

        default:
            ESP_LOGW(TAG, "Unknown calculation type: %d", calc->calc_type);
            result = raw_value;
            break;
    }

    // Apply decimal places rounding if specified
    if (calc->decimal_places >= 0 && calc->decimal_places <= 6) {
        double multiplier = pow(10.0, calc->decimal_places);
        result = round(result * multiplier) / multiplier;
    }

    return result;
}