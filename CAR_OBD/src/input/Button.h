#pragma once
#include "ButtonGestures.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

class Button {
public:
    esp_err_t begin();
    ButtonEvent take();
private:
    static void task(void* context);
    QueueHandle_t events_ = nullptr;
};
