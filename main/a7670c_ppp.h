#ifndef A7670C_PPP_H
#define A7670C_PPP_H

#include "esp_err.h"
#include "esp_netif.h"
#include "esp_event.h"

// PPP events
ESP_EVENT_DECLARE_BASE(PPP_EVENT);

typedef enum {
    PPP_EVENT_START,
    PPP_EVENT_CONNECTED,
    PPP_EVENT_DISCONNECTED,
    PPP_EVENT_ERROR
} ppp_event_t;

// PPP configuration
typedef struct {
    const char* apn;
    const char* user;
    const char* pass;
    int uart_num;
    int tx_pin;
    int rx_pin;
    int pwr_pin;
    int reset_pin;  // Hardware reset pin
    int baud_rate;
} ppp_config_t;

// Signal strength structure
typedef struct {
    int rssi;        // Received Signal Strength Indicator (0-31, 99=unknown)
    int ber;         // Bit Error Rate (0-7, 99=unknown)
    int rssi_dbm;    // Signal strength in dBm
    const char* quality;  // Signal quality description
    char operator_name[64];  // Network operator name
} signal_strength_t;

// Function prototypes
esp_err_t a7670c_ppp_init(const ppp_config_t* config);
esp_err_t a7670c_ppp_deinit(void);
esp_err_t a7670c_ppp_connect(void);
esp_err_t a7670c_ppp_disconnect(void);
bool a7670c_ppp_is_connected(void);
bool a7670c_is_connected(void);  // Alias for compatibility
esp_netif_t* a7670c_ppp_get_netif(void);
esp_err_t a7670c_ppp_get_ip_info(char* ip_str, size_t ip_str_size);

// Signal strength and modem restart functions
esp_err_t a7670c_get_signal_strength(signal_strength_t* signal);
esp_err_t a7670c_get_stored_signal_strength(signal_strength_t* signal);
esp_err_t a7670c_restart_modem(void);

// Get recommended retry delay based on connection failure history (in milliseconds)
uint32_t a7670c_get_retry_delay_ms(void);

#endif // A7670C_PPP_H