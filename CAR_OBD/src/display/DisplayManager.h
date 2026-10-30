#pragma once

#include <atomic>

#include "GaugeRenderer.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

// Owns the panel; serializes complete frames and SPI brightness commands.
class DisplayManager {
public:
    void begin();
    void flush(const GaugeRenderer& renderer, DirtyRect dirty);
    void turnOn();
    esp_err_t setBrightnessPercent(unsigned percent);
    unsigned brightnessPercent() const { return brightnessPercent_.load(); }
    esp_err_t setImageBrightnessPercent(unsigned percent);
    unsigned imageBrightnessPercent() const { return imageBrightnessPercent_.load(); }
private:
    static bool transferDone(esp_lcd_panel_io_handle_t io,
                             esp_lcd_panel_io_event_data_t* event, void* context);
    esp_lcd_panel_handle_t panel_ = nullptr;
    esp_lcd_panel_io_handle_t io_ = nullptr;
    SemaphoreHandle_t transferDone_ = nullptr;
    uint16_t* transferPixels_ = nullptr;
    SemaphoreHandle_t ioMutex_ = nullptr;
    std::atomic<unsigned> brightnessPercent_{100};  // Commanded, not measured.
    std::atomic<unsigned> imageBrightnessPercent_{100};
    std::atomic<bool> imageBrightnessDirty_{false};
};
