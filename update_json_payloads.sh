#!/bin/bash

# Script to add signal strength fields to all JSON templates

cd "/c/Users/chath/OneDrive/Documents/Python code/modbus_iot_gateway/main"

# Backup original
cp json_templates.c json_templates.c.bak4

# Use Python to update the JSON templates
python3 << 'PYTHON_SCRIPT'
import re

# Read the file
with open('json_templates.c', 'r') as f:
    content = f.read()

# Update JSON_TYPE_FLOW (lines around 111-119)
old_flow = '''        case JSON_TYPE_FLOW: {
            // {"consumption":1234,"created_on":"2023-12-10T12:58:57Z","unit_id":"TFG2235F"}
            snprintf(json_buffer, buffer_size,
                "{"
                "\"consumption\":%.2f,"
                "\"created_on\":\"%s\","
                "\"unit_id\":\"%s\""
                "}",
                params->scaled_value,
                params->timestamp,
                params->unit_id);
            break;
        }'''

new_flow = '''        case JSON_TYPE_FLOW: {
            // {"consumption":1234,"created_on":"2023-12-10T12:58:57Z","unit_id":"TFG2235F","signal_strength":-73,"network_type":"WiFi","network_quality":"Good"}
            snprintf(json_buffer, buffer_size,
                "{"
                "\"consumption\":%.2f,"
                "\"created_on\":\"%s\","
                "\"unit_id\":\"%s\","
                "\"signal_strength\":%d,"
                "\"network_type\":\"%s\","
                "\"network_quality\":\"%s\""
                "}",
                params->scaled_value,
                params->timestamp,
                params->unit_id,
                params->signal_strength,
                params->network_type,
                params->network_quality);
            break;
        }'''

content = content.replace(old_flow, new_flow)

# Update JSON_TYPE_LEVEL
old_level = '''        case JSON_TYPE_LEVEL: {
            // {"level_filled":50,"created_on":"2023-12-10T12:58:57Z","unit_id":"TFG2235L"}
            snprintf(json_buffer, buffer_size,
                "{"
                "\"level_filled\":%.2f,"
                "\"created_on\":\"%s\","
                "\"unit_id\":\"%s\""
                "}",
                params->scaled_value,
                params->timestamp,
                params->unit_id);
            break;
        }'''

new_level = '''        case JSON_TYPE_LEVEL: {
            // {"level_filled":50,"created_on":"2023-12-10T12:58:57Z","unit_id":"TFG2235L","signal_strength":-73,"network_type":"WiFi","network_quality":"Good"}
            snprintf(json_buffer, buffer_size,
                "{"
                "\"level_filled\":%.2f,"
                "\"created_on\":\"%s\","
                "\"unit_id\":\"%s\","
                "\"signal_strength\":%d,"
                "\"network_type\":\"%s\","
                "\"network_quality\":\"%s\""
                "}",
                params->scaled_value,
                params->timestamp,
                params->unit_id,
                params->signal_strength,
                params->network_type,
                params->network_quality);
            break;
        }'''

content = content.replace(old_level, new_level)

# Update JSON_TYPE_RAINGAUGE
old_raingauge = '''        case JSON_TYPE_RAINGAUGE: {
            // {"raingauge":1234,"created_on":"2023-12-10T12:58:57Z","unit_id":"TFG2235F"}
            snprintf(json_buffer, buffer_size,
                "{"
                "\"raingauge\":%.2f,"
                "\"created_on\":\"%s\","
                "\"unit_id\":\"%s\""
                "}",
                params->scaled_value,
                params->timestamp,
                params->unit_id);
            break;
        }'''

new_raingauge = '''        case JSON_TYPE_RAINGAUGE: {
            // {"raingauge":1234,"created_on":"2023-12-10T12:58:57Z","unit_id":"TFG2235F","signal_strength":-73,"network_type":"WiFi","network_quality":"Good"}
            snprintf(json_buffer, buffer_size,
                "{"
                "\"raingauge\":%.2f,"
                "\"created_on\":\"%s\","
                "\"unit_id\":\"%s\","
                "\"signal_strength\":%d,"
                "\"network_type\":\"%s\","
                "\"network_quality\":\"%s\""
                "}",
                params->scaled_value,
                params->timestamp,
                params->unit_id,
                params->signal_strength,
                params->network_type,
                params->network_quality);
            break;
        }'''

content = content.replace(old_raingauge, new_raingauge)

# Update JSON_TYPE_BOREWELL
old_borewell = '''        case JSON_TYPE_BOREWELL: {
            // {"borewell":1234,"created_on":"2023-12-10T12:58:57Z","unit_id":"TFG2235L"}
            snprintf(json_buffer, buffer_size,
                "{"
                "\"borewell\":%.2f,"
                "\"created_on\":\"%s\","
                "\"unit_id\":\"%s\""
                "}",
                params->scaled_value,
                params->timestamp,
                params->unit_id);
            break;
        }'''

new_borewell = '''        case JSON_TYPE_BOREWELL: {
            // {"borewell":1234,"created_on":"2023-12-10T12:58:57Z","unit_id":"TFG2235L","signal_strength":-73,"network_type":"WiFi","network_quality":"Good"}
            snprintf(json_buffer, buffer_size,
                "{"
                "\"borewell\":%.2f,"
                "\"created_on\":\"%s\","
                "\"unit_id\":\"%s\","
                "\"signal_strength\":%d,"
                "\"network_type\":\"%s\","
                "\"network_quality\":\"%s\""
                "}",
                params->scaled_value,
                params->timestamp,
                params->unit_id,
                params->signal_strength,
                params->network_type,
                params->network_quality);
            break;
        }'''

content = content.replace(old_borewell, new_borewell)

# Write the updated content
with open('json_templates.c', 'w') as f:
    f.write(content)

print("Updated FLOW, LEVEL, RAINGAUGE, and BOREWELL JSON templates")

PYTHON_SCRIPT

echo "Done! Backup saved as json_templates.c.bak4"
echo "Note: ENERGY and QUALITY types use different JSON formats and may need manual updates"
