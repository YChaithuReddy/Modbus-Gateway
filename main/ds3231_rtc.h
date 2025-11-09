#ifndef DS3231_RTC_H
#define DS3231_RTC_H

#include <time.h>
#include "esp_err.h"

// DS3231 I2C address
#define DS3231_I2C_ADDR 0x68

// DS3231 register addresses
#define DS3231_REG_SECONDS    0x00
#define DS3231_REG_MINUTES    0x01
#define DS3231_REG_HOURS      0x02
#define DS3231_REG_DAY        0x03
#define DS3231_REG_DATE       0x04
#define DS3231_REG_MONTH      0x05
#define DS3231_REG_YEAR       0x06
#define DS3231_REG_CONTROL    0x0E
#define DS3231_REG_STATUS     0x0F
#define DS3231_REG_TEMP_MSB   0x11
#define DS3231_REG_TEMP_LSB   0x12

/**
 * @brief Initialize DS3231 RTC module
 *
 * @return esp_err_t ESP_OK on success
 */
esp_err_t ds3231_init(void);

/**
 * @brief Deinitialize DS3231 RTC module
 *
 * @return esp_err_t ESP_OK on success
 */
esp_err_t ds3231_deinit(void);

/**
 * @brief Read time from DS3231 RTC
 *
 * @param time Pointer to time_t to store the read time
 * @return esp_err_t ESP_OK on success
 */
esp_err_t ds3231_get_time(time_t* time);

/**
 * @brief Write time to DS3231 RTC
 *
 * @param time Time to write to RTC
 * @return esp_err_t ESP_OK on success
 */
esp_err_t ds3231_set_time(time_t time);

/**
 * @brief Get time as struct tm
 *
 * @param timeinfo Pointer to struct tm to fill
 * @return esp_err_t ESP_OK on success
 */
esp_err_t ds3231_get_time_tm(struct tm* timeinfo);

/**
 * @brief Set time from struct tm
 *
 * @param timeinfo Pointer to struct tm with time to set
 * @return esp_err_t ESP_OK on success
 */
esp_err_t ds3231_set_time_tm(const struct tm* timeinfo);

/**
 * @brief Read temperature from DS3231
 *
 * @param temp Pointer to float to store temperature in Celsius
 * @return esp_err_t ESP_OK on success
 */
esp_err_t ds3231_get_temperature(float* temp);

/**
 * @brief Sync system time from RTC
 *
 * Reads time from DS3231 and sets the ESP32 system time
 *
 * @return esp_err_t ESP_OK on success
 */
esp_err_t ds3231_sync_system_time(void);

/**
 * @brief Update RTC from system time
 *
 * Writes current ESP32 system time to DS3231 RTC
 * Useful after NTP sync to keep RTC accurate
 *
 * @return esp_err_t ESP_OK on success
 */
esp_err_t ds3231_update_from_system_time(void);

#endif // DS3231_RTC_H
