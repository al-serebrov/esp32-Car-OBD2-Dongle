#include "Button.h"
#include "Pins.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "freertos/task.h"
#include "logging/FieldLog.h"

esp_err_t Button::begin() {
    gpio_config_t cfg = {};
    cfg.pin_bit_mask = 1ULL << pins::kButton;
    cfg.mode = GPIO_MODE_INPUT;
    cfg.pull_up_en = GPIO_PULLUP_ENABLE;
    cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
    cfg.intr_type = GPIO_INTR_DISABLE;
    const auto result = gpio_config(&cfg);
    if (result != ESP_OK) return result;
    events_ = xQueueCreate(8, sizeof(ButtonEvent));
    if (!events_) return ESP_ERR_NO_MEM;
    if (xTaskCreate(task, "button", 2048, this, 1, nullptr) != pdPASS) {
        vQueueDelete(events_); events_ = nullptr;
        return ESP_ERR_NO_MEM;
    }
    FieldLog::instance().write("BUTTON_INIT gpio=%d active_low=1 debounce_ms=30 double_ms=300 hold_ms=1000", pins::kButton);
    return ESP_OK;
}

ButtonEvent Button::take() {
    ButtonEvent event = ButtonEvent::None;
    if (events_) xQueueReceive(events_, &event, 0);
    return event;
}

void Button::task(void* context) {
    auto* self = static_cast<Button*>(context);
    ButtonGestures gestures;
    gestures.reset(gpio_get_level(static_cast<gpio_num_t>(pins::kButton)) == 0, esp_timer_get_time() / 1000);
    TickType_t next = xTaskGetTickCount();
    for (;;) {
        const auto event = gestures.update(gpio_get_level(static_cast<gpio_num_t>(pins::kButton)) == 0, esp_timer_get_time() / 1000);
        if (event != ButtonEvent::None && xQueueSend(self->events_, &event, 0) != pdTRUE)
            FieldLog::instance().write("BUTTON_QUEUE_OVERFLOW event=%d", static_cast<int>(event));
        vTaskDelayUntil(&next, pdMS_TO_TICKS(10));
    }
}
