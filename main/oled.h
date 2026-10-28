#pragma once

#include "esp_err.h"

// 128x64 I2C OLED (SSD1306, or SH1106 with OLED_COL_OFFSET set to 2 in oled.c).
// Text is 6x8 pixels per character: 21 characters per row, rows 0-7.

esp_err_t oled_init(int sda_gpio, int scl_gpio);
void oled_clear(void);
void oled_draw_string(int x, int row, const char *text);
// Drawing only changes the framebuffer; call this to send it to the display
esp_err_t oled_flush(void);
