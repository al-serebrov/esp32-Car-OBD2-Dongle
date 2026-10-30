#pragma once

#include "esp_err.h"
class DisplayManager;

namespace backlight {
// Only PWM mode touches GPIO4. SPI/image modes use the supplied panel.
esp_err_t begin(DisplayManager* display = nullptr);
esp_err_t setPercent(unsigned percent);
unsigned percent();
}  // namespace backlight
