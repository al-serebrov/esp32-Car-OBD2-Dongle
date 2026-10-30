#pragma once

#include "Telemetry.h"

enum class WarningState { Unknown, Clear, Active };
inline const char* warningStateName(WarningState state) {
    switch (state) {
        case WarningState::Unknown: return "UNKNOWN";
        case WarningState::Clear: return "OFF";
        case WarningState::Active: return "ON";
    }
    return "UNKNOWN";
}
struct EngineWarningState {
    WarningState mil = WarningState::Unknown, temperature = WarningState::Unknown, voltage = WarningState::Unknown;
    bool showMil = false, showTemperature = false, showVoltage = false;
};

class EngineWarnings {
public:
    EngineWarningState update(const EngineReadings& readings, uint64_t now, bool live) {
        if (!live) { milOn_ = temperatureOn_ = voltageOn_ = false; return {}; }
        const bool milFresh = isFresh(readings.mil, now, config::kMilStaleMs);
        const bool temperatureFresh = isFresh(readings.coolant, now, config::kCoolantStaleMs);
        const bool voltageFresh = isFresh(readings.voltage, now, config::kVoltageStaleMs);
        if (milFresh) milOn_ = (readings.mil.value & 0x80) != 0;
        if (temperatureFresh) {
            if (readings.coolant.value >= config::kTemperatureWarningOnC) temperatureOn_ = true;
            else if (readings.coolant.value <= config::kTemperatureWarningOffC) temperatureOn_ = false;
        }
        if (voltageFresh) {
            if (readings.voltage.value < config::kVoltageWarningOnMv) voltageOn_ = true;
            else if (readings.voltage.value >= config::kVoltageWarningOffMv) voltageOn_ = false;
        }
        // Missing/stale replies are UNKNOWN. During LIVE, retain an existing
        // alert until a valid clear reading; a failed read must not clear it.
        return {milFresh ? (milOn_ ? WarningState::Active : WarningState::Clear) : WarningState::Unknown,
                temperatureFresh ? (temperatureOn_ ? WarningState::Active : WarningState::Clear) : WarningState::Unknown,
                voltageFresh ? (voltageOn_ ? WarningState::Active : WarningState::Clear) : WarningState::Unknown,
                milOn_, temperatureOn_, voltageOn_};
    }
private:
    bool milOn_ = false, temperatureOn_ = false, voltageOn_ = false;
};
