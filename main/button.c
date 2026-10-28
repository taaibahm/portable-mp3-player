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
    button_callback_t cb;
} button_t;

static button_t buttons[MAX_BUTTONS];
static volatile int button_count;
static QueueHandle_t button_queue;

static void IRAM_ATTR button_isr_handler(void *arg) {
    uint32_t index = (uint32_t)arg;
    xQueueSendFromISR(button_queue, &index, NULL);
}

static void button_task(void *arg) {
    uint32_t index;

    while (1) {
        if (xQueueReceive(button_queue, &index, portMAX_DELAY)) {
            // Let the contacts settle, then drop the bounce interrupts that piled up
            vTaskDelay(pdMS_TO_TICKS(DEBOUNCE_MS));
            xQueueReset(button_queue);

            // Check every button, since the reset may have dropped another button's event
            for (int i = 0; i < button_count; i++) {
                button_t *b = &buttons[i];
                int level = gpio_get_level(b->gpio);
                if (level == b->last_level) {
                    continue;
                }
                b->last_level = level;

                // Active low: pressed pulls the pin to GND
                bool pressed = level == 0;
                if (pressed) {
                    b->press_count++;
                    b->pressed_at = xTaskGetTickCount();
                    ESP_LOGI(TAG, "Button pressed (GPIO %d, count %lu)", b->gpio, b->press_count);
                } else {
                    uint32_t held_ms = pdTICKS_TO_MS(xTaskGetTickCount() - b->pressed_at);
                    ESP_LOGI(TAG, "Button released (GPIO %d, held %lu ms)", b->gpio, held_ms);
                }

                if (b->cb) {
                    b->cb(b->gpio, pressed);
                }
            }
        }
    }
}

static esp_err_t button_start(void) {
    button_queue = xQueueCreate(10, sizeof(uint32_t));
    if (button_queue == NULL) {
        return ESP_ERR_NO_MEM;
    }

    // Callbacks run on this task, so leave room for display/log work
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
    buttons[index] = (button_t){
        .gpio = gpio,
        .last_level = gpio_get_level(gpio),
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
