idf_component_register(SRCS "main.c" "button.c"
                    INCLUDE_DIRS "."
                    REQUIRES fatfs sdmmc esp_driver_spi esp_driver_gpio)
