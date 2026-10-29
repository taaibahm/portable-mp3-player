#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <dirent.h>
#include "sd_card.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "driver/sdspi_host.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"

#define TAG "SD_CARD"

#define SD_SPI_HOST SPI2_HOST

static sdmmc_card_t *card = NULL;

esp_err_t sd_card_mount(int mosi_gpio, int miso_gpio, int sck_gpio, int cs_gpio) {
    ESP_LOGI(TAG, "Waiting for SD card power stabilization...");
    vTaskDelay(pdMS_TO_TICKS(250));

    // Ensure strict pull-ups on data/control lines
    gpio_pullup_en(miso_gpio);
    gpio_pullup_en(mosi_gpio);
    gpio_pullup_en(cs_gpio);

    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024
    };

    ESP_LOGI(TAG, "Initializing SPI peripheral...");
    spi_bus_config_t bus_cfg = {
        .mosi_io_num = mosi_gpio,
        .miso_io_num = miso_gpio,
        .sclk_io_num = sck_gpio,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4000,
    };

    esp_err_t ret = spi_bus_initialize(SD_SPI_HOST, &bus_cfg, SDSPI_DEFAULT_DMA);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize SPI bus: %s", esp_err_to_name(ret));
        return ret;
    }

    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs = cs_gpio;
    slot_config.host_id = SD_SPI_HOST;

    // Cap the SPI clock; step down to 1000 or 400 if you get 0x109 (CRC) errors
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.max_freq_khz = 4000;

    ESP_LOGI(TAG, "Mounting filesystem at %s...", SD_MOUNT_POINT);
    ret = esp_vfs_fat_sdspi_mount(SD_MOUNT_POINT, &host, &slot_config, &mount_config, &card);
    if (ret != ESP_OK || card == NULL) {
        ESP_LOGE(TAG, "Mount failed with error: %s (0x%x)", esp_err_to_name(ret), ret);
        card = NULL;
        spi_bus_free(SD_SPI_HOST);
        return ret != ESP_OK ? ret : ESP_FAIL;
    }

    ESP_LOGI(TAG, "Filesystem mounted successfully!");
    sdmmc_card_print_info(stdout, card);
    return ESP_OK;
}

void sd_card_unmount(void) {
    if (card == NULL) {
        return;
    }
    esp_vfs_fat_sdcard_unmount(SD_MOUNT_POINT, card);
    spi_bus_free(SD_SPI_HOST);
    card = NULL;
    ESP_LOGI(TAG, "Card unmounted.");
}

static bool is_hidden(const char *name) {
    // Dot files, and the "System Volume Information" folder Windows adds
    // (shown as SYSTEM~1 because long file names are disabled)
    return name[0] == '.' || strcmp(name, "SYSTEM~1") == 0;
}

static int compare_entries(const void *a, const void *b) {
    const sd_entry_t *ea = a;
    const sd_entry_t *eb = b;
    if (ea->is_dir != eb->is_dir) {
        return ea->is_dir ? -1 : 1;
    }
    return strcasecmp(ea->name, eb->name);
}

int sd_card_list(const char *path, sd_entry_t *entries, int max_entries) {
    DIR *dir = opendir(path);
    if (dir == NULL) {
        ESP_LOGE(TAG, "Failed to open %s", path);
        return -1;
    }

    int count = 0;
    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL && count < max_entries) {
        if (is_hidden(ent->d_name)) {
            continue;
        }

        sd_entry_t *e = &entries[count];
        strlcpy(e->name, ent->d_name, sizeof(e->name));
        e->is_dir = ent->d_type == DT_DIR;
        e->size = 0;

        if (!e->is_dir) {
            char full_path[300];
            struct stat st;
            snprintf(full_path, sizeof(full_path), "%s/%s", path, ent->d_name);
            if (stat(full_path, &st) == 0) {
                e->size = st.st_size;
            }
        }

        ESP_LOGI(TAG, " Found %s: %s", e->is_dir ? "folder" : "file", e->name);
        count++;
    }
    closedir(dir);

    qsort(entries, count, sizeof(entries[0]), compare_entries);
    return count;
}
