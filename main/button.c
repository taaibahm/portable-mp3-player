#include <stdbool.h>
#include "button.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "driver/gpio.h"

#define TAG "BUTTON"

#define DEBOUNCE_MS 30
#define MAX_BUTTONS 4

typedef struct {
    int gpio;
    int last_level;
    uint32_t press_count;
    TickType_t pressed_at;
    bool long_press_sent;
    button_callback_t cb;
} button_t;

static button_t buttons[MAX_BUTTONS];
static volatile int button_count;
static QueueHandle_t button_queue;

static void IRAM_ATTR button_isr_handler(void *arg) {
    uint32_t index = (uint32_t)arg;
    xQueueSendFromISR(button_queue, &index, NULL);
}

static void notify(button_t *b, button_event_t event) {
    if (b->cb) {
        b->cb(b->gpio, event);
    }
}

static bool is_waiting_for_long_press(const button_t *b) {
    return b->last_level == 0 && !b->long_press_sent;
}

static void handle_level_change(button_t *b, int level) {
    b->last_level = level;

    // Active low: pressed pulls the pin to GND
    if (level == 0) {
        b->press_count++;
        b->pressed_at = xTaskGetTickCount();
        b->long_press_sent = false;
        ESP_LOGI(TAG, "Button pressed (GPIO %d, count %lu)", b->gpio, b->press_count);
        notify(b, BUTTON_EVENT_PRESSED);
    } else {
        uint32_t held_ms = pdTICKS_TO_MS(xTaskGetTickCount() - b->pressed_at);
        ESP_LOGI(TAG, "Button released (GPIO %d, held %lu ms)", b->gpio, held_ms);
        notify(b, BUTTON_EVENT_RELEASED);
        if (!b->long_press_sent) {
            notify(b, BUTTON_EVENT_SHORT_PRESS);
        }
    }
}

// How long the task can sleep before some held button reaches the long press time
static TickType_t ticks_until_next_long_press(void) {
    TickType_t now = xTaskGetTickCount();
    TickType_t long_press_ticks = pdMS_TO_TICKS(BUTTON_LONG_PRESS_MS);
    TickType_t wait = portMAX_DELAY;

    for (int i = 0; i < button_count; i++) {
        if (!is_waiting_for_long_press(&buttons[i])) {
            continue;
        }
        TickType_t held = now - buttons[i].pressed_at;
        TickType_t left = held >= long_press_ticks ? 0 : long_press_ticks - held;
        if (left < wait) {
            wait = left;
        }
    }
    return wait;
}

static void send_due_long_presses(void) {
    TickType_t now = xTaskGetTickCount();
    TickType_t long_press_ticks = pdMS_TO_TICKS(BUTTON_LONG_PRESS_MS);

    for (int i = 0; i < button_count; i++) {
        button_t *b = &buttons[i];
        if (is_waiting_for_long_press(b) && now - b->pressed_at >= long_press_ticks) {
            b->long_press_sent = true;
            ESP_LOGI(TAG, "Button long press (GPIO %d)", b->gpio);
            notify(b, BUTTON_EVENT_LONG_PRESS);
        }
    }
}

static void button_task(void *arg) {
    uint32_t index;

    while (1) {
        // Wake on a pin change, or when a held button is due for its long press
        if (xQueueReceive(button_queue, &index, ticks_until_next_long_press())) {
            // Let the contacts settle, then drop the bounce interrupts that piled up
            vTaskDelay(pdMS_TO_TICKS(DEBOUNCE_MS));
            xQueueReset(button_queue);

            // Check every button, since the reset may have dropped another button's event
            for (int i = 0; i < button_count; i++) {
                int level = gpio_get_level(buttons[i].gpio);
                if (level != buttons[i].last_level) {
                    handle_level_change(&buttons[i], level);
                }
            }
        }
        send_due_long_presses();
    }
}

static esp_err_t button_start(void) {
    button_queue = xQueueCreate(10, sizeof(uint32_t));
    if (button_queue == NULL) {
        return ESP_ERR_NO_MEM;
    }

    // Callbacks run on this task, so leave room for display/SD work
    xTaskCreate(button_task, "button_task", 4096, NULL, 10, NULL);

    // ESP_ERR_INVALID_STATE means the ISR service is already installed, which is fine
    esp_err_t ret = gpio_install_isr_service(0);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "Failed to install ISR service: %s", esp_err_to_name(ret));
        return ret;
    }
    return ESP_OK;
}

esp_err_t button_add(int gpio, button_callback_t cb) {
    if (button_count >= MAX_BUTTONS) {
        ESP_LOGE(TAG, "Too many buttons (max %d)", MAX_BUTTONS);
        return ESP_ERR_NO_MEM;
    }

    esp_err_t ret;
    if (button_queue == NULL) {
        ret = button_start();
        if (ret != ESP_OK) {
            return ret;
        }
    }

    gpio_config_t io_conf = {
        .pin_bit_mask = 1ULL << gpio,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_ANYEDGE,
    };
    ret = gpio_config(&io_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure GPIO %d: %s", gpio, esp_err_to_name(ret));
        return ret;
    }

    int index = button_count;
    // Treat a button that's already held at startup as having had its long press,
    // so it doesn't fire one (or a short press on release) for that first hold
    buttons[index] = (button_t){
        .gpio = gpio,
        .last_level = gpio_get_level(gpio),
        .long_press_sent = true,
        .cb = cb,
    };

    ret = gpio_isr_handler_add(gpio, button_isr_handler, (void *)index);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add ISR handler: %s", esp_err_to_name(ret));
        return ret;
    }

    // Only publish the button to the task once it's fully set up
    button_count = index + 1;
    ESP_LOGI(TAG, "Button ready on GPIO %d", gpio);
    return ESP_OK;
}
