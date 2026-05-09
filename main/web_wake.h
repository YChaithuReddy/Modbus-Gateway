/**
 * web_wake.h - On-demand web server activation for VPN remote access.
 *
 * In operational mode the web server is OFF by default to save heap and
 * shrink attack surface. This module:
 *   1) Listens for ICMP echo requests from VPN peers (10.100.0.0/24) and
 *      starts the web server when one arrives.
 *   2) Tracks HTTP activity via httpd's open_fn callback.
 *   3) Stops the server after WAKE_IDLE_TIMEOUT_SEC seconds of inactivity.
 *
 * Setup mode (CONFIG_STATE_SETUP) bypasses this — server is auto-started
 * unconditionally there. Caller must skip web_wake_init() in setup mode.
 */

#ifndef WEB_WAKE_H
#define WEB_WAKE_H

#include "esp_err.h"
#include "esp_http_server.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WAKE_IDLE_TIMEOUT_SEC   (30 * 60)   // 30 minutes of HTTP+ICMP silence -> stop
#define WAKE_CHECK_PERIOD_SEC   60          // idle watchdog tick
#define WAKE_VPN_SUBNET         "10.100.0.0"
#define WAKE_VPN_PREFIX_LEN     24          // /24 -> match 10.100.0.x
#define WAKE_TASK_STACK         8192
#define WAKE_TASK_PRIORITY      4

/**
 * Start raw ICMP listener task + idle watchdog timer.
 * Idempotent — second call is a no-op.
 * Skip in CONFIG_STATE_SETUP (server is already auto-started there).
 */
esp_err_t web_wake_init(void);

/**
 * httpd open_fn callback. Wire into web_config.c's start_webserver()
 * via `config.open_fn = web_wake_session_open;` before httpd_start().
 * Called by ESP-IDF httpd on every accepted TCP connection — resets the
 * idle timer so the server stays up while the user interacts with it.
 */
esp_err_t web_wake_session_open(httpd_handle_t hd, int sockfd);

#ifdef __cplusplus
}
#endif

#endif // WEB_WAKE_H
