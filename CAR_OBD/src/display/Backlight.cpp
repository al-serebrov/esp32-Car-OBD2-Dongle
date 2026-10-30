#include "Backlight.h"
#include "DisplayManager.h"

#include <atomic>
#include "Config.h"
#include "Pins.h"
#include "driver/ledc.h"
#include "logging/FieldLog.h"

namespace backlight {
namespace {
constexpr auto kMode = LEDC_LOW_SPEED_MODE;
constexpr auto kTimer = LEDC_TIMER_0;
constexpr auto kChannel = LEDC_CHANNEL_0;
constexpr unsigned kMaxDuty = (1u << 10) - 1;
std::atomic<bool> ready{false};
DisplayManager* display = nullptr;
static_assert(config::kBacklightStartupPercent <= 100, "Brightness must be 0..100");
}  // namespace

esp_err_t begin(DisplayManager* panel) {
    if (config::kBacklightSpiEnabled || config::kImageDimmingEnabled) {
        if (!panel) return ESP_ERR_INVALID_ARG;
        display = panel;
        ready.store(true);
        if (config::kImageDimmingEnabled)
            FieldLog::instance().write("BACKLIGHT mode=IMAGE GPIO%d unused; backlight remains fixed", pins::kBacklight);
        else FieldLog::instance().write("BACKLIGHT mode=SPI GPIO%d unused; optical support unverified", pins::kBacklight);
        return ESP_OK;
    }
    if (!config::kBacklightPwmEnabled) {
        FieldLog::instance().write("BACKLIGHT external 3V3; GPIO%d PWM disabled", pins::kBacklight);
        return ESP_OK;
    }
    ledc_timer_config_t timer = {};
    timer.speed_mode = kMode;
    timer.duty_resolution = LEDC_TIMER_10_BIT;
    timer.timer_num = kTimer;
    timer.freq_hz = config::kBacklightPwmHz;
    timer.clk_cfg = LEDC_AUTO_CLK;
    auto result = ledc_timer_config(&timer);
    if (result != ESP_OK) return result;

    ledc_channel_config_t channel = {};
    channel.gpio_num = pins::kBacklight;
    channel.speed_mode = kMode;
    channel.channel = kChannel;
    channel.timer_sel = kTimer;
    channel.intr_type = LEDC_INTR_DISABLE;
    channel.duty = 0;  // Illuminate only after the first complete frame is sent.
    result = ledc_channel_config(&channel);
    if (result != ESP_OK) return result;
    // ESP-IDF's thread-safe duty API uses the fade service's channel lock,
    // even when applying an immediate duty change rather than a fade.
    result = ledc_fade_func_install(0);
    if (result == ESP_OK) {
        ready.store(true);
        FieldLog::instance().write("BACKLIGHT_PWM gpio=%d frequency=%d resolution=10 initial=0", pins::kBacklight, config::kBacklightPwmHz);
    }
    return result;
}

esp_err_t setPercent(unsigned percent) {
    if (percent > 100) return ESP_ERR_INVALID_ARG;
    if (!ready.load()) return ESP_ERR_INVALID_STATE;
    if (config::kImageDimmingEnabled) return display->setImageBrightnessPercent(percent);
    if (config::kBacklightSpiEnabled) {
        const auto result = display->setBrightnessPercent(percent);
        FieldLog::instance().write("BACKLIGHT_SET mode=SPI percent=%u result=%s", percent, esp_err_to_name(result));
        return result;
    }
    const unsigned duty = (percent * kMaxDuty + 50) / 100;
    // Thread-safe when called from the USB console after startup.
    const auto result = ledc_set_duty_and_update(kMode, kChannel, duty, 0);
    FieldLog::instance().write("BACKLIGHT_SET percent=%u duty=%u result=%s", percent, duty, esp_err_to_name(result));
    return result;
}
unsigned percent() {
    if (!ready.load()) return 0;
    if (config::kImageDimmingEnabled) return display->imageBrightnessPercent();
    if (config::kBacklightSpiEnabled) return display->brightnessPercent();
    return (ledc_get_duty(kMode, kChannel) * 100u + kMaxDuty / 2) / kMaxDuty;
}
}  // namespace backlight
