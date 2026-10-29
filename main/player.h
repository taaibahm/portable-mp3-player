#pragma once

#include <stdint.h>
#include "esp_err.h"

typedef struct {
    uint32_t sample_rate;
    uint16_t channels;
    uint32_t duration_ms;
} player_info_t;

// Called from the player task when a file finishes playing on its own
// (not after player_stop()). Keep it short; don't call player_* from it.
typedef void (*player_done_cb_t)(void);

// Sets up I2S output to the amplifier. on_done may be NULL.
esp_err_t player_init(int bclk_gpio, int ws_gpio, int dout_gpio, player_done_cb_t on_done);

// Opens a 16-bit PCM WAV (mono or stereo, 8-48 kHz) and starts playing it in
// the background, stopping anything already playing. info may be NULL.
esp_err_t player_play(const char *path, player_info_t *info);

// Stops playback and waits until the player task has finished
void player_stop(void);

uint32_t player_elapsed_ms(void);
