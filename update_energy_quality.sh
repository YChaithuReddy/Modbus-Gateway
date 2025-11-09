#!/bin/bash

# Script to add signal strength fields to ENERGY and QUALITY JSON templates

cd "/c/Users/chath/OneDrive/Documents/Python code/modbus_iot_gateway/main"

# Backup original
cp json_templates.c json_templates.c.bak5

# Use Python to update ENERGY and QUALITY types
python3 << 'PYTHON_SCRIPT'
# Read the file
with open('json_templates.c', 'r') as f:
    content = f.read()

# Update JSON_TYPE_ENERGY
old_energy = '''            snprintf(json_buffer, buffer_size,
                "{"
                "\"ene_con_hex\":\"%s\","
                "\"type\":\"ENERGY\","
                "\"created_on_epoch\":%" PRIu32 ","
                "\"slave_id\":%d,"
                "\"meter\":\"%s\""
                "}",
                hex_value,
                epoch_time,
                params->slave_id,
                strlen(params->extra_params.meter_id) > 0 ? params->extra_params.meter_id : params->unit_id);'''

new_energy = '''            snprintf(json_buffer, buffer_size,
                "{"
                "\"ene_con_hex\":\"%s\","
                "\"type\":\"ENERGY\","
                "\"created_on_epoch\":%" PRIu32 ","
                "\"slave_id\":%d,"
                "\"meter\":\"%s\","
                "\"signal_strength\":%d,"
                "\"network_type\":\"%s\","
                "\"network_quality\":\"%s\""
                "}",
                hex_value,
                epoch_time,
                params->slave_id,
                strlen(params->extra_params.meter_id) > 0 ? params->extra_params.meter_id : params->unit_id,
                params->signal_strength,
                params->network_type,
                params->network_quality);'''

content = content.replace(old_energy, new_energy)

# Update JSON_TYPE_QUALITY
old_quality = '''            snprintf(json_buffer, buffer_size,
                "{"
                "\"params_data\":{"
                "\"pH\":%.2f,"
                "\"TDS\":%.2f,"
                "\"Temp\":%.2f,"
                "\"HUMIDITY\":%.2f,"
                "\"TSS\":%.2f,"
                "\"BOD\":%.2f,"
                "\"COD\":%.2f"
                "},"
                "\"type\":\"QUALITY\","
                "\"created_on\":\"%s\","
                "\"unit_id\":\"%s\""
                "}",
                ph_value,
                tds_value,
                temp_value,
                humidity_value,
                tss_value,
                bod_value,
                cod_value,
                params->timestamp,
                params->unit_id);'''

new_quality = '''            snprintf(json_buffer, buffer_size,
                "{"
                "\"params_data\":{"
                "\"pH\":%.2f,"
                "\"TDS\":%.2f,"
                "\"Temp\":%.2f,"
                "\"HUMIDITY\":%.2f,"
                "\"TSS\":%.2f,"
                "\"BOD\":%.2f,"
                "\"COD\":%.2f"
                "},"
                "\"type\":\"QUALITY\","
                "\"created_on\":\"%s\","
                "\"unit_id\":\"%s\","
                "\"signal_strength\":%d,"
                "\"network_type\":\"%s\","
                "\"network_quality\":\"%s\""
                "}",
                ph_value,
                tds_value,
                temp_value,
                humidity_value,
                tss_value,
                bod_value,
                cod_value,
                params->timestamp,
                params->unit_id,
                params->signal_strength,
                params->network_type,
                params->network_quality);'''

content = content.replace(old_quality, new_quality)

# Write the updated content
with open('json_templates.c', 'w') as f:
    f.write(content)

print("Updated ENERGY and QUALITY JSON templates with signal strength fields")

PYTHON_SCRIPT

echo "Done! Backup saved as json_templates.c.bak5"
echo "All JSON templates now include signal_strength, network_type, and network_quality fields"
