#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#define SD_MOUNT_POINT "/sdcard"
#define SD_NAME_MAX    32

typedef struct {
    char name[SD_NAME_MAX];
    bool is_dir;
    uint32_t size; // Bytes; 0 for folders
} sd_entry_t;

// Starts the SPI bus and mounts the card's FAT filesystem at SD_MOUNT_POINT.
// The card stays mounted until sd_card_unmount().
esp_err_t sd_card_mount(int mosi_gpio, int miso_gpio, int sck_gpio, int cs_gpio);
void sd_card_unmount(void);

// Fills entries with the contents of path (folders first, then files, each
// sorted by name), skipping hidden and system entries. Returns the number of
// entries found (at most max_entries), or -1 if the folder can't be opened.
int sd_card_list(const char *path, sd_entry_t *entries, int max_entries);
