/**
 * web_wake.c - Wake-on-ICMP + idle-shutdown for the operational-mode web server.
 *
 * See web_wake.h for the rationale. Implementation notes:
 *
 * - Raw ICMP socket: SOCK_RAW + IPPROTO_ICMP gets a copy of every inbound
 *   ICMP packet. lwIP's normal ICMP stack still replies to echo requests,
 *   so `ping` keeps working unchanged.
 * - Source-IP filter: we only wake on echo requests from the VPN subnet
 *   (default 10.100.0.0/24). Pings from local LAN are ignored.
 * - Activity is anything: an inbound VPN ping, or any TCP session opened
 *   on the httpd. Both reset last_activity_us; the idle watchdog stops the
 *   server only after WAKE_IDLE_TIMEOUT_SEC of total silence.
 * - A mutex serializes start/stop so we don't fight the GPIO34 button or
 *   Device Twin web_server_enabled toggles (CLAUDE.md known race).
 */

#include "web_wake.h"
#include "web_config.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "freertos/semphr.h"

#include "lwip/sockets.h"
#include "lwip/inet.h"

#include <string.h>

static const char *TAG = "WEB_WAKE";

// Helpers from main.c — they handle SIM-mode WiFi init, the
// web_server_running flag, and the LED. Reused so wake-trigger and
// idle-shutdown go through the exact same path as the GPIO34 button.
extern void start_web_server(void);
extern void stop_web_server(void);

static volatile int64_t s_last_activity_us = 0;
static SemaphoreHandle_t s_lock = NULL;
static TimerHandle_t s_idle_timer = NULL;
static TaskHandle_t s_listener_task = NULL;
static bool s_initialized = false;

// VPN subnet match: incoming src AND mask == network
static uint32_t s_vpn_network_be = 0;   // network-byte-order
static uint32_t s_vpn_mask_be = 0;

static inline void mark_activity(void)
{
    s_last_activity_us = esp_timer_get_time();
}

static bool server_is_up(void)
{
    return web_config_get_server() != NULL;
}

static void wake_server_if_down(const char *reason)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (!server_is_up()) {
        ESP_LOGI(TAG, "[WAKE] %s — starting web server", reason);
        start_web_server();
    }
    xSemaphoreGive(s_lock);
    mark_activity();
}

static void idle_watchdog_cb(TimerHandle_t xTimer)
{
    (void)xTimer;
    if (!server_is_up()) return;

    int64_t idle_us = esp_timer_get_time() - s_last_activity_us;
    int64_t timeout_us = (int64_t)WAKE_IDLE_TIMEOUT_SEC * 1000000LL;
    if (idle_us < timeout_us) return;

    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (server_is_up()) {
        ESP_LOGI(TAG, "[IDLE] no activity for %lld s — stopping web server",
                 (long long)(idle_us / 1000000LL));
        stop_web_server();
    }
    xSemaphoreGive(s_lock);
}

esp_err_t web_wake_session_open(httpd_handle_t hd, int sockfd)
{
    (void)hd;
    (void)sockfd;
    mark_activity();
    return ESP_OK;
}

static void icmp_listener_task(void *pv)
{
    (void)pv;

    int sock = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
    if (sock < 0) {
        ESP_LOGE(TAG, "raw ICMP socket failed: errno=%d (LWIP_RAW must be enabled)", errno);
        s_listener_task = NULL;
        vTaskDelete(NULL);
        return;
    }
    ESP_LOGI(TAG, "ICMP listener up — waking on echo from %s/%d",
             WAKE_VPN_SUBNET, WAKE_VPN_PREFIX_LEN);

    uint8_t buf[128];
    while (true) {
        struct sockaddr_in src;
        socklen_t slen = sizeof(src);
        int n = recvfrom(sock, buf, sizeof(buf), 0, (struct sockaddr *)&src, &slen);
        if (n < 1) {
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }

        // First byte of the ICMP payload (lwIP raw ICMP socket strips IP header).
        // Some lwIP builds deliver the IP header too — accept either layout.
        uint8_t icmp_type;
        if (n >= 20 && (buf[0] >> 4) == 4) {
            // IPv4 header included: ICMP type at offset = ip_ihl * 4
            int ihl = (buf[0] & 0x0F) * 4;
            if (n < ihl + 1) continue;
            icmp_type = buf[ihl];
        } else {
            icmp_type = buf[0];
        }
        if (icmp_type != 8) continue;   // 8 = echo request

        // Source-IP filter (network-byte-order compare)
        if ((src.sin_addr.s_addr & s_vpn_mask_be) != s_vpn_network_be) {
            continue;
        }

        char ipstr[INET_ADDRSTRLEN];
        inet_ntoa_r(src.sin_addr, ipstr, sizeof(ipstr));

        if (!server_is_up()) {
            char reason[48];
            snprintf(reason, sizeof(reason), "ICMP from %s", ipstr);
            wake_server_if_down(reason);
        } else {
            // Server already up — count the ping as activity so the user
            // can keep it alive with periodic pings if the UI is idle.
            mark_activity();
        }
    }
}

esp_err_t web_wake_init(void)
{
    if (s_initialized) {
        ESP_LOGW(TAG, "already initialized");
        return ESP_OK;
    }

    s_lock = xSemaphoreCreateMutex();
    if (!s_lock) return ESP_ERR_NO_MEM;

    // Pre-compute VPN subnet match in network byte order
    struct in_addr net;
    if (inet_aton(WAKE_VPN_SUBNET, &net) == 0) {
        ESP_LOGE(TAG, "bad WAKE_VPN_SUBNET: %s", WAKE_VPN_SUBNET);
        return ESP_ERR_INVALID_ARG;
    }
    uint32_t mask_host = WAKE_VPN_PREFIX_LEN == 0
        ? 0
        : (uint32_t)(0xFFFFFFFFu << (32 - WAKE_VPN_PREFIX_LEN));
    s_vpn_mask_be = htonl(mask_host);
    s_vpn_network_be = net.s_addr & s_vpn_mask_be;

    mark_activity();   // grace period: don't auto-stop in the first 30 min after boot

    s_idle_timer = xTimerCreate(
        "web_wake_idle",
        pdMS_TO_TICKS(WAKE_CHECK_PERIOD_SEC * 1000),
        pdTRUE,    // auto-reload
        NULL,
        idle_watchdog_cb);
    if (!s_idle_timer || xTimerStart(s_idle_timer, 0) != pdPASS) {
        ESP_LOGE(TAG, "idle timer create/start failed");
        return ESP_FAIL;
    }

    BaseType_t ok = xTaskCreate(
        icmp_listener_task, "web_wake_icmp",
        WAKE_TASK_STACK, NULL, WAKE_TASK_PRIORITY, &s_listener_task);
    if (ok != pdPASS) {
        ESP_LOGE(TAG, "ICMP listener task create failed");
        return ESP_ERR_NO_MEM;
    }

    s_initialized = true;
    ESP_LOGI(TAG, "wake-on-ICMP + %d-min idle shutdown active",
             WAKE_IDLE_TIMEOUT_SEC / 60);
    return ESP_OK;
}
