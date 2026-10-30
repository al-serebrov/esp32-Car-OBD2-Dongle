#pragma once

#include <cstdint>

namespace config {
constexpr int kWidth = 240;
constexpr int kHeight = 240;
constexpr int kStripRows = 16;
constexpr int kSpiClockHz = 20'000'000;  // Conservative breadboard starting point.
constexpr int kFrameIntervalMs = 50;    // At most 20 FPS, redraw only on changes.
constexpr int kTransferTimeoutMs = 1000;
constexpr int kSplashMs = 800;
constexpr int kSpeedScaleKmh = 160;
constexpr int kDemoStepMs = 3000;
constexpr bool kWarningIconPreviewEnabled = true; // DEMO only; LIVE uses measured states.
constexpr char kAdapterName[] = "V-LINK";
constexpr char kAdapterPin[] = "1234";
constexpr int kSpeedPollMs = 250;
constexpr int kLiveStaleMs = 1500;
constexpr int kDetailStaleMs = 10'000;
constexpr bool kEngineMonitorEnabled = true;
constexpr bool kCandidateDiagnostics = true; // Page three: validate the responsive Hyundai blocks.
constexpr int kMilPollMs = 10'000;
constexpr int kCoolantPollMs = 20'000;
constexpr int kVoltagePollMs = 20'000;
constexpr int kEngineMonitorTimeoutMs = 1000;
constexpr int kMilStaleMs = 3 * kMilPollMs;
constexpr int kCoolantStaleMs = 3 * kCoolantPollMs;
constexpr int kVoltageStaleMs = 3 * kVoltagePollMs;
constexpr int kVoltageWarningOnMv = 12'600; // Strictly below this voltage.
constexpr int kVoltageWarningOffMv = 12'800;
static_assert(kVoltageWarningOnMv < kVoltageWarningOffMv, "Voltage alert needs hysteresis");
constexpr int kTemperatureWarningOnC = 105; // Configurable early alert, not a verified factory threshold.
constexpr int kTemperatureWarningOffC = 100;
static_assert(kTemperatureWarningOffC < kTemperatureWarningOnC, "Temperature alert needs hysteresis");
// Targeted read-only experiment. 770/778 + 22BC03 is documented on newer
// Hyundais; neither access nor its bit layout is verified on this LM ix35.
constexpr bool kHyundaiBodyProbeEnabled = false;
constexpr int kBodyProbeMaxSpeedKmh = 10; // Probe may start only strictly below this speed.
constexpr int kBodyProbeStartupMs = 10'000;
constexpr int kBodyProbePollMs = 5'000;
constexpr int kBodyProbeRetryMs = 60'000;
constexpr int kBodyProbeBudgetMs = 1000;
constexpr int kBodyProbeCommandTimeoutMs = 600;
// Default is LED -> 3V3. Define OBD_BACKLIGHT_PWM=1 only with GPIO-safe wiring.
#ifndef OBD_BACKLIGHT_PWM
#define OBD_BACKLIGHT_PWM 0
#endif
constexpr bool kBacklightPwmEnabled = OBD_BACKLIGHT_PWM != 0;
#ifndef OBD_BACKLIGHT_SPI
#define OBD_BACKLIGHT_SPI 0
#endif
constexpr bool kBacklightSpiEnabled = OBD_BACKLIGHT_SPI != 0;
#ifndef OBD_BACKLIGHT_IMAGE
#define OBD_BACKLIGHT_IMAGE 0
#endif
constexpr bool kImageDimmingEnabled = OBD_BACKLIGHT_IMAGE != 0;
constexpr int kSpiDimmingTestStepMs = 6000;
static_assert(static_cast<int>(kBacklightPwmEnabled) + static_cast<int>(kBacklightSpiEnabled) +
              static_cast<int>(kImageDimmingEnabled) <= 1, "Select one brightness interface");
constexpr int kBacklightPwmHz = 5000;
constexpr unsigned kBacklightStartupPercent = 100;
// Provisional module settings: verify on the physical display.
constexpr bool kInvertColors = true;
constexpr bool kBgrOrder = true;
constexpr bool kSwapAxes = false;
constexpr bool kMirrorX = true;  // Corrects mirrored text on the connected module.
constexpr bool kMirrorY = false;
static_assert(kDemoStepMs > 0, "Demo step duration must be positive");
}  // namespace config
