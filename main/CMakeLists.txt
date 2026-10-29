idf_component_register(SRCS "main.c" "browser.c" "button.c" "oled.c" "player.c" "sd_card.c"
                    INCLUDE_DIRS "."
                    REQUIRES fatfs sdmmc esp_driver_spi esp_driver_gpio esp_driver_i2c esp_driver_i2s)
