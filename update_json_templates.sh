#!/bin/bash

# Script to update json_templates.c with network stats support

cd "/c/Users/chath/OneDrive/Documents/Python code/modbus_iot_gateway/main"

# Create a temporary file with the updated generate_sensor_json function
cat > temp_function1.txt << 'EOF'
// Generate JSON for a sensor configuration with real data
esp_err_t generate_sensor_json(const sensor_config_t* sensor, double scaled_value,
                              uint32_t raw_value, const network_stats_t* net_stats,
                              char* json_buffer, size_t buffer_size)
{
    if (!sensor || !json_buffer || buffer_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Generating JSON for sensor: %s (Type: %s, Unit: %s)",
             sensor->name, sensor->sensor_type, sensor->unit_id);

    // Prepare JSON parameters
    json_params_t params = {0};

    // Determine JSON template type from sensor type
    params.type = get_json_type_from_sensor_type(sensor->sensor_type);
    if (params.type == JSON_TYPE_UNKNOWN) {
        ESP_LOGE(TAG, "Unknown sensor type: %s", sensor->sensor_type);
        return ESP_ERR_INVALID_ARG;
    }

    // Copy basic parameters
    strncpy(params.unit_id, sensor->unit_id, sizeof(params.unit_id) - 1);
    params.scaled_value = scaled_value;
    params.raw_value = raw_value;
    params.slave_id = sensor->slave_id;

    // Format timestamp
    format_timestamp_iso8601(params.timestamp, sizeof(params.timestamp));

    // Add network telemetry data
    if (net_stats) {
        params.signal_strength = net_stats->signal_strength;
        strncpy(params.network_type, net_stats->network_type, sizeof(params.network_type) - 1);

        // Determine quality based on signal strength
        if (net_stats->signal_strength >= -60) {
            strncpy(params.network_quality, "Excellent", sizeof(params.network_quality) - 1);
        } else if (net_stats->signal_strength >= -70) {
            strncpy(params.network_quality, "Good", sizeof(params.network_quality) - 1);
        } else if (net_stats->signal_strength >= -80) {
            strncpy(params.network_quality, "Fair", sizeof(params.network_quality) - 1);
        } else {
            strncpy(params.network_quality, "Poor", sizeof(params.network_quality) - 1);
        }
    } else {
        // Default values when network stats unavailable
        params.signal_strength = 0;
        strncpy(params.network_type, "Unknown", sizeof(params.network_type) - 1);
        strncpy(params.network_quality, "Unknown", sizeof(params.network_quality) - 1);
    }

    // Set additional parameters for specific types
    if (params.type == JSON_TYPE_ENERGY) {
        // For ENERGY type, use meter_type if available, otherwise fallback to sensor name
        if (strlen(sensor->meter_type) > 0) {
            strncpy(params.extra_params.meter_id, sensor->meter_type, sizeof(params.extra_params.meter_id) - 1);
        } else if (strlen(sensor->name) > 0) {
            strncpy(params.extra_params.meter_id, sensor->name, sizeof(params.extra_params.meter_id) - 1);
        }
    } else if (params.type == JSON_TYPE_QUALITY) {
        // For QUALITY type, use scaled_value as primary parameter and set reasonable defaults
        params.extra_params.ph_value = scaled_value;
        params.extra_params.tds_value = scaled_value * 10; // Example conversion
        params.extra_params.temp_value = 25.0; // Default temperature
        params.extra_params.humidity_value = 60.0; // Default humidity
        params.extra_params.tss_value = 10.0; // Default TSS
        params.extra_params.bod_value = 5.0; // Default BOD
        params.extra_params.cod_value = 8.0; // Default COD
    }

    // Generate the JSON
    esp_err_t ret = create_json_payload(&params, json_buffer, buffer_size);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create JSON payload for sensor %s", sensor->name);
        return ret;
    }

    ESP_LOGI(TAG, "JSON generated for sensor %s: %s", sensor->name, json_buffer);
    return ESP_OK;
}
EOF

# Now extract the file in 3 parts and reassemble
head -250 json_templates.c > temp_part1.c
tail -n +309 json_templates.c > temp_part3.c

# Combine: part1 + new function + part3
cat temp_part1.c temp_function1.txt temp_part3.c > json_templates_new.c

# Backup original
cp json_templates.c json_templates.c.bak2

# Replace original
mv json_templates_new.c json_templates.c

echo "Updated generate_sensor_json function"

# Clean up temp files
rm temp_function1.txt temp_part1.c temp_part3.c

echo "Done! Backup saved as json_templates.c.bak2"
