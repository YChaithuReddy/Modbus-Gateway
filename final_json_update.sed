# SED script to add signal strength fields to all JSON templates

# Update FLOW type - line 115
115 s/"unit_id":"%s"/"unit_id":"%s","signal_strength":%d,"network_type":"%s","network_quality":"%s"/

# Update FLOW type parameters - line 119
119 s/params->unit_id);/params->unit_id,\n                params->signal_strength,\n                params->network_type,\n                params->network_quality);/

# Update LEVEL type - line 129
129 s/"unit_id":"%s"/"unit_id":"%s","signal_strength":%d,"network_type":"%s","network_quality":"%s"/

# Update LEVEL type parameters - line 133
133 s/params->unit_id);/params->unit_id,\n                params->signal_strength,\n                params->network_type,\n                params->network_quality);/

# Update RAINGAUGE type - line 143
143 s/"unit_id":"%s"/"unit_id":"%s","signal_strength":%d,"network_type":"%s","network_quality":"%s"/

# Update RAINGAUGE type parameters - line 147
147 s/params->unit_id);/params->unit_id,\n                params->signal_strength,\n                params->network_type,\n                params->network_quality);/

# Update BOREWELL type - line 157
157 s/"unit_id":"%s"/"unit_id":"%s","signal_strength":%d,"network_type":"%s","network_quality":"%s"/

# Update BOREWELL type parameters - line 161
161 s/params->unit_id);/params->unit_id,\n                params->signal_strength,\n                params->network_type,\n                params->network_quality);/

# Update ENERGY type - line 194
194 s/"meter":"%s"/"meter":"%s","signal_strength":%d,"network_type":"%s","network_quality":"%s"/

# Update ENERGY type parameters - line 199
199 s/: params->unit_id);/: params->unit_id,\n                params->signal_strength,\n                params->network_type,\n                params->network_quality);/

# Update QUALITY type - line 226
226 s/"unit_id":"%s"/"unit_id":"%s","signal_strength":%d,"network_type":"%s","network_quality":"%s"/

# Update QUALITY type parameters - line 236
236 s/params->unit_id);/params->unit_id,\n                params->signal_strength,\n                params->network_type,\n                params->network_quality);/
