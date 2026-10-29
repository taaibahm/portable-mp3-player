#include <stdio.h>
#include "esp_err.h"
#include "esp_log.h"
#include "button.h"
#include "oled.h"
#include "sd_card.h"

#define TAG "MAIN"

#define PIN_NUM_SD_MOSI 19
#define PIN_NUM_SD_MISO 18
#define PIN_NUM_SD_CLK  21
#define PIN_NUM_SD_CS   23
#define MAX_SD_ENTRIES  64

#define PIN_NUM_OLED_BUTTON 25
#define PIN_NUM_OLED_SDA    33
#define PIN_NUM_OLED_SCL    32

static bool oled_ready = false;

static void show_oled_idle(void) {
    char line[22];
    snprintf(line, sizeof(line), "on GPIO %d", PIN_NUM_OLED_BUTTON);

    oled_clear();
    oled_draw_string(0, 0, "OLED test");
    oled_draw_string(0, 2, "Press the button");
    oled_draw_string(0, 3, line);
    oled_flush();
}

// Runs on the button task for each OLED button event
static void on_oled_button(int gpio, button_event_t event) {
    static uint32_t presses = 0;

    if (!oled_ready) {
        return;
    }

    char line[22];
    switch (event) {
    case BUTTON_EVENT_PRESSED:
        presses++;
        snprintf(line, sizeof(line), "Presses: %lu", presses);
        oled_clear();
        oled_draw_string(0, 0, "Button pressed!");
        oled_draw_string(0, 2, "Hello from ESP32");
        oled_draw_string(0, 4, line);
        oled_draw_string(0, 6, "Hold 3s for long");
        oled_flush();
        break;
    case BUTTON_EVENT_LONG_PRESS:
        oled_clear();
        oled_draw_string(0, 0, "Long press!");
        oled_draw_string(0, 2, "Let go to reset");
        oled_flush();
        break;
    case BUTTON_EVENT_RELEASED:
        show_oled_idle();
        break;
    default:
        break;
    }
}

void app_main(void) {
    oled_ready = oled_init(PIN_NUM_OLED_SDA, PIN_NUM_OLED_SCL) == ESP_OK;
    if (oled_ready) {
        show_oled_idle();
    }

    // Buttons run in their own task, so they keep working after the SD test below finishes
    button_add(PIN_NUM_OLED_BUTTON, on_oled_button);

    if (sd_card_mount(PIN_NUM_SD_MOSI, PIN_NUM_SD_MISO, PIN_NUM_SD_CLK, PIN_NUM_SD_CS) != ESP_OK) {
        return;
    }

    static sd_entry_t entries[MAX_SD_ENTRIES];
    int count = sd_card_list(SD_MOUNT_POINT, entries, MAX_SD_ENTRIES);
    ESP_LOGI(TAG, "%d item(s) on the SD card", count);
}
