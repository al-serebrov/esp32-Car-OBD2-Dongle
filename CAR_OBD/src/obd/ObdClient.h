#pragma once

#include <atomic>
#include <cstdint>
#include "Elm327.h"
#include "Telemetry.h"
#include "esp_gap_bt_api.h"
#include "esp_spp_api.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

class ObdClient {
public:
    bool begin();
    SpeedReading reading();
    DetailReadings details();
    EngineReadings engineReadings();
    DrivingReadings drivingReadings();
    void setDrivingActive(bool active) { drivingActive_.store(active); }
    void setDetailsActive(bool active) { detailsActive_.store(active); }
    void setDiagnosticsActive(bool active) { diagnosticsActive_.store(active); }
    DiagnosticStatus diagnosticStatus();
    static void dumpDiagnosticReport();
private:
    enum class Kind { Init, Found, ScanDone, Services, Opening, Open, Closed, Data, Write, Congested, AuthFailed };
    struct Event {
        Kind kind;
        uint32_t handle = 0;
        uint64_t receivedMs = 0;
        int status = 0;
        int length = 0;
        bool flag = false;
        uint8_t address[6] = {};
        uint8_t data[128] = {};
    };
    enum class State { WaitInit, Scan, Services, Connecting, Open, Backoff };
    static void gap(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t* param);
    static void spp(esp_spp_cb_event_t event, esp_spp_cb_param_t* param);
    static void task(void* context);
    void enqueue(const Event& event);
    void handle(const Event& event, uint64_t now);
    void tick(uint64_t now);
    void retry(const char* reason, uint64_t now);
    void invalidate();
    void logDiagnostic();
    void saveDiagnostics(uint64_t now, bool force = false);
    static ObdClient* instance_;
    QueueHandle_t events_ = nullptr;
    std::atomic<unsigned> dropped_{0};
    portMUX_TYPE readingLock_ = portMUX_INITIALIZER_UNLOCKED;
    SpeedReading reading_;
    DetailReadings details_;
    MetricReading mil_;
    DrivingReadings driving_;
    std::atomic<bool> drivingActive_{false};
    std::atomic<bool> detailsActive_{false};
    std::atomic<bool> diagnosticsActive_{false};
    DiagnosticStatus diagnosticStatus_;
    uint64_t lastDiagnosticSave_ = 0, nextDiagnosticSaveAttempt_ = 0;
    unsigned savedTested_ = 0;
    bool saveRequested_ = false;
    State state_ = State::WaitInit;
    uint64_t deadline_ = 0;
    uint64_t transportDeadline_ = 0;
    uint32_t handle_ = 0;
    uint8_t target_[6] = {};
    bool candidate_ = false;
    bool congested_ = false;
    bool writing_ = false;
    int writeLength_ = 0;
    unsigned failures_ = 0;
    obd::Elm327 elm_;
};
