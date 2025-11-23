// telegram_bot.h - Telegram Bot Integration for ESP32 Gateway
// Provides remote monitoring and control via Telegram

#ifndef TELEGRAM_BOT_H
#define TELEGRAM_BOT_H

#include <stdbool.h>
#include "esp_err.h"

// Initialize Telegram bot
esp_err_t telegram_bot_init(void);

// Start Telegram bot task
esp_err_t telegram_bot_start(void);

// Stop Telegram bot task
esp_err_t telegram_bot_stop(void);

// Send message to configured chat
esp_err_t telegram_send_message(const char *message);

// Send alert notification
esp_err_t telegram_send_alert(const char *title, const char *message);

// Send formatted status message
esp_err_t telegram_send_status(void);

// Send sensor readings
esp_err_t telegram_send_sensor_readings(void);

// Check if bot is enabled and configured
bool telegram_is_enabled(void);

#endif // TELEGRAM_BOT_H
