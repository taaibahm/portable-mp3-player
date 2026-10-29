#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include "player.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "driver/i2s_std.h"

#define TAG "PLAYER"

// Software volume, so the speaker isn't at full blast through the amp's fixed 9 dB gain
#define PLAYER_VOLUME_PERCENT 50

#define READ_FRAMES 512

static i2s_chan_handle_t tx_chan;
static player_done_cb_t done_cb;
static SemaphoreHandle_t stopped_sem; // Given by the player task when it exits

static FILE *file;
static uint32_t data_left; // Bytes of sample data still to read
static player_info_t current;
static volatile uint32_t frames_played;
static volatile bool stop_requested;
static volatile bool playing;

static uint16_t read_le16(const uint8_t *p) {
    return p[0] | (p[1] << 8);
}

static uint32_t read_le32(const uint8_t *p) {
    return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24);
}

// Reads the WAV header and leaves f positioned at the start of the sample data
static esp_err_t parse_wav(FILE *f, player_info_t *info, uint32_t *data_size) {
    uint8_t riff[12];
    if (fread(riff, 1, sizeof(riff), f) != sizeof(riff) ||
        memcmp(riff, "RIFF", 4) != 0 || memcmp(riff + 8, "WAVE", 4) != 0) {
        ESP_LOGE(TAG, "Not a WAV file");
        return ESP_ERR_NOT_SUPPORTED;
    }

    bool have_fmt = false;
    while (1) {
        uint8_t chunk[8];
        if (fread(chunk, 1, sizeof(chunk), f) != sizeof(chunk)) {
            ESP_LOGE(TAG, "WAV file has no data chunk");
            return ESP_ERR_INVALID_SIZE;
        }
        uint32_t size = read_le32(chunk + 4);
        uint32_t padded_size = size + (size & 1); // Chunks are padded to an even size

        if (memcmp(chunk, "fmt ", 4) == 0) {
            uint8_t fmt[16];
            if (size < sizeof(fmt) || fread(fmt, 1, sizeof(fmt), f) != sizeof(fmt)) {
                return ESP_ERR_INVALID_SIZE;
            }
            uint16_t format = read_le16(fmt);
            uint16_t channels = read_le16(fmt + 2);
            uint32_t sample_rate = read_le32(fmt + 4);
            uint16_t bits = read_le16(fmt + 14);

            if (format != 1 || bits != 16 || channels < 1 || channels > 2 ||
                sample_rate < 8000 || sample_rate > 48000) {
                ESP_LOGE(TAG, "Unsupported WAV (format %u, %u-bit, %u ch, %lu Hz); "
                         "need 16-bit PCM, mono or stereo, 8-48 kHz",
                         format, bits, channels, sample_rate);
                return ESP_ERR_NOT_SUPPORTED;
            }
            info->sample_rate = sample_rate;
            info->channels = channels;
            have_fmt = true;
            padded_size -= sizeof(fmt);
        } else if (memcmp(chunk, "data", 4) == 0) {
            if (!have_fmt) {
                return ESP_ERR_INVALID_STATE;
            }
            *data_size = size;
            info->duration_ms = (uint64_t)size * 1000 / (info->sample_rate * info->channels * 2);
            return ESP_OK;
        }

        if (fseek(f, padded_size, SEEK_CUR) != 0) {
            return ESP_ERR_INVALID_SIZE;
        }
    }
}

static void play_task(void *arg) {
    static int16_t in[READ_FRAMES * 2];
    static int16_t out[READ_FRAMES * 2];
    size_t frame_bytes = current.channels * sizeof(int16_t);
    bool finished = false;

    while (!stop_requested) {
        size_t want = READ_FRAMES * frame_bytes;
        if (want > data_left) {
            want = data_left;
        }
        size_t frames = fread(in, 1, want, file) / frame_bytes;
        if (frames == 0) {
            finished = true;
            break;
        }
        data_left -= frames * frame_bytes;

        // The amp mixes left and right, so always send stereo; mono files go to both sides
        for (size_t i = 0; i < frames; i++) {
            int32_t left = in[i * current.channels];
            int32_t right = current.channels == 2 ? in[i * 2 + 1] : left;
            out[i * 2] = left * PLAYER_VOLUME_PERCENT / 100;
            out[i * 2 + 1] = right * PLAYER_VOLUME_PERCENT / 100;
        }

        size_t written;
        i2s_channel_write(tx_chan, out, frames * 2 * sizeof(int16_t), &written, portMAX_DELAY);
        frames_played += frames;
    }

    i2s_channel_disable(tx_chan);
    fclose(file);
    file = NULL;

    bool notify = finished && !stop_requested;
    ESP_LOGI(TAG, "Playback %s", notify ? "finished" : "stopped");
    playing = false;
    xSemaphoreGive(stopped_sem);

    if (notify && done_cb) {
        done_cb();
    }
    vTaskDelete(NULL);
}

esp_err_t player_init(int bclk_gpio, int ws_gpio, int dout_gpio, player_done_cb_t on_done) {
    done_cb = on_done;
    stopped_sem = xSemaphoreCreateBinary();
    if (stopped_sem == NULL) {
        return ESP_ERR_NO_MEM;
    }

    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan_cfg.dma_desc_num = 8;
    chan_cfg.dma_frame_num = 511; // ~90 ms of buffering at 44.1 kHz to ride out slow SD reads
    chan_cfg.auto_clear = true;   // Send silence instead of repeating old data on underrun
    esp_err_t ret = i2s_new_channel(&chan_cfg, &tx_chan, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create I2S channel: %s", esp_err_to_name(ret));
        return ret;
    }

    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(44100),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = bclk_gpio,
            .ws = ws_gpio,
            .dout = dout_gpio,
            .din = I2S_GPIO_UNUSED,
        },
    };
    ret = i2s_channel_init_std_mode(tx_chan, &std_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init I2S: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "I2S ready (BCLK GPIO %d, LRC GPIO %d, DIN GPIO %d)", bclk_gpio, ws_gpio, dout_gpio);
    return ESP_OK;
}

esp_err_t player_play(const char *path, player_info_t *info) {
    player_stop();

    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        ESP_LOGE(TAG, "Failed to open %s", path);
        return ESP_ERR_NOT_FOUND;
    }

    uint32_t data_size;
    esp_err_t ret = parse_wav(f, &current, &data_size);
    if (ret != ESP_OK) {
        fclose(f);
        return ret;
    }

    // The channel is disabled between files, so the clock can be changed per file
    i2s_std_clk_config_t clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(current.sample_rate);
    ret = i2s_channel_reconfig_std_clock(tx_chan, &clk_cfg);
    if (ret == ESP_OK) {
        ret = i2s_channel_enable(tx_chan);
    }
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start I2S: %s", esp_err_to_name(ret));
        fclose(f);
        return ret;
    }

    file = f;
    data_left = data_size;
    frames_played = 0;
    stop_requested = false;
    playing = true;
    xSemaphoreTake(stopped_sem, 0); // Clear a give left over from the last file

    if (xTaskCreate(play_task, "player", 4096, NULL, 7, NULL) != pdPASS) {
        i2s_channel_disable(tx_chan);
        fclose(f);
        file = NULL;
        playing = false;
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "Playing %s (%lu Hz, %s, %lu s)", path, current.sample_rate,
             current.channels == 2 ? "stereo" : "mono", current.duration_ms / 1000);
    if (info) {
        *info = current;
    }
    return ESP_OK;
}

void player_stop(void) {
    if (!playing) {
        return;
    }
    stop_requested = true;
    xSemaphoreTake(stopped_sem, portMAX_DELAY);
}

uint32_t player_elapsed_ms(void) {
    if (current.sample_rate == 0) {
        return 0;
    }
    return (uint64_t)frames_played * 1000 / current.sample_rate;
}
