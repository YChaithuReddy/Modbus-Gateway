// network_stats.h - Network statistics structure for telemetry
#ifndef NETWORK_STATS_H
#define NETWORK_STATS_H

// Network statistics structure
typedef struct {
    int signal_strength;
    char network_type[16];
    char network_quality[16];
    char operator_name[64];
} network_stats_t;

#endif // NETWORK_STATS_H
