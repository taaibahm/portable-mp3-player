#include <stdio.h>
#include <string.h>
#include <sys/unistd.h>
#include <sys/stat.h>
#include <dirent.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "driver/sdspi_host.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "button.h"
#include "oled.h"

#define TAG "SD_TEST"

#define PIN_NUM_MISO 19
#define PIN_NUM_MOSI 23
#define PIN_NUM_CLK  18
#define PIN_NUM_CS   5

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

    ESP_LOGI(TAG, "Waiting for SD card power stabilization...");
    vTaskDelay(pdMS_TO_TICKS(250));

    ESP_LOGI(TAG, "Initializing SD card test...");

    // Ensure strict pull-ups on data/control lines
    gpio_pullup_en(PIN_NUM_MISO);
    gpio_pullup_en(PIN_NUM_MOSI);
    gpio_pullup_en(PIN_NUM_CS);

    esp_err_t ret;

    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024
    };

    sdmmc_card_t *card = NULL;
    const char mount_point[] = "/sdcard";
    
    ESP_LOGI(TAG, "Initializing SPI peripheral...");
    spi_bus_config_t bus_cfg = {
        .mosi_io_num = PIN_NUM_MOSI,
        .miso_io_num = PIN_NUM_MISO,
        .sclk_io_num = PIN_NUM_CLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4000,
    };

    ret = spi_bus_initialize(SPI2_HOST, &bus_cfg, SDSPI_DEFAULT_DMA);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize SPI bus: %s", esp_err_to_name(ret));
        return;
    }

    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs = PIN_NUM_CS;
    slot_config.host_id = SPI2_HOST;

    // Cap the SPI clock; step down to 1000 or 400 if you get 0x109 (CRC) errors
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.max_freq_khz = 4000; 

    ESP_LOGI(TAG, "Mounting filesystem at %s...", mount_point);
    ret = esp_vfs_fat_sdspi_mount(mount_point, &host, &slot_config, &mount_config, &card);

    if (ret != ESP_OK || card == NULL) {
        ESP_LOGE(TAG, "Mount failed with error: %s (0x%x)", esp_err_to_name(ret), ret);
        spi_bus_free(SPI2_HOST);
        return;
    }

    ESP_LOGI(TAG, "Filesystem mounted successfully!");
    sdmmc_card_print_info(stdout, card);

    DIR *dir = opendir("/sdcard");
    if (dir != NULL) {
        struct dirent *entry;
        while ((entry = readdir(dir)) != NULL) {
            ESP_LOGI(TAG, " Found file: %s", entry->d_name);
        }
        closedir(dir);
    }

    esp_vfs_fat_sdcard_unmount(mount_point, card);
    spi_bus_free(SPI2_HOST);
    ESP_LOGI(TAG, "Card unmounted.");
}