idf_component_register(SRCS "main.c" "button.c" "oled.c"
                    INCLUDE_DIRS "."
                    REQUIRES fatfs sdmmc esp_driver_spi esp_driver_gpio esp_driver_i2c)
