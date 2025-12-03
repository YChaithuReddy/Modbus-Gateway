idf_component_register(SRCS "telegram_bot.c" "ds3231_rtc.c" "sd_card_logger.c" "a7670c_ppp.c" "main.c" "modbus.c" "web_config.c" "sensor_manager.c" "json_templates.c" "ota_update.c"
                    INCLUDE_DIRS "."
                    REQUIRES esp_https_ota app_update esp_http_client nvs_flash
                    EMBED_FILES "azure_ca_cert.pem")
