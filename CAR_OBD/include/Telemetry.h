#pragma once

#include <cstdint>
#include "Config.h"

struct SpeedReading { int kmh = 0; uint64_t receivedMs = 0; bool valid = false; };
inline bool isLive(const SpeedReading& reading, uint64_t now) {
    return reading.valid && now >= reading.receivedMs && now - reading.receivedMs < config::kLiveStaleMs;
}

struct MetricReading { int value = 0; uint64_t receivedMs = 0; bool valid = false; };
enum class ScreenPage : unsigned { Speed, Driving, EngineData, Diagnostics, Count };
struct TransmissionReading { int selector = 0, gear = 0; uint64_t receivedMs = 0; bool valid = false; };
struct DrivingReadings { MetricReading rpm, load, throttle, coolant, voltage; TransmissionReading transmission; };
// Voltage and lambda use millivolts; coolant uses degrees Celsius.
enum class Metric : unsigned { Voltage, Coolant, Lambda1, Lambda2, Count };
struct DetailReadings { MetricReading values[static_cast<unsigned>(Metric::Count)] = {}; };
// MIL value is aggregated PID 01 byte A: bit 7 commanded lamp; low seven bits
// maximum reported DTC count, not a count of unique fault codes across ECUs.
struct EngineReadings { MetricReading mil{}; MetricReading coolant{}; MetricReading voltage{}; };
struct DiagnosticStatus {
    bool enabled = false, connected = false, complete = false;
    bool candidateFocus = false, gearValid = false, atfValid = false, oilValid = false;
    int gear = 0, commandedGear = 0, atfC = 0, oilQuarterC = 0;
    uint32_t samples = 0;
    uint16_t tested = 0, succeeded = 0, failed = 0, deferred = 0;
    uint32_t seconds = 0, tripMeters = 0;
    char item[32] = {}, outcome[24] = {};
};
inline bool isFresh(const MetricReading& reading, uint64_t now, uint64_t staleMs = config::kDetailStaleMs) {
    return reading.valid && now >= reading.receivedMs && now - reading.receivedMs < staleMs;
}
