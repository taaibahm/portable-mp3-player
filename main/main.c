#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_err.h"
#include "esp_log.h"
#include "browser.h"
#include "button.h"
#include "oled.h"
#include "player.h"
#include "sd_card.h"

#define TAG "MAIN"

#define PIN_NUM_SD_MOSI 19
#define PIN_NUM_SD_MISO 18
#define PIN_NUM_SD_CLK  21
#define PIN_NUM_SD_CS   23

#define PIN_NUM_OLED_SDA 33
#define PIN_NUM_OLED_SCL 32

#define PIN_NUM_BUTTON 25

#define PIN_NUM_AMP_BCLK 26
#define PIN_NUM_AMP_LRC  27
#define PIN_NUM_AMP_DIN  14

#define MAX_SD_ENTRIES     64
#define CHECK_SCREEN_MS    2000

static sd_entry_t entries[MAX_SD_ENTRIES];

// Set once the browser has drawn its first screen; button events before
// that (e.g. during the startup check) are ignored
static volatile bool browser_active = false;

// Runs on the button task
static void on_button(int gpio, button_event_t event) {
    if (browser_active) {
        browser_handle_button(event);
    }
}

static void draw_check_row(int row, const char *label, const char *status) {
    char line[22];
    snprintf(line, sizeof(line), "%-9s%s", label, status);
    oled_draw_string(0, row, line);
    oled_flush();
}

void app_main(void) {
    // Connection check: bring up each part in turn and show the result on
    // the screen (if it works) and in the serial log
    bool oled_ok = oled_init(PIN_NUM_OLED_SDA, PIN_NUM_OLED_SCL) == ESP_OK;
    oled_clear();
    oled_draw_string(0, 0, "Startup check");
    draw_check_row(2, "OLED", "OK");

    bool button_ok = button_add(PIN_NUM_BUTTON, on_button) == ESP_OK;
    draw_check_row(3, "Button", button_ok ? "OK" : "FAIL");

    // The amp has no way to report back, so this only checks the I2S setup
    bool audio_ok = player_init(PIN_NUM_AMP_BCLK, PIN_NUM_AMP_LRC, PIN_NUM_AMP_DIN, NULL) == ESP_OK;
    draw_check_row(4, "Audio", audio_ok ? "OK" : "FAIL");

    draw_check_row(5, "SD card", "...");
    bool sd_ok = sd_card_mount(PIN_NUM_SD_MOSI, PIN_NUM_SD_MISO, PIN_NUM_SD_CLK, PIN_NUM_SD_CS) == ESP_OK;
    int count = sd_ok ? sd_card_list(SD_MOUNT_POINT, entries, MAX_SD_ENTRIES) : -1;
    sd_ok = sd_ok && count >= 0;
    draw_check_row(5, "SD card", sd_ok ? "OK " : "FAIL");

    ESP_LOGI(TAG, "Startup check: OLED %s, button %s, audio %s, SD card %s",
             oled_ok ? "OK" : "FAIL", button_ok ? "OK" : "FAIL",
             audio_ok ? "OK" : "FAIL", sd_ok ? "OK" : "FAIL");

    if (!sd_ok) {
        oled_draw_string(0, 7, "Check SD, then reset");
        oled_flush();
        return;
    }

    char items[22];
    snprintf(items, sizeof(items), "%d", count);
    draw_check_row(6, "Items", items);
    ESP_LOGI(TAG, "%d item(s) on the SD card", count);

    vTaskDelay(pdMS_TO_TICKS(CHECK_SCREEN_MS));
    browser_init(entries, count);
    browser_active = true;
}
