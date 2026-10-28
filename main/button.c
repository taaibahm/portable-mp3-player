#include "button.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "driver/gpio.h"

#define TAG "BUTTON"

#define DEBOUNCE_MS 30

static QueueHandle_t button_queue;

static void IRAM_ATTR button_isr_handler(void *arg) {
    uint32_t gpio_num = (uint32_t)arg;
    xQueueSendFromISR(button_queue, &gpio_num, NULL);
}

static void button_task(void *arg) {
    uint32_t gpio_num;
    int last_level = gpio_get_level(PIN_NUM_BUTTON);
    uint32_t press_count = 0;
    TickType_t pressed_at = 0;

    while (1) {
        if (xQueueReceive(button_queue, &gpio_num, portMAX_DELAY)) {
            // Let the contacts settle, then drop the bounce interrupts that piled up
            vTaskDelay(pdMS_TO_TICKS(DEBOUNCE_MS));
            xQueueReset(button_queue);

            int level = gpio_get_level(gpio_num);
            if (level == last_level) {
                continue;
            }
            last_level = level;

            // Active low: pressed pulls the pin to GND
            if (level == 0) {
                press_count++;
                pressed_at = xTaskGetTickCount();
                ESP_LOGI(TAG, "Button pressed (GPIO %lu, count %lu)", gpio_num, press_count);
            } else {
                uint32_t held_ms = pdTICKS_TO_MS(xTaskGetTickCount() - pressed_at);
                ESP_LOGI(TAG, "Button released (held %lu ms)", held_ms);
            }
        }
    }
}

esp_err_t button_init(void) {
    gpio_config_t io_conf = {
        .pin_bit_mask = 1ULL << PIN_NUM_BUTTON,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_ANYEDGE,
    };
    esp_err_t ret = gpio_config(&io_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure GPIO %d: %s", PIN_NUM_BUTTON, esp_err_to_name(ret));
        return ret;
    }

    button_queue = xQueueCreate(10, sizeof(uint32_t));
    if (button_queue == NULL) {
        return ESP_ERR_NO_MEM;
    }

    xTaskCreate(button_task, "button_task", 2048, NULL, 10, NULL);

    // ESP_ERR_INVALID_STATE means the ISR service is already installed, which is fine
    ret = gpio_install_isr_service(0);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "Failed to install ISR service: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = gpio_isr_handler_add(PIN_NUM_BUTTON, button_isr_handler, (void *)PIN_NUM_BUTTON);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add ISR handler: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "Button ready on GPIO %d", PIN_NUM_BUTTON);
    return ESP_OK;
}
