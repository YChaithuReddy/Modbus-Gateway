// json_templates.h - JSON template definitions for different sensor types

#ifndef JSON_TEMPLATES_H
#define JSON_TEMPLATES_H

#include <stdint.h>
#include <time.h>
#include "sensor_manager.h"

// Maximum JSON payload size
#define MAX_JSON_PAYLOAD_SIZE 1024  // Increased to support larger individual sensor JSON

// JSON template types
typedef enum {
    JSON_TYPE_FLOW,
    JSON_TYPE_LEVEL,
    JSON_TYPE_RAINGAUGE,
    JSON_TYPE_BOREWELL,
    JSON_TYPE_ENERGY,
    JSON_TYPE_QUALITY,
    JSON_TYPE_ZEST,
    JSON_TYPE_UNKNOWN
} json_template_type_t;

// Structure to hold JSON generation parameters
typedef struct {
    json_template_type_t type;
    char unit_id[16];
    double scaled_value;
    uint32_t raw_value;
    char timestamp[32];
    int slave_id;
    // Network telemetry fields (NEW)
    int signal_strength;          // Signal strength in dBm (WiFi RSSI or SIM RSSI)
    char network_type[16];        // "WiFi" or "4G"
    char network_quality[16];     // "Excellent", "Good", "Fair", "Poor", "Unknown"
    // Additional parameters for specific types
    struct {
        double ph_value;          // For QUALITY type
        double tds_value;         // For QUALITY type
        double temp_value;        // For QUALITY type
        double humidity_value;    // For QUALITY type
        double tss_value;         // For QUALITY type (TSS - Total Suspended Solids)
        double bod_value;         // For QUALITY type (BOD - Biochemical Oxygen Demand)
        double cod_value;         // For QUALITY type (COD - Chemical Oxygen Demand)
        char hex_string[16];      // For ENERGY type
        char meter_id[32];        // For ENERGY type
    } extra_params;
} json_params_t;

// Include network statistics structure
#include "network_stats.h"

// Function prototypes
json_template_type_t get_json_type_from_sensor_type(const char* sensor_type);
esp_err_t generate_sensor_json(const sensor_config_t* sensor, double scaled_value,
                              uint32_t raw_value, const network_stats_t* net_stats,
                              char* json_buffer, size_t buffer_size);
esp_err_t generate_sensor_json_with_hex(const sensor_config_t* sensor, double scaled_value,
                              uint32_t raw_value, const char* hex_string,
                              const network_stats_t* net_stats,
                              char* json_buffer, size_t buffer_size);
esp_err_t generate_quality_sensor_json(const sensor_reading_t* reading, char* json_buffer, size_t buffer_size);
esp_err_t create_json_payload(const json_params_t* params, char* json_buffer, size_t buffer_size);
const char* get_json_template_name(json_template_type_t type);

// Utility functions
void format_timestamp_iso8601(char* timestamp, size_t size);
void format_timestamp_epoch(uint32_t* epoch_time);
esp_err_t validate_json_params(const json_params_t* params);

#endif // JSON_TEMPLATES_H