idf_component_register(SRCS "telegram_bot.c" "ds3231_rtc.c" "sd_card_logger.c" "a7670c_ppp.c" "main.c" "modbus.c" "web_config.c" "sensor_manager.c" "json_templates.c"
                    INCLUDE_DIRS "."
                    EMBED_FILES "azure_ca_cert.pem")