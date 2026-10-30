#include "DisplayManager.h"
#include "ImageBrightness.h"

#include <algorithm>
#include "Config.h"
#include "Pins.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_lcd_gc9a01.h"
#include "esp_lcd_io_spi.h"
#include "esp_log.h"
#include "logging/FieldLog.h"

namespace { constexpr char kTag[] = "display"; }

bool DisplayManager::transferDone(esp_lcd_panel_io_handle_t,
                                  esp_lcd_panel_io_event_data_t*, void* context) {
    auto* self = static_cast<DisplayManager*>(context);
    BaseType_t woken = pdFALSE;
    xSemaphoreGiveFromISR(self->transferDone_, &woken);
    // ESP-IDF 5.5's SPI panel IO discards this callback's return value.
    // Request the yield explicitly so each strip does not wait for a tick.
    if (woken == pdTRUE) portYIELD_FROM_ISR();
    return false;
}

void DisplayManager::begin() {
    ioMutex_ = xSemaphoreCreateMutex();
    ESP_ERROR_CHECK(ioMutex_ ? ESP_OK : ESP_ERR_NO_MEM);
    // Keep every unused panel deselected before any shared-bus traffic.
    for (const int pin : pins::kReservedCs) {
        ESP_ERROR_CHECK(gpio_set_level(static_cast<gpio_num_t>(pin), 1));
        ESP_ERROR_CHECK(gpio_set_direction(static_cast<gpio_num_t>(pin), GPIO_MODE_OUTPUT));
    }
    ESP_ERROR_CHECK(gpio_set_level(static_cast<gpio_num_t>(pins::kSpeedCs), 1));
    ESP_ERROR_CHECK(gpio_set_direction(static_cast<gpio_num_t>(pins::kSpeedCs), GPIO_MODE_OUTPUT));

    transferDone_ = xSemaphoreCreateBinary();
    ESP_ERROR_CHECK(transferDone_ ? ESP_OK : ESP_ERR_NO_MEM);
    transferPixels_ = static_cast<uint16_t*>(heap_caps_malloc(
        config::kWidth * config::kStripRows * sizeof(uint16_t), MALLOC_CAP_DMA));
    ESP_ERROR_CHECK(transferPixels_ ? ESP_OK : ESP_ERR_NO_MEM);

    spi_bus_config_t bus = {};
    bus.sclk_io_num = pins::kSck;
    bus.mosi_io_num = pins::kMosi;
    bus.miso_io_num = -1;
    bus.quadwp_io_num = -1;
    bus.quadhd_io_num = -1;
    bus.max_transfer_sz = config::kWidth * config::kStripRows * sizeof(uint16_t);
    ESP_ERROR_CHECK(spi_bus_initialize(SPI3_HOST, &bus, SPI_DMA_CH_AUTO));

    esp_lcd_panel_io_spi_config_t io = {};
    io.cs_gpio_num = pins::kSpeedCs;
    io.dc_gpio_num = pins::kDc;
    io.spi_mode = 0;
    io.pclk_hz = config::kSpiClockHz;
    io.trans_queue_depth = 1;
    io.on_color_trans_done = transferDone;
    io.user_ctx = this;
    io.lcd_cmd_bits = 8;
    io.lcd_param_bits = 8;
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(SPI3_HOST, &io, &io_));

    esp_lcd_panel_dev_config_t panel = {};
    panel.reset_gpio_num = pins::kReset;
    panel.rgb_ele_order = config::kBgrOrder ? LCD_RGB_ELEMENT_ORDER_BGR : LCD_RGB_ELEMENT_ORDER_RGB;
    panel.bits_per_pixel = 16;
    ESP_ERROR_CHECK(esp_lcd_new_panel_gc9a01(io_, &panel, &panel_));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel_));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_, false));
    ESP_ERROR_CHECK(esp_lcd_panel_invert_color(panel_, config::kInvertColors));
    ESP_ERROR_CHECK(esp_lcd_panel_swap_xy(panel_, config::kSwapAxes));
    ESP_ERROR_CHECK(esp_lcd_panel_mirror(panel_, config::kMirrorX, config::kMirrorY));
    ESP_LOGI(kTag, "GC9A01 configured: 240x240, SPI=%d Hz, CS=%d (write-only, no panel detection)",
             config::kSpiClockHz, pins::kSpeedCs);
}

void DisplayManager::flush(const GaugeRenderer& renderer, DirtyRect dirty) {
    if (dirty.empty() && !imageBrightnessDirty_.load()) return;
    ESP_ERROR_CHECK(xSemaphoreTake(ioMutex_, pdMS_TO_TICKS(config::kTransferTimeoutMs)) == pdTRUE ? ESP_OK : ESP_ERR_TIMEOUT);
    const bool fullImageTransfer = imageBrightnessDirty_.exchange(false);
    if (fullImageTransfer) dirty = {0, 0, config::kWidth, config::kHeight};
    const unsigned imagePercent = imageBrightnessPercent_.load();
    const int width = dirty.x1 - dirty.x0;
    for (int y = dirty.y0; y < dirty.y1; y += config::kStripRows) {
        const int rows = std::min(config::kStripRows, dirty.y1 - y);
        for (int row = 0; row < rows; ++row) {
            for (int x = 0; x < width; ++x) {
                const uint16_t pixel = dimRgb565(renderer.displayPixel(dirty.x0 + x, y + row), imagePercent);
                // GC9A01 expects RGB565 most-significant byte first on SPI.
                transferPixels_[row * width + x] = static_cast<uint16_t>((pixel << 8) | (pixel >> 8));
            }
        }
        ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(panel_, dirty.x0, y, dirty.x1, y + rows, transferPixels_));
        // Do not reuse the DMA buffer before completion. A timeout is fatal;
        // continuing would corrupt in-flight data. All waits are bounded.
        if (xSemaphoreTake(transferDone_, pdMS_TO_TICKS(config::kTransferTimeoutMs)) != pdTRUE) {
            ESP_LOGE(kTag, "SPI transfer timed out; stopping before DMA buffer reuse");
            ESP_ERROR_CHECK(ESP_ERR_TIMEOUT);
        }
    }
    xSemaphoreGive(ioMutex_);
    if (fullImageTransfer)
        FieldLog::instance().write("IMAGE_DIM_FRAME percent=%u pixels=%d", imagePercent, config::kWidth * config::kHeight);
}

void DisplayManager::turnOn() {
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_, true));
}

esp_err_t DisplayManager::setBrightnessPercent(unsigned percent) {
    if (percent > 100) return ESP_ERR_INVALID_ARG;
    if (!io_ || !ioMutex_) return ESP_ERR_INVALID_STATE;
    if (xSemaphoreTake(ioMutex_, pdMS_TO_TICKS(config::kTransferTimeoutMs)) != pdTRUE) return ESP_ERR_TIMEOUT;
    const uint8_t value = static_cast<uint8_t>((percent * 255 + 50) / 100);
    // Program the target before enabling brightness control to avoid a zero
    // default causing an initial flash. BCTRL=1, DD=1, BL=1 (register 53h).
    const uint8_t control = 0x2c;
    auto result = esp_lcd_panel_io_tx_param(io_, 0x51, &value, 1);
    if (result == ESP_OK) result = esp_lcd_panel_io_tx_param(io_, 0x53, &control, 1);
    if (result == ESP_OK) brightnessPercent_.store(percent);
    xSemaphoreGive(ioMutex_);
    FieldLog::instance().write("LCD_DIM percent=%u DBV=0x%02X CTRL=0x%02X result=%s (write-only)", percent, value, control, esp_err_to_name(result));
    return result;
}

esp_err_t DisplayManager::setImageBrightnessPercent(unsigned percent) {
    if (percent > 100) return ESP_ERR_INVALID_ARG;
    if (!io_ || !ioMutex_) return ESP_ERR_INVALID_STATE;
    if (xSemaphoreTake(ioMutex_, pdMS_TO_TICKS(config::kTransferTimeoutMs)) != pdTRUE) return ESP_ERR_TIMEOUT;
    if (imageBrightnessPercent_.load() != percent) {
        imageBrightnessPercent_.store(percent);
        // A full transfer is necessary even when the demo number stays still.
        imageBrightnessDirty_.store(true);
    }
    xSemaphoreGive(ioMutex_);
    FieldLog::instance().write("IMAGE_DIM percent=%u backlight=unchanged", percent);
    return ESP_OK;
}
