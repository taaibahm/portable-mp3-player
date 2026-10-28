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

#define TAG "SD_TEST"

#define PIN_NUM_MISO 19
#define PIN_NUM_MOSI 23
#define PIN_NUM_CLK  18
#define PIN_NUM_CS   5

#define PIN_NUM_BUTTON 4

void app_main(void) {
    // Buttons run in their own task, so they keep working after the SD test below finishes
    button_add(PIN_NUM_BUTTON, NULL);

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