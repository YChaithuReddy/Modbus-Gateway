#!/usr/bin/awk -f

# AWK script to update JSON format strings

NR == 115 {
    gsub(/"unit_id":"%s""/, "\"unit_id\":\"%s\",\"signal_strength\":%d,\"network_type\":\"%s\",\"network_quality\":\"%s\"\"")
}

NR == 132 {
    gsub(/"unit_id":"%s""/, "\"unit_id\":\"%s\",\"signal_strength\":%d,\"network_type\":\"%s\",\"network_quality\":\"%s\"\"")
}

NR == 149 {
    gsub(/"unit_id":"%s""/, "\"unit_id\":\"%s\",\"signal_strength\":%d,\"network_type\":\"%s\",\"network_quality\":\"%s\"\"")
}

NR == 166 {
    gsub(/"unit_id":"%s""/, "\"unit_id\":\"%s\",\"signal_strength\":%d,\"network_type\":\"%s\",\"network_quality\":\"%s\"\"")
}

NR == 207 {
    gsub(/"meter":"%s""/, "\"meter\":\"%s\",\"signal_strength\":%d,\"network_type\":\"%s\",\"network_quality\":\"%s\"\"")
}

NR == 243 {
    gsub(/"unit_id":"%s""/, "\"unit_id\":\"%s\",\"signal_strength\":%d,\"network_type\":\"%s\",\"network_quality\":\"%s\"\"")
}

{ print }
