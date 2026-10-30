#pragma once

#include <cstddef>
#include <cstdint>
#include "HyundaiBody.h"
#include "DiagnosticRun.h"
#include "TransmissionPoll.h"

namespace obd {
enum class Parse { Valid, Missing, Malformed, AdapterError, Conflicting };
// Combine valid 0100 capability bitmaps; require agreement for measurements.
Parse parsePid(const char* response, uint8_t pid, uint8_t* payload, size_t payloadSize);
Parse parseVoltage(const char* response, int& millivolts);
// Aggregate commanded MIL with OR, and retain the maximum reported count.
Parse parseMilStatus(const char* response, uint8_t& status, unsigned& responseLines);
const char* parseName(Parse result);

// Single outstanding request. No transport, heap allocation or hardware access.
class Elm327 {
public:
    enum class Result { None, Initialized, Supported, Unsupported, Speed, Detail, Monitor, MonitorError, Diagnostic, Driving, DrivingError, BodySample, BodyError, Error };
    void reset(uint64_t now);
    const char* nextCommand(uint64_t now);
    Result receive(char byte, uint64_t now);
    bool timedOut(uint64_t now);
    bool alive() const { return alive_; }
    DiagnosticRun& diagnostics() { return diagnostics_; }
    bool diagnosticTransaction() const { return diagnosticTransaction_; }
    bool requestPending() const { return waiting_; }
    bool speedTransaction() const { return waiting_ && step_ == 7 && metric_ < 0 && monitor_ < 0 && drivingMetric_ < 0 && !transmission_.active() && !body_.active() && !diagnosticTransaction_; }
    void setDrivingActive(bool active) { drivingActive_ = active; transmission_.enable(active); }
    int drivingMetric() const { return drivingMetric_; } // RPM, load, throttle, transmission.
    const TransmissionReading& transmissionReading() const { return transmission_.reading(); }
    void setDetailsActive(bool active) { detailsActive_ = active; }
    void setEngineMonitorEnabled(bool enabled) { monitorEnabled_ = enabled; }
    int monitor() const { return monitor_; } // 0 = MIL, 1 = coolant, 2 = adapter voltage; -1 otherwise.
    unsigned milResponseLines() const { return milResponseLines_; }
    void setBodyProbeEnabled(bool enabled) { bodyEnabled_ = enabled && !bodySuppressed_; }
    bool bodyProbeSuppressed() const { return bodySuppressed_; }
    const uint8_t* bodyData() const { return body_.data(); }
    int metric() const { return metric_; }  // -1 for speed/initialization.
    int value() const { return value_; }
    int speed() const { return speed_; }
    const char* response() const { return response_; }
    const char* reason() const { return reason_; }
    size_t pendingBytes() const { return used_; }
private:
    char response_[512] = {};
    const char* reason_ = "none";
    size_t used_ = 0;
    int step_ = 0;
    int speed_ = 0;
    bool waiting_ = false;
    bool overflow_ = false;
    bool alive_ = false;
    uint64_t due_ = 0;
    uint64_t deadline_ = 0;
    bool detailsActive_ = false, detailTurn_ = false;
    int metric_ = -1, nextMetric_ = 0, value_ = 0;
    uint64_t retryMetricAt_[4] = {};
    HyundaiBodyProbe body_;
    uint64_t bodyDeadline_ = 0;
    bool bodyEnabled_ = false, speedReady_ = false, bodySuppressed_ = false;
    bool monitorEnabled_ = false, monitorStarted_ = false;
    int monitor_ = -1;
    uint64_t monitorAt_[3] = {};
    unsigned milResponseLines_ = 0;
    DiagnosticRun diagnostics_;
    bool diagnosticTransaction_ = false;
    TransmissionPoll transmission_;
    uint64_t transmissionDeadline_ = 0, drivingAt_[3] = {};
    bool drivingActive_ = false;
    int drivingMetric_ = -1;
    unsigned nextDriving_ = 0;
};
}  // namespace obd
