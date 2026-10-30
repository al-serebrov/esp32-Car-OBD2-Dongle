#pragma once

#include <cstdint>
#include "Config.h"

// Calm demo-only steps; real OBD readings never use this source.
inline int simulatedSpeedKmh(uint64_t elapsedMs) {
    constexpr int speeds[] = {11, 22, 33, 44, 55, 66, 77, 88, 99, 111};
    constexpr auto count = sizeof(speeds) / sizeof(speeds[0]);
    static_assert(speeds[count - 1] <= config::kSpeedScaleKmh, "Demo exceeds gauge scale");
    return speeds[(elapsedMs / config::kDemoStepMs) % count];
}
