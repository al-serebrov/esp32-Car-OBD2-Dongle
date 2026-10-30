#pragma once
#include <cstdint>

// RGB intensity scaling only: this does not control the panel's LED backlight.
inline uint16_t dimRgb565(uint16_t color, unsigned percent) {
    if (percent >= 100) return color;
    const unsigned red = (((color >> 11) & 31u) * percent + 50) / 100;
    const unsigned green = (((color >> 5) & 63u) * percent + 50) / 100;
    const unsigned blue = ((color & 31u) * percent + 50) / 100;
    return static_cast<uint16_t>((red << 11) | (green << 5) | blue);
}
