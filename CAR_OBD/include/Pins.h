#pragma once

namespace pins {
constexpr int kSck = 18;
constexpr int kMosi = 23;
constexpr int kDc = 27;
constexpr int kReset = 26;
constexpr int kSpeedCs = 25;
constexpr int kBacklight = 4;  // PWM control; verify LED interface / use a suitable driver.
constexpr int kButton = 21;  // Normally open button to GND; internal pull-up.
// Reserved only: no additional panel is initialized in Phase 1.
constexpr int kReservedCs[] = {32, 33};
// Backlight and display power must be wired according to the module datasheet.
}  // namespace pins
