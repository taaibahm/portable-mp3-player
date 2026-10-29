#include <stdio.h>
#include <string.h>
#include <strings.h>
#include "browser.h"
#include "oled.h"
#include "esp_log.h"

#define TAG "BROWSER"

#define ROW_TITLE     0
#define ROW_FIRST     1 // First row used for list entries
#define VISIBLE_ROWS  6 // Rows 1-6 show entries
#define ROW_HINT      7
#define LINE_LEN      22 // 21 characters per row plus the terminator

typedef enum {
    VIEW_LIST,
    VIEW_DETAILS,
} view_t;

static const sd_entry_t *entries;
static int entry_count;
static int selected;
static int scroll_top; // Index of the entry shown on the first list row
static view_t view = VIEW_LIST;

static void draw_list(const char *hint) {
    char line[LINE_LEN];

    oled_clear();
    snprintf(line, sizeof(line), "SD card: %d item%s", entry_count, entry_count == 1 ? "" : "s");
    oled_draw_string(0, ROW_TITLE, line);

    if (entry_count == 0) {
        oled_draw_string(0, 2, "No files found");
        oled_draw_string(0, 4, "Add files to the SD");
        oled_draw_string(0, 5, "card and reset");
        oled_flush();
        return;
    }

    for (int row = 0; row < VISIBLE_ROWS; row++) {
        int i = scroll_top + row;
        if (i >= entry_count) {
            break;
        }
        // Marker + up to 19 name characters + folder slash fills one 21-char row
        snprintf(line, sizeof(line), "%c%.19s%s", i == selected ? '>' : ' ',
                 entries[i].name, entries[i].is_dir ? "/" : "");
        oled_draw_string(0, ROW_FIRST + row, line);
    }

    oled_draw_string(0, ROW_HINT, hint);
    oled_flush();
}

static void format_size(uint32_t bytes, char *out, size_t out_len) {
    if (bytes < 1024) {
        snprintf(out, out_len, "%lu B", bytes);
    } else if (bytes < 1024 * 1024) {
        snprintf(out, out_len, "%lu KB", bytes / 1024);
    } else {
        // One decimal place without pulling in float formatting
        uint32_t tenths = bytes / (1024 * 1024 / 10);
        snprintf(out, out_len, "%lu.%lu MB", tenths / 10, tenths % 10);
    }
}

static bool is_mp3(const char *name) {
    size_t len = strlen(name);
    return len > 4 && strcasecmp(name + len - 4, ".mp3") == 0;
}

static void draw_details(void) {
    const sd_entry_t *e = &entries[selected];
    char line[LINE_LEN];

    oled_clear();
    oled_draw_string(0, ROW_TITLE, "Selected:");
    oled_draw_string(0, 2, e->name);

    if (e->is_dir) {
        oled_draw_string(0, 3, "Folder");
    } else {
        format_size(e->size, line, sizeof(line));
        oled_draw_string(0, 3, line);
        oled_draw_string(0, 4, is_mp3(e->name) ? "MP3 file" : "File");
    }

    oled_draw_string(0, ROW_HINT, "Tap to go back");
    oled_flush();
}

static void select_next(void) {
    selected = (selected + 1) % entry_count;

    // Keep the selection inside the visible window
    if (selected < scroll_top) {
        scroll_top = selected; // Wrapped back to the top
    } else if (selected >= scroll_top + VISIBLE_ROWS) {
        scroll_top = selected - VISIBLE_ROWS + 1;
    }
}

void browser_init(const sd_entry_t *new_entries, int count) {
    entries = new_entries;
    entry_count = count;
    selected = 0;
    scroll_top = 0;
    view = VIEW_LIST;
    draw_list("Tap:next Hold:open");
}

void browser_handle_button(button_event_t event) {
    if (entry_count == 0) {
        return;
    }

    switch (event) {
    case BUTTON_EVENT_PRESSED:
        // Let the user know a hold will open the item
        if (view == VIEW_LIST) {
            draw_list("Hold 3s to open...");
        }
        break;

    case BUTTON_EVENT_SHORT_PRESS:
        if (view == VIEW_LIST) {
            select_next();
            ESP_LOGI(TAG, "Highlighted %s", entries[selected].name);
        } else {
            view = VIEW_LIST;
        }
        draw_list("Tap:next Hold:open");
        break;

    case BUTTON_EVENT_LONG_PRESS:
        if (view == VIEW_LIST) {
            view = VIEW_DETAILS;
            ESP_LOGI(TAG, "Selected %s", entries[selected].name);
            draw_details();
        }
        break;

    default:
        break;
    }
}
