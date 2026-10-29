#include <stdio.h>
#include <string.h>
#include <strings.h>
#include "browser.h"
#include "oled.h"
#include "player.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"

#define TAG "BROWSER"

#define ROW_TITLE     0
#define ROW_FIRST     1 // First row used for list entries
#define VISIBLE_ROWS  6 // Rows 1-6 show entries
#define ROW_HINT      7
#define LINE_LEN      22 // 21 characters per row plus the terminator

#define LIST_HINT "Tap:next Hold:open"

typedef enum {
    VIEW_LIST,
    VIEW_DETAILS,
    VIEW_PLAYING,
} view_t;

typedef enum {
    MSG_BUTTON,
    MSG_PLAYBACK_DONE,
} msg_type_t;

typedef struct {
    msg_type_t type;
    button_event_t button;
} msg_t;

// Only the browser task touches the state below and draws on the screen
static QueueHandle_t msg_queue;
static const sd_entry_t *entries;
static int entry_count;
static int selected;
static int scroll_top; // Index of the entry shown on the first list row
static view_t view = VIEW_LIST;
static player_info_t playing_info;

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

static bool has_extension(const char *name, const char *ext) {
    size_t len = strlen(name);
    size_t ext_len = strlen(ext);
    return len > ext_len && strcasecmp(name + len - ext_len, ext) == 0;
}

// message is shown on row 5, e.g. why a file can't be played; may be NULL
static void draw_details(const char *message) {
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
        if (has_extension(e->name, ".mp3")) {
            oled_draw_string(0, 4, "MP3 file");
            if (message == NULL) {
                message = "MP3 not supported yet";
            }
        } else if (has_extension(e->name, ".wav")) {
            oled_draw_string(0, 4, "WAV file");
        } else {
            oled_draw_string(0, 4, "File");
        }
    }

    if (message) {
        oled_draw_string(0, 5, message);
    }
    oled_draw_string(0, ROW_HINT, "Tap to go back");
    oled_flush();
}

static void format_time(uint32_t ms, char *out, size_t out_len) {
    uint32_t s = ms / 1000;
    snprintf(out, out_len, "%lu:%02lu", s / 60, s % 60);
}

static void draw_playing(void) {
    char line[LINE_LEN];
    char elapsed[8];
    char total[8];

    oled_clear();
    oled_draw_string(0, ROW_TITLE, "Now playing:");
    oled_draw_string(0, 2, entries[selected].name);

    snprintf(line, sizeof(line), "%lu Hz %s", playing_info.sample_rate,
             playing_info.channels == 2 ? "stereo" : "mono");
    oled_draw_string(0, 3, line);

    format_time(player_elapsed_ms(), elapsed, sizeof(elapsed));
    format_time(playing_info.duration_ms, total, sizeof(total));
    snprintf(line, sizeof(line), "%s / %s", elapsed, total);
    oled_draw_string(0, 5, line);

    oled_draw_string(0, ROW_HINT, "Tap to stop");
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

static void open_selected(void) {
    const sd_entry_t *e = &entries[selected];
    ESP_LOGI(TAG, "Selected %s", e->name);

    if (e->is_dir || !has_extension(e->name, ".wav")) {
        view = VIEW_DETAILS;
        draw_details(NULL);
        return;
    }

    char path[64];
    snprintf(path, sizeof(path), "%s/%s", SD_MOUNT_POINT, e->name);
    if (player_play(path, &playing_info) != ESP_OK) {
        view = VIEW_DETAILS;
        draw_details("Can't play this WAV");
        return;
    }
    view = VIEW_PLAYING;
    draw_playing();
}

static void handle_button(button_event_t event) {
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
            if (view == VIEW_PLAYING) {
                player_stop();
            }
            view = VIEW_LIST;
        }
        draw_list(LIST_HINT);
        break;

    case BUTTON_EVENT_LONG_PRESS:
        if (view == VIEW_LIST) {
            open_selected();
        }
        break;

    default:
        break;
    }
}

static void browser_task(void *arg) {
    msg_t msg;

    while (1) {
        // While playing, wake once a second to update the elapsed time
        TickType_t wait = view == VIEW_PLAYING ? pdMS_TO_TICKS(1000) : portMAX_DELAY;
        if (!xQueueReceive(msg_queue, &msg, wait)) {
            draw_playing();
            continue;
        }

        if (msg.type == MSG_BUTTON) {
            handle_button(msg.button);
        } else if (msg.type == MSG_PLAYBACK_DONE && view == VIEW_PLAYING) {
            // Ignored if the user already stopped playback and left this screen
            view = VIEW_LIST;
            draw_list(LIST_HINT);
        }
    }
}

void browser_init(const sd_entry_t *new_entries, int count) {
    entries = new_entries;
    entry_count = count;
    selected = 0;
    scroll_top = 0;
    view = VIEW_LIST;
    draw_list(LIST_HINT);

    msg_queue = xQueueCreate(8, sizeof(msg_t));
    // player_stop() can block briefly, so this runs apart from the button task
    xTaskCreate(browser_task, "browser", 4096, NULL, 5, NULL);
}

void browser_handle_button(button_event_t event) {
    msg_t msg = {.type = MSG_BUTTON, .button = event};
    if (msg_queue) {
        xQueueSend(msg_queue, &msg, 0);
    }
}

void browser_on_playback_done(void) {
    msg_t msg = {.type = MSG_PLAYBACK_DONE};
    if (msg_queue) {
        xQueueSend(msg_queue, &msg, 0);
    }
}
