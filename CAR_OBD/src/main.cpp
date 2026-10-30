#include <cinttypes>
#include <atomic>
#include <cerrno>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include "Config.h"
#include "display/Backlight.h"
#include "display/DisplayManager.h"
#include "display/GaugeRenderer.h"
#include "input/Button.h"
#include "logging/FieldLog.h"
#include "obd/ObdClient.h"
#include "telemetry/SimulatedSpeed.h"
#include "telemetry/EngineWarnings.h"
#include "esp_app_desc.h"
#include "esp_check.h"
#include "esp_chip_info.h"
#include "esp_heap_caps.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace {
std::atomic<bool> dimmingTest{false};
std::atomic<bool> overlayTest{false};
std::atomic<int> requestedPage{-1};

void console(FieldLog& log) {
    static char command[32] = {};
    static size_t used = 0;
    char input[32];
    const auto length = read(STDIN_FILENO, input, sizeof(input)); // UART VFS defaults to nonblocking.
    for (int i = 0; i < length; ++i) {
        const char c = input[i];
        if (c == '\n' || c == '\r') {
            command[used] = 0;
            if (!std::strcmp(command, "logs")) log.dump();
            else if (!std::strcmp(command, "diagreport")) ObdClient::dumpDiagnosticReport();
            else if (!std::strcmp(command,"overlaytest on")) { overlayTest.store(true);std::puts("OVERLAYTEST_ON"); }
            else if (!std::strcmp(command,"overlaytest off")) { overlayTest.store(false);std::puts("OVERLAYTEST_OFF"); }
            else if (used==6 && !std::strncmp(command,"page ",5) && command[5]>='1' && command[5]<='4') {
                requestedPage.store(command[5]-'1');std::printf("PAGE_REQUESTED %c\n",command[5]);
            }
            else if (!std::strcmp(command, "dimtest on")) {
                if (config::kBacklightSpiEnabled || config::kImageDimmingEnabled) {
                    dimmingTest.store(true);
                    std::puts("DIMTEST_ON cycle=100,50,0 step_ms=6000");
                } else std::puts("DIMTEST_ERROR SPI or image dimming is not enabled");
            } else if (!std::strcmp(command, "dimtest off")) {
                dimmingTest.store(false);
                std::puts("DIMTEST_OFF restore pending");
            }
            else if (!std::strncmp(command, "brightness ", 11)) {
                char* end = nullptr;
                errno = 0;
                const long percent = std::strtol(command + 11, &end, 10);
                const bool parsed = end != command + 11 && errno == 0;
                while (std::isspace(static_cast<unsigned char>(*end))) ++end;
                if (!parsed || *end || percent < 0 || percent > 100) {
                    std::puts("BRIGHTNESS_ERROR expected integer 0..100");
                } else if (dimmingTest.load()) {
                    std::puts("BRIGHTNESS_ERROR use dimtest off before manual brightness");
                } else {
                    const auto result = backlight::setPercent(static_cast<unsigned>(percent));
                    if (result == ESP_OK) std::printf("BRIGHTNESS_OK percent=%ld\n", percent);
                    else std::printf("BRIGHTNESS_ERROR %s (enable a brightness interface)\n", esp_err_to_name(result));
                }
            }
            else if (used) std::puts("Commands: logs, diagreport, page 1..4, overlaytest on/off, brightness 0..100, dimtest on/off, help");
            used = 0;
        } else if (c >= 32 && c <= 126 && used + 1 < sizeof(command)) command[used++] = c;
    }
}
void consoleTask(void*) {
    for (;;) {
        console(FieldLog::instance());
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
}  // namespace

extern "C" void app_main() {
    auto& log = FieldLog::instance();
    ESP_ERROR_CHECK(log.begin() ? ESP_OK : ESP_FAIL);
    const auto* app = esp_app_get_description();
    const auto* hash = app->app_elf_sha256;
    log.write("BOOT version=%s elf=%02x%02x%02x%02x reset_reason=%d target=%s poll_ms=%d stale_ms=%d",
              app->version, hash[0], hash[1], hash[2], hash[3], static_cast<int>(esp_reset_reason()),
              config::kAdapterName, config::kSpeedPollMs, config::kLiveStaleMs);
    esp_chip_info_t chip = {};
    esp_chip_info(&chip);
    log.write("HARDWARE chip=%d cores=%d revision=%d classic_bt=%d", static_cast<int>(chip.model), chip.cores, chip.revision, !!(chip.features & CHIP_FEATURE_BT));
    auto nvs = nvs_flash_init();
    if (nvs == ESP_ERR_NVS_NO_FREE_PAGES || nvs == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        log.write("NVS_REINITIALIZE reason=%s (field logs retained)", esp_err_to_name(nvs));
        ESP_ERROR_CHECK(nvs_flash_erase()); nvs = nvs_flash_init();
    }
    ESP_ERROR_CHECK(nvs);

    // Classic BT reserves RAM in the middle of DRAM. Allocate the same RGB565
    // framebuffer in independent 16-row bands rather than a single 115 KB block.
    static_assert(config::kHeight % config::kStripRows == 0, "Whole framebuffer bands required");
    uint16_t* bands[config::kHeight / config::kStripRows] = {};
    for (auto& band : bands) {
        band = static_cast<uint16_t*>(heap_caps_calloc(
            config::kWidth * config::kStripRows, sizeof(uint16_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
        ESP_ERROR_CHECK(band ? ESP_OK : ESP_ERR_NO_MEM);
    }
    static GaugeRenderer gauge(bands);
    DisplayManager display;
    ESP_ERROR_CHECK(backlight::begin(&display));
    display.begin(); gauge.splash();
    display.flush(gauge, gauge.takeDirty()); display.turnOn();
    if (config::kBacklightPwmEnabled || config::kBacklightSpiEnabled || config::kImageDimmingEnabled)
        ESP_ERROR_CHECK(backlight::setPercent(config::kBacklightStartupPercent));
    vTaskDelay(pdMS_TO_TICKS(config::kSplashMs));
    gauge.setDemoWarnings(config::kWarningIconPreviewEnabled);
    gauge.begin(); gauge.showSpeed(simulatedSpeedKmh(0));
    display.flush(gauge, gauge.takeDirty());

    static ObdClient obd; // Discovery report belongs in static RAM, not app_main's stack.
    Button button;
    ESP_ERROR_CHECK(button.begin());
    if (!obd.begin()) log.write("BT_UNAVAILABLE initialization failed; remaining in DEMO");
    ESP_ERROR_CHECK(xTaskCreate(consoleTask, "usb_console", 3072, nullptr, 1, nullptr) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM);
    log.write("UI_MODE DEMO; USB commands: help, logs, diagreport, page 1..4, overlaytest on/off, brightness 0..100, dimtest on/off");
    log.write("DRIVING_CONFIG RPM=010C load=0104 throttle=0111 gear=21A0 tx=7E1 rx=7E9 profile=Hyundai_LM speed_interleaved=1");
    log.write("DIAG_CONFIG page=4 profile=%s active_custom_candidates=%u readonly=1 mapping_verified=0 report_bytes=%u",
              config::kCandidateDiagnostics ? "CANDIDATES" : "DISCOVERY", config::kCandidateDiagnostics ? 3u : obd::DiagnosticRun::kCustomCount,
              static_cast<unsigned>(sizeof(obd::DiagnosticRun::Report)));
    log.write("ENGINE_MONITOR enabled=%d mil_ms=%d coolant_ms=%d voltage_ms=%d body_below_kmh=%d demo_preview=%d",
              config::kEngineMonitorEnabled, config::kMilPollMs, config::kCoolantPollMs, config::kVoltagePollMs,
              config::kBodyProbeMaxSpeedKmh, config::kWarningIconPreviewEnabled);
    log.write("WARNING_THRESHOLDS temperature_on_C=%d off_C=%d low_voltage_below_mV=%d clears_mV=%d voltage_source=ATRV",
              config::kTemperatureWarningOnC, config::kTemperatureWarningOffC,
              config::kVoltageWarningOnMv, config::kVoltageWarningOffMv);
    log.write("BODY_PROBE enabled=%d tx=770 rx=778 DID=BC03 startup_ms=%d poll_ms=%d retry_ms=%d mapping_verified=0 log_only=1",
              config::kHyundaiBodyProbeEnabled, config::kBodyProbeStartupMs, config::kBodyProbePollMs, config::kBodyProbeRetryMs);
    const int64_t startUs = esp_timer_get_time();
    int64_t demoStartUs = startUs;
    int64_t lastLogUs = startUs, maxDrawUs = 0;
    bool wasLive = false;
    int lastDemoSpeed = -1;
    int dimmingTestStep = -1;
    int64_t dimmingTestStartUs = 0;
    bool day = false;
    unsigned page = 0;
    constexpr const char* pageNames[] = {"SPEED", "DRIVING", "DETAILS", "DIAGNOSTICS"};
    EngineWarnings engineWarnings;
    EngineWarningState lastWarnings;
    TickType_t nextFrame = xTaskGetTickCount();
    for (;;) {
        const int64_t nowUs = esp_timer_get_time();
        for (auto event = button.take(); event != ButtonEvent::None; event = button.take()) {
            if (event == ButtonEvent::Single) {
                day = !day;
                log.write("BUTTON single; THEME %s", day ? "DAY" : "NIGHT");
            } else if (event == ButtonEvent::Double) {
                page = (page + 1) % 4;
                log.write("BUTTON double; PAGE %s", pageNames[page]);
            } else if (event == ButtonEvent::Hold) {
                if (dimmingTest.load()) {
                    log.write("BUTTON hold ignored; dimming test controls brightness");
                    continue;
                }
                const unsigned next = backlight::percent() == 100 ? 50 : 100;
                const auto result = backlight::setPercent(next);
                log.write("BUTTON hold; BRIGHTNESS percent=%u result=%s", backlight::percent(), esp_err_to_name(result));
            }
        }
        const int usbPage=requestedPage.exchange(-1);
        if(usbPage>=0){page=static_cast<unsigned>(usbPage);log.write("USB_PAGE %s",pageNames[page]);}
        obd.setDrivingActive(page==1);
        obd.setDetailsActive(page==2);
        obd.setDiagnosticsActive(page==3);
        if (!dimmingTest.load() && dimmingTestStep >= 0) {
            ESP_ERROR_CHECK(backlight::setPercent(100));
            gauge.clearDimmingTest();
            dimmingTestStep = -1;
            log.write("DIM_TEST stopped; restored=100");
            std::puts("DIMTEST_STOPPED brightness=100");
        }
        gauge.setPage(day, page);
        const auto reading = obd.reading();
        const auto engine=obd.engineReadings();
        const auto driving=obd.drivingReadings();
        // Capture time after the snapshots, including any page redraw. A
        // freshly received sample must never appear future-dated on a page switch.
        const uint64_t nowMs=static_cast<uint64_t>(esp_timer_get_time()/1000);
        const bool live = isLive(reading, nowMs);
        const auto warnings = engineWarnings.update(engine, nowMs, live);
        if (warnings.mil != lastWarnings.mil || warnings.showMil != lastWarnings.showMil)
            log.write("WARNING check_engine state=%s visible=%d", warningStateName(warnings.mil), warnings.showMil);
        if (warnings.temperature != lastWarnings.temperature || warnings.showTemperature != lastWarnings.showTemperature)
            log.write("WARNING temperature state=%s visible=%d", warningStateName(warnings.temperature), warnings.showTemperature);
        if (warnings.voltage != lastWarnings.voltage || warnings.showVoltage != lastWarnings.showVoltage)
            log.write("WARNING low_voltage state=%s visible=%d", warningStateName(warnings.voltage), warnings.showVoltage);
        lastWarnings = warnings;
        gauge.setDemoWarnings(!live && config::kWarningIconPreviewEnabled);
        if (live) {
            gauge.setWarning(WarningIcon::CheckEngine, warnings.showMil);
            gauge.setWarning(WarningIcon::Overheat, warnings.showTemperature);
            gauge.setWarning(WarningIcon::Battery, warnings.showVoltage);
        }
        if (live != wasLive) {
            if (!live) demoStartUs = nowUs;
            log.write("UI_MODE %s reason=%s reading_age_ms=%" PRIu64,
                      live ? "LIVE" : "DEMO", live ? "valid 010D response" : "invalid/disconnected/stale reading", nowMs>=reading.receivedMs?nowMs-reading.receivedMs:0);
            wasLive = live;
        }
        const int speed = live ? reading.kmh : page==1 ? 88 : simulatedSpeedKmh(static_cast<uint64_t>((nowUs - demoStartUs) / 1000));
        if (live) lastDemoSpeed = -1;
        else if (speed != lastDemoSpeed) {
            log.write("DEMO_STEP kmh=%d step_ms=%d", speed, config::kDemoStepMs);
            lastDemoSpeed = speed;
        }
        if(page==1) {
            if(live)gauge.showDriving(speed,true,driving,nowMs);
            else {
                const DrivingReadings demo{{2600,nowMs,true},{38,nowMs,true},{24,nowMs,true},{85,nowMs,true},{13800,nowMs,true},{8,4,nowMs,true}};
                gauge.showDriving(speed,false,demo,nowMs);
            }
        }
        else if (page == 2) gauge.showDetails(obd.details(), nowMs);
        else if (page == 3) gauge.showDiagnostics(obd.diagnosticStatus());
        else { gauge.setLive(live); gauge.showSpeed(speed); }
        gauge.updateMilOverlay((live && warnings.showMil) || overlayTest.load(),nowMs);
        int nextBrightness = -1;
        if (dimmingTest.load()) {
            if (dimmingTestStep < 0) dimmingTestStartUs = nowUs;
            const int step = static_cast<int>((nowUs - dimmingTestStartUs) / (config::kSpiDimmingTestStepMs * 1000LL) % 3);
            if (step != dimmingTestStep) {
                constexpr int levels[] = {100, 50, 0};
                nextBrightness = levels[step];
                dimmingTestStep = step;
            }
            gauge.showDimmingTestPercent(nextBrightness >= 0 ? static_cast<unsigned>(nextBrightness) : backlight::percent());
        }
        display.flush(gauge, gauge.takeDirty());
        if (nextBrightness >= 0) ESP_ERROR_CHECK(backlight::setPercent(static_cast<unsigned>(nextBrightness)));
        const int64_t drawUs = esp_timer_get_time() - nowUs;
        if (drawUs > maxDrawUs) maxDrawUs = drawUs;
        if (nowUs - lastLogUs >= 10'000'000) {
            log.write("HEALTH mode=%s shown_kmh=%d max_draw_us=%" PRId64 " heap=%u largest=%u stack=%u page=%s theme=%s brightness=%u",
                      live ? "LIVE" : "DEMO", speed, maxDrawUs, static_cast<unsigned>(esp_get_free_heap_size()),
                      static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)),
                      static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)),
                      pageNames[page], day ? "DAY" : "NIGHT", backlight::percent());
            maxDrawUs = 0; lastLogUs = nowUs;
        }
        if (esp_timer_get_time() - nowUs >= config::kFrameIntervalMs * 1000 ||
            xTaskGetTickCount() - nextFrame > pdMS_TO_TICKS(config::kFrameIntervalMs * 2)) nextFrame = xTaskGetTickCount();
        vTaskDelayUntil(&nextFrame, pdMS_TO_TICKS(config::kFrameIntervalMs));
    }
}
