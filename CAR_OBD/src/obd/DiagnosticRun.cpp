#include "DiagnosticRun.h"
#include "Elm327.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <cstddef>

namespace obd {
namespace {
struct Custom { const char* name; const char* request; uint16_t tx, rx; bool parked; };
// Read-only published Hyundai/Tucson candidates. No session changes, writes,
// security access or actuator tests. Bit/enum semantics need car validation.
constexpr Custom kCustom[] = {
    {"transmission_21A0", "21A0", 0x7e1, 0x7e9, false},
    {"actual_gear_21A1", "21A1", 0x7e1, 0x7e9, false},
    {"selector_gear_ATF", "2201A0", 0x7e1, 0x7e9, false},
    {"gear_alternative", "2201A4", 0x7e1, 0x7e9, false},
    {"engine_ATF_candidate", "21A0", 0x7e0, 0x7e8, false},
    {"brake_belts_doors", "22BC03", 0x770, 0x778, true},
    {"reverse_rear_belts", "22BC04", 0x770, 0x778, true},
    {"light_switch", "22BC05", 0x770, 0x778, false},
    {"brake_lights", "22BC06", 0x770, 0x778, false},
    {"turn_indicators", "22BC08", 0x770, 0x778, false},
    {"headlights", "22BC09", 0x770, 0x778, false},
    {"engine_oil_candidate", "2101", 0x7e0, 0x7e8, false},
    {"odometer_fuel_cluster", "22B002", 0x7c6, 0x7ce, false},
    {"engine_odometer", "2103", 0x7e0, 0x7e8, false},
};
static_assert(sizeof(kCustom)/sizeof(kCustom[0]) == DiagnosticRun::kCustomCount, "Candidate count");
// Every second optional slot prioritises the responsive transmission block.
// These IDs retain their original report/catalogue meanings.
constexpr unsigned kFocusedSlots[] = {269, 0x0c, 269, 270, 269, 280, 269, 0x0f};
constexpr unsigned kFocusedItems[] = {269, 270, 280, 0x0c, 0x0f};
int hex(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}
bool exact(const char* response, const char* expected) {
    for (; *response; ++response) {
        if (std::isspace(static_cast<unsigned char>(*response))) continue;
        if (*response != *expected++) return false;
    }
    return !*expected;
}
uint32_t checksum(const DiagnosticRun::Report& report) {
    const auto* bytes = reinterpret_cast<const uint8_t*>(&report);
    uint32_t hash = 2166136261u;
    for (size_t i = 0; i < offsetof(DiagnosticRun::Report, checksum); ++i) hash = (hash ^ bytes[i]) * 16777619u;
    return hash;
}
int expectedLength(unsigned pid) {
    switch (pid) {
        case 1: return 4;
        case 2: case 3: case 0x0c: case 0x10: case 0x1f: case 0x21: case 0x22: case 0x23:
        case 0x31: case 0x32: case 0x3c: case 0x3d: case 0x3e: case 0x3f: case 0x42:
        case 0x43: case 0x44: case 0x4d: case 0x4e: case 0x53: case 0x54: case 0x59:
        case 0x5d: case 0x5e: return 2;
        case 4: case 5: case 6: case 7: case 8: case 9: case 0x0a: case 0x0b: case 0x0d:
        case 0x0e: case 0x0f: case 0x11: case 0x12: case 0x13: case 0x1c: case 0x1d: case 0x1e:
        case 0x2c: case 0x2d: case 0x2e: case 0x2f: case 0x30: case 0x33: case 0x45: case 0x46:
        case 0x47: case 0x48: case 0x49: case 0x4a: case 0x4b: case 0x4c: case 0x51: case 0x52:
        case 0x5a: case 0x5b: case 0x5c: return 1;
        default: return pid >= 0x14 && pid <= 0x1b ? 2 : pid >= 0x24 && pid <= 0x2b ? 4 : 0;
    }
}
bool dtcLayout(const uint8_t* data, size_t length, size_t& offset, size_t& count) {
    offset = 0; count = length/2;
    if (!length) return false;
    if (length % 2) {
        count = data[0]; offset = 1;
        if (1 + count*2 > length) return false;
        for (size_t i = 1 + count*2; i < length; ++i) if (data[i]) return false;
    }
    return true;
}
} // namespace

const char* diagnosticOutcomeName(DiagnosticOutcome result) {
    switch (result) {
        case DiagnosticOutcome::Untested: return "UNTESTED";
        case DiagnosticOutcome::Success: return "SUCCESS";
        case DiagnosticOutcome::NoData: return "NO_DATA";
        case DiagnosticOutcome::Negative: return "ECU_REJECTED";
        case DiagnosticOutcome::Malformed: return "FORMAT_ERROR";
        case DiagnosticOutcome::Conflicting: return "CONFLICTING";
        case DiagnosticOutcome::Timeout: return "TIMEOUT";
        case DiagnosticOutcome::RoutingError: return "ROUTING_ERROR";
        case DiagnosticOutcome::Protocol: return "PROTOCOL_SKIPPED";
        case DiagnosticOutcome::Deferred: return "WAIT_BELOW_10_KMH";
        case DiagnosticOutcome::NotAdvertised: return "NOT_ADVERTISED";
        case DiagnosticOutcome::Cancelled: return "CANCELLED";
        case DiagnosticOutcome::NotSelected: return "NOT_SELECTED";
    }
    return "UNKNOWN";
}

DiagnosticOutcome parseDiagnosticPayload(const char* command, const char* response,
                                        uint8_t* payload, size_t capacity, size_t& length) {
    length = 0;
    if (std::strstr(response, "NO DATA") || std::strstr(response, "UNABLE TO CONNECT")) return DiagnosticOutcome::NoData;
    if (std::strstr(response, "ERROR") || std::strstr(response, "STOPPED") || std::strchr(response, '?') ||
        std::strstr(response, "BUS BUSY") || std::strstr(response, "BUFFER FULL")) return DiagnosticOutcome::Negative;
    uint8_t prefix[3] = {};
    const size_t prefixSize = std::strlen(command) / 2;
    if (!prefixSize || prefixSize > 3 || std::strlen(command) % 2) return DiagnosticOutcome::Malformed;
    for (size_t i = 0; i < prefixSize; ++i) {
        if (hex(command[i*2]) < 0 || hex(command[i*2+1]) < 0) return DiagnosticOutcome::Malformed;
        prefix[i] = static_cast<uint8_t>(hex(command[i*2])*16 + hex(command[i*2+1]));
    }
    const uint8_t service = prefix[0]; prefix[0] += 0x40;
    uint8_t message[132] = {};
    size_t used = 0, declared = 0;
    unsigned frame = 0;
    bool found = false;
    for (auto* first = response; *first;) {
        auto* last = first;
        while (*last && *last != '\r' && *last != '\n' && *last != '>') ++last;
        char line[280]; size_t count = 0;
        for (auto* p = first; p != last; ++p) {
            if (std::isspace(static_cast<unsigned char>(*p))) continue;
            if (count + 1 >= sizeof(line)) return DiagnosticOutcome::Malformed;
            line[count++] = *p;
        }
        line[count] = 0;
        first = *last ? last + 1 : last;
        if (!count || !std::strcmp(line, command) || !std::strcmp(line, "SEARCHING...")) continue;
        if (count >= 6 && !std::strncmp(line, "7F", 2) && hex(line[2])*16+hex(line[3]) == service)
            return DiagnosticOutcome::Negative;
        if (count == 3 && !found && !declared && hex(line[0]) >= 0 && hex(line[1]) >= 0 && hex(line[2]) >= 0) {
            declared = static_cast<size_t>(hex(line[0])*256 + hex(line[1])*16 + hex(line[2]));
            if (declared <= 7 || declared > sizeof(message)) return DiagnosticOutcome::Malformed;
            continue;
        }
        const char* bytes = line;
        size_t byteCount = count / 2;
        size_t dataBytes = byteCount;
        if (declared) {
            if (found || count < 4 || line[1] != ':' || hex(line[0]) != static_cast<int>(frame % 16) || (count - 2) % 2)
                return DiagnosticOutcome::Malformed;
            bytes += 2; byteCount = (count - 2) / 2;
            const size_t frameCapacity = frame ? 7 : 6;
            dataBytes = std::min(frameCapacity, declared - used);
            // CAF1 adapters may retain the final CAN frame's padding. Only a
            // complete final frame may exceed the remaining declared bytes.
            if (byteCount < dataBytes || byteCount > frameCapacity ||
                (byteCount != dataBytes && byteCount != frameCapacity)) return DiagnosticOutcome::Malformed;
            ++frame;
        } else {
            if (count % 2 || byteCount > sizeof(message)) return DiagnosticOutcome::Malformed;
            used = 0;
        }
        for (size_t i = 0; i < byteCount; ++i) {
            const int high = hex(bytes[2*i]), low = hex(bytes[2*i+1]);
            if (high < 0 || low < 0) return DiagnosticOutcome::Malformed;
            if (i < dataBytes) {
                if (used >= sizeof(message)) return DiagnosticOutcome::Malformed;
                message[used++] = static_cast<uint8_t>(high*16 + low);
            }
        }
        if (declared && used != declared) continue;
        if (used < prefixSize || std::memcmp(message, prefix, prefixSize)) return DiagnosticOutcome::Malformed;
        const size_t dataSize = used - prefixSize;
        if (dataSize > capacity) return DiagnosticOutcome::Malformed;
        if (found && (length != dataSize || std::memcmp(payload, message + prefixSize, dataSize))) return DiagnosticOutcome::Conflicting;
        length = dataSize; std::memcpy(payload, message + prefixSize, length); found = true;
    }
    return found ? DiagnosticOutcome::Success : DiagnosticOutcome::Malformed;
}

const char* DiagnosticRun::entryName(unsigned id) {
    if (id >= 269 && id < kEntries) return kCustom[id-269].name;
    if (id >= 256 && id < 264) return "supported_PIDs";
    switch (id) {
        case 1: return "ECU_errors"; case 2: return "freeze_frame_DTC"; case 3: return "fuel_system_status";
        case 4: return "engine_load"; case 5: return "coolant_temperature";
        case 6: return "short_fuel_trim_B1"; case 7: return "long_fuel_trim_B1";
        case 0x0a: return "fuel_pressure"; case 0x0b: return "intake_pressure";
        case 0x0c: return "engine_RPM"; case 0x0d: return "speed"; case 0x0e: return "ignition_timing";
        case 0x0f: return "intake_temperature"; case 0x10: return "air_flow"; case 0x11: return "throttle";
        case 0x13: return "oxygen_sensors_present"; case 0x14: return "oxygen_sensor_1"; case 0x15: return "oxygen_sensor_2";
        case 0x1c: return "OBD_standard"; case 0x1f: return "engine_runtime";
        case 0x21: return "distance_with_MIL"; case 0x23: return "fuel_rail_pressure";
        case 0x2f: return "fuel_level"; case 0x30: return "warmups_since_clear"; case 0x31: return "distance_since_clear";
        case 0x33: return "barometric_pressure"; case 0x3c: return "catalyst_temperature";
        case 0x42: return "ECU_voltage"; case 0x46: return "ambient_temperature";
        case 0x5c: return "oil_temperature"; case 0x5e: return "fuel_rate";
        case 264: return "confirmed_fault_codes"; case 265: return "pending_fault_codes";
        case 266: return "permanent_fault_codes"; case 267: return "VIN"; case 268: return "adapter_voltage";
        default: return "standard_PID";
    }
}
void DiagnosticRun::entryRequest(unsigned id, char* out, size_t capacity) {
    if (id < 256) std::snprintf(out, capacity, "01%02X", id);
    else if (id < 264) std::snprintf(out, capacity, "01%02X", (id-256)*32);
    else if (id >= 269 && id < kEntries) std::snprintf(out, capacity, "%s", kCustom[id-269].request);
    else {
        const char* commands[] = {"03", "07", "0A", "0902", "ATRV"};
        std::snprintf(out, capacity, "%s", id < 269 ? commands[id-264] : "?");
    }
}
const char* DiagnosticRun::name() const { return entryName(item_); }
unsigned DiagnosticRun::target() const { return candidate() ? kCustom[item_-269].tx : 0x7df; }

void DiagnosticRun::startSession() {
    const auto session = report_.session + 1;
    std::memset(static_cast<void*>(&report_), 0, sizeof(report_));
    report_.magic = 0x44494147; report_.version = 1; report_.session = session;
    status_ = {};
    status_.enabled = enabled_;
    status_.candidateFocus = candidateFocus_;
    if (candidateFocus_) {
        report_.records[0].sample[0] = 1;
        for (unsigned i = 1; i < kEntries; ++i) report_.records[i].outcome = DiagnosticOutcome::NotSelected;
        report_.records[0x0d].outcome = DiagnosticOutcome::Untested;
        for (auto id : kFocusedItems) report_.records[id].outcome = DiagnosticOutcome::Untested;
    }
    focusedSlot_ = 0; lastTcmMs_ = lastOilMs_ = 0;
    referenceRpm_ = {}; referenceIntake_ = {};
    for (auto& when : nextAt_) when = 0;
    phase_ = Phase::Capabilities; bank_ = 0; standard_ = 1; extra_ = 264; custom_ = 0;
    stage_ = Stage::Idle; started_ = true; dirty_ = true;
}
void DiagnosticRun::setEnabled(bool enabled, uint64_t now) {
    if (enabled != enabled_) { haveSpeed_ = false; if (started_) dirty_ = true; lastTick_ = now; }
    enabled_ = enabled; status_.enabled = enabled;
    if (started_) refreshStatus();
}
void DiagnosticRun::setCandidateFocus(bool enabled) {
    if (started_ || active()) return;
    candidateFocus_ = enabled; status_.candidateFocus = enabled;
    if (enabled) {
        status_.tested = status_.succeeded = status_.failed = status_.deferred = 0;
        status_.samples = 0; status_.complete = false;
    }
}
void DiagnosticRun::connected(uint64_t now) {
    stage_ = Stage::Idle; protocolChecked_ = can11_ = false;
    haveSpeed_ = false; nextProbe_ = now;
    lastTcmMs_ = lastOilMs_ = 0;
    referenceRpm_ = {}; referenceIntake_ = {};
}
void DiagnosticRun::tick(uint64_t now, bool connectedNow) {
    if (enabled_ && started_ && connectedNow && lastTick_ && now >= lastTick_) {
        report_.activeMs += static_cast<uint32_t>(now - lastTick_); dirty_ = true;
    }
    lastTick_ = now; status_.connected = connectedNow;
    status_.seconds = report_.activeMs / 1000;
    status_.tripMeters = static_cast<uint32_t>(report_.distanceTwiceKmhMs / 7200);
    if (candidateFocus_ && !started_) status_.seconds = status_.tripMeters = 0;
    status_.gearValid = connectedNow && started_ && lastTcmMs_ && now >= lastTcmMs_ && now-lastTcmMs_ < 5000;
    status_.atfValid = status_.gearValid;
    status_.oilValid = connectedNow && started_ && lastOilMs_ && now >= lastOilMs_ && now-lastOilMs_ < 20000;
}
void DiagnosticRun::observeSpeed(int kmh, uint64_t now) {
    if (!enabled_) { haveSpeed_ = false; return; }
    if (!started_) startSession(); // Do not overwrite a saved car run just by viewing page three at the PC.
    if (haveSpeed_ && now >= lastSpeed_) {
        const auto delta = now - lastSpeed_;
        if (delta < config::kLiveStaleMs) {
            report_.distanceTwiceKmhMs += static_cast<uint64_t>(previousSpeed_ + kmh) * delta;
            report_.speedMs += static_cast<uint32_t>(delta);
        } else ++report_.speedGaps;
    }
    report_.maxSpeed = std::max(report_.maxSpeed, static_cast<uint16_t>(kmh));
    lastSpeed_ = now; previousSpeed_ = kmh; haveSpeed_ = true; dirty_ = true;
    auto& speed = report_.records[0x0d];
    if (!speed.successes) { speed.successes = 1; speed.outcome = DiagnosticOutcome::Success; speed.length = 1; refreshStatus(); }
    speed.sample[0] = static_cast<uint8_t>(kmh);
}

bool DiagnosticRun::choose(uint64_t now, int kmh) {
    if (candidateFocus_) {
        for (unsigned i = 0; i < sizeof(kFocusedSlots)/sizeof(kFocusedSlots[0]); ++i) {
            const auto id = kFocusedSlots[focusedSlot_];
            focusedSlot_ = (focusedSlot_ + 1) % (sizeof(kFocusedSlots)/sizeof(kFocusedSlots[0]));
            if ((report_.records[id].flags & 1) || now < nextAt_[id]) continue;
            item_ = id; return true;
        }
        return false;
    }
    for (unsigned tries = 0; tries < kEntries + kCustomCount; ++tries) {
        if (phase_ == Phase::Capabilities) { item_ = 256 + bank_; return true; }
        if (phase_ == Phase::Standard) {
            while (standard_ < 256) {
                const auto id = standard_++;
                if (id % 32 == 0) continue;
                const unsigned bank = id / 32, bit = 31 - (id - bank*32 - 1);
                if (!(report_.knownBanks & (1u << bank))) continue;
                const auto* mask = report_.support + bank*4;
                const uint32_t bits = (uint32_t(mask[0])<<24)|(uint32_t(mask[1])<<16)|(uint32_t(mask[2])<<8)|mask[3];
                if (!(bits & (1u << bit))) { report_.records[id].outcome = DiagnosticOutcome::NotAdvertised; continue; }
                if (id == 0x0d && report_.records[id].successes) continue;
                item_ = id; return true;
            }
            phase_ = Phase::Extra;
        }
        if (phase_ == Phase::Extra) {
            if (extra_ < 269) { item_ = extra_++; return true; }
            phase_ = Phase::Custom;
        }
        if (phase_ == Phase::Custom) {
            if (custom_ < kCustomCount) {
                item_ = 269 + custom_++;
                if (kCustom[item_-269].parked && kmh >= config::kBodyProbeMaxSpeedKmh) {
                    report_.records[item_].outcome = DiagnosticOutcome::Deferred; continue;
                }
                return true;
            }
            phase_ = Phase::Repeat; report_.sweepComplete = true; dirty_ = true; refreshStatus();
        }
        if (phase_ == Phase::Repeat) {
            // Share repeat slots so successful custom probes cannot starve standard data.
            unsigned selected = kEntries;
            for (unsigned pass = 0; pass < 2 && selected == kEntries; ++pass) {
                const bool standard = pass == 0 ? preferStandard_ : !preferStandard_;
                for (unsigned id = standard ? 1 : 269; id < (standard ? 269 : kEntries); ++id) {
                    const auto& r = report_.records[id];
                    if (r.flags & 1 || now < nextAt_[id]) continue;
                    if (standard && (id == 0x0d || (id >= 256 && id < 264) ||
                        r.outcome == DiagnosticOutcome::NotAdvertised || r.outcome == DiagnosticOutcome::Untested)) continue;
                    if (!standard && kCustom[id-269].parked && kmh >= config::kBodyProbeMaxSpeedKmh) continue;
                    if (selected == kEntries || nextAt_[id] < nextAt_[selected]) selected = id;
                }
            }
            if (selected == kEntries) return false;
            preferStandard_ = selected >= 269;
            item_ = selected; return true;
        }
    }
    return false;
}

const char* DiagnosticRun::nextCommand(uint64_t now, int kmh) {
    if (!enabled_ && stage_ == Stage::Read) {
        record(DiagnosticOutcome::Cancelled, now);
        stage_ = candidate() ? Stage::RestoreHeader : Stage::Idle;
    }
    if (!enabled_ && (stage_ == Stage::Header || stage_ == Stage::Filter)) stage_ = Stage::RestoreHeader;
    if (stage_ == Stage::Idle) {
        if (!enabled_ || !started_ || now < nextProbe_ || !choose(now, kmh)) return nullptr;
        entryRequest(item_, request_, sizeof(request_));
        std::snprintf(status_.item, sizeof(status_.item), "%s", name());
        if (candidate()) {
            if (protocolChecked_ && !can11_) {
                record(DiagnosticOutcome::Protocol, now); return nullptr;
            }
            stage_ = protocolChecked_ ? Stage::Header : Stage::Protocol;
        } else stage_ = Stage::Read;
        budget_ = now + 1200;
    }
    switch (stage_) {
        case Stage::Protocol: std::snprintf(command_, sizeof(command_), "ATDPN\r"); break;
        case Stage::Header: std::snprintf(command_, sizeof(command_), "ATSH%03X\r", kCustom[item_-269].tx); break;
        case Stage::Filter: std::snprintf(command_, sizeof(command_), "ATCRA%03X\r", kCustom[item_-269].rx); break;
        case Stage::Read: std::snprintf(command_, sizeof(command_), "%s\r", request_); break;
        case Stage::RestoreHeader: std::snprintf(command_, sizeof(command_), "ATSH7DF\r"); break;
        case Stage::RestoreFilter: std::snprintf(command_, sizeof(command_), "ATCRA\r"); break;
        case Stage::Idle: return nullptr;
    }
    deadline_ = std::min(now + (stage_ == Stage::Read ? 1000 : 600), budget_);
    return command_;
}

void DiagnosticRun::record(DiagnosticOutcome result, uint64_t now) {
    auto& r = report_.records[item_];
    r.outcome = result;
    if (result == DiagnosticOutcome::Success) {
        if (candidate()) {
            const auto index = item_-269;
            if (!r.successes) std::memcpy(report_.customFirst[index], payload_, std::min(length_, size_t{64}));
            else for (size_t i = 0; i < std::min(length_, size_t{64}); ++i)
                report_.customChanged[index][i] |= report_.customSamples[index][i] ^ payload_[i];
        }
        if (r.successes < UINT16_MAX) ++r.successes;
        r.length = static_cast<uint8_t>(length_);
        std::memcpy(r.sample, payload_, std::min(length_, sizeof(r.sample)));
        if (candidate()) std::memcpy(report_.customSamples[item_-269], payload_, std::min(length_, size_t{64}));
        else if (item_ >= 264 && item_ < 269) std::memcpy(report_.extraSamples[item_-264], payload_, std::min(length_, size_t{64}));
    } else if (result != DiagnosticOutcome::Cancelled && result != DiagnosticOutcome::Deferred) {
        if (r.failures < UINT16_MAX) ++r.failures;
    }
    if (result == DiagnosticOutcome::Timeout || result == DiagnosticOutcome::RoutingError) r.flags |= 1;
    nextAt_[item_] = static_cast<uint32_t>(now + (result == DiagnosticOutcome::Success ?
        (candidateFocus_ ? (item_ == 269 ? 500 : 1000) : candidate() ? 3000 : 10000) : 60000));
    nextProbe_ = now + (candidateFocus_ ? 0 : 1000);
    dirty_ = true;
    std::snprintf(status_.item, sizeof(status_.item), "%s", name());
    std::snprintf(status_.outcome, sizeof(status_.outcome), "%s", diagnosticOutcomeName(result));
    refreshStatus();
    describe();
}

DiagnosticRun::Event DiagnosticRun::receive(const char* response, uint64_t now) {
    deadline_ = budget_; // Between commands only the total transaction budget applies.
    if (stage_ == Stage::Protocol) {
        protocolChecked_ = true; can11_ = exact(response, "6") || exact(response, "A6");
        if (!can11_) { record(DiagnosticOutcome::Protocol, now); stage_ = Stage::Idle; return Event::Result; }
        stage_ = enabled_ ? Stage::Header : Stage::Idle;
        return Event::None;
    }
    if (stage_ == Stage::Header || stage_ == Stage::Filter) {
        if (!exact(response, "OK")) {
            record(DiagnosticOutcome::RoutingError, now); stage_ = Stage::RestoreHeader; return Event::Result;
        }
        stage_ = !enabled_ ? Stage::RestoreHeader : stage_ == Stage::Header ? Stage::Filter : Stage::Read;
        return Event::None;
    }
    if (stage_ == Stage::RestoreHeader || stage_ == Stage::RestoreFilter) {
        if (!exact(response, "OK")) {
            record(DiagnosticOutcome::RoutingError, now); stage_ = Stage::Idle; return Event::Fatal;
        }
        stage_ = stage_ == Stage::RestoreHeader ? Stage::RestoreFilter : Stage::Idle;
        return Event::None;
    }
    if (stage_ != Stage::Read) return Event::None;
    length_ = 0;
    DiagnosticOutcome result;
    if (item_ == 268) {
        int mv = 0;
        const auto parsed = parseVoltage(response, mv);
        result = parsed == Parse::Valid ? DiagnosticOutcome::Success : parsed == Parse::Conflicting ? DiagnosticOutcome::Conflicting : DiagnosticOutcome::NoData;
        payload_[0] = static_cast<uint8_t>(mv >> 8); payload_[1] = static_cast<uint8_t>(mv); length_ = 2;
    } else if (item_ == 1) {
        unsigned lines = 0; uint8_t value = 0;
        const auto parsed = parseMilStatus(response, value, lines);
        result = parsed == Parse::Valid ? DiagnosticOutcome::Success : parsed == Parse::Conflicting ? DiagnosticOutcome::Conflicting :
                 parsed == Parse::AdapterError ? DiagnosticOutcome::NoData : DiagnosticOutcome::Malformed;
        payload_[0] = value; length_ = 4; payload_[1] = payload_[2] = payload_[3] = 0;
    } else if (item_ >= 256 && item_ < 264) {
        uint8_t bitmap[4] = {};
        const auto parsed = parsePid(response, static_cast<uint8_t>(bank_*32), bitmap, 4);
        result = parsed == Parse::Valid ? DiagnosticOutcome::Success : DiagnosticOutcome::NoData;
        length_ = 4; std::memcpy(payload_, bitmap, 4);
    } else result = parseDiagnosticPayload(request_, response, payload_, sizeof(payload_), length_);
    if (result == DiagnosticOutcome::Success && item_ < 256 && expectedLength(item_) &&
        length_ != static_cast<size_t>(expectedLength(item_))) result = DiagnosticOutcome::Malformed;
    if (result == DiagnosticOutcome::Success) {
        if (candidate() && !length_) result = DiagnosticOutcome::Malformed;
        if (item_ >= 264 && item_ <= 266) {
            size_t offset, count;
            if (!dtcLayout(payload_, length_, offset, count)) result = DiagnosticOutcome::Malformed;
        }
        if (item_ == 267) {
            if (length_ != 18 || payload_[0] != 1) result = DiagnosticOutcome::Malformed;
            else for (size_t i = 1; i < length_; ++i)
                if (!std::isalnum(payload_[i])) result = DiagnosticOutcome::Malformed;
        }
    }
    record(result, now);
    if (result == DiagnosticOutcome::Success) {
        if (item_ == 0x0c) referenceRpm_ = {(int(payload_[0])*256 + payload_[1])/4, now, true};
        if (item_ == 0x0f) referenceIntake_ = {int(payload_[0])-40, now, true};
        if (item_ == 269) {
            lastTcmMs_ = length_ > 22 ? now : 0;
            if (lastTcmMs_) {
                status_.gear = payload_[21]; status_.commandedGear = payload_[22]; status_.atfC = int(payload_[13])-40;
                if (candidateFocus_) report_.support[payload_[21]/8] |= 1u << (payload_[21]%8);
            }
        }
        if (item_ == 280) {
            lastOilMs_ = length_ > 34 ? now : 0;
            if (lastOilMs_) status_.oilQuarterC = int(payload_[34])*3 - 192;
        }
    }
    if (item_ >= 256 && item_ < 264) {
        if (result == DiagnosticOutcome::Success) {
            report_.knownBanks |= 1u << bank_;
            std::memcpy(report_.support + bank_*4, payload_, 4);
        }
        if (result == DiagnosticOutcome::Success && payload_[3] & 1 && bank_ < 7) ++bank_;
        else { phase_ = Phase::Standard; standard_ = 1; }
    }
    stage_ = candidate() ? Stage::RestoreHeader : Stage::Idle;
    return Event::Result;
}
bool DiagnosticRun::timedOut(uint64_t now) {
    if (!active() || now < deadline_) return false;
    record(DiagnosticOutcome::Timeout, now);
    std::snprintf(description_, sizeof(description_), "missing/late prompt; item quarantined until reboot");
    stage_ = Stage::Idle;
    if (phase_ == Phase::Capabilities) { phase_ = Phase::Standard; standard_ = 1; }
    return true;
}
void DiagnosticRun::abort(DiagnosticOutcome result, uint64_t now) {
    if (!active()) return;
    record(result, now);
    if (result != DiagnosticOutcome::Cancelled) report_.records[item_].flags |= 1;
    stage_ = Stage::Idle;
    if (phase_ == Phase::Capabilities) { phase_ = Phase::Standard; standard_ = 1; }
}

void DiagnosticRun::describe() {
    if (outcome() != DiagnosticOutcome::Success) {
        std::snprintf(description_, sizeof(description_), "%s; not proof of permanent unavailability", diagnosticOutcomeName(outcome())); return;
    }
    const auto a = length_ ? payload_[0] : 0;
    const unsigned ab = length_ >= 2 ? unsigned(a)*256 + payload_[1] : a;
    if (item_ == 1) std::snprintf(description_, sizeof(description_), "MIL %s; max_reported_DTC_count=%u", a & 0x80 ? "ON" : "OFF", a & 0x7f);
    else if (item_ == 0x0c) std::snprintf(description_, sizeof(description_), "%.0f rpm", ab/4.0);
    else if (item_ == 5 || item_ == 0x0f || item_ == 0x46 || item_ == 0x5c) std::snprintf(description_, sizeof(description_), "%d C", int(a)-40);
    else if (item_ == 6 || item_ == 7) std::snprintf(description_, sizeof(description_), "%.2f percent", (int(a)-128)*100.0/128);
    else if (item_ == 4 || item_ == 0x11 || item_ == 0x2f) std::snprintf(description_, sizeof(description_), "%.1f percent", a*100.0/255);
    else if (item_ == 0x0b || item_ == 0x33) std::snprintf(description_, sizeof(description_), "%u kPa absolute", a);
    else if (item_ == 0x14 || item_ == 0x15) std::snprintf(description_, sizeof(description_), "%.3f V", a*0.005);
    else if (item_ == 0x1f) std::snprintf(description_, sizeof(description_), "%u s since engine start", ab);
    else if (item_ == 0x21 || item_ == 0x31) std::snprintf(description_, sizeof(description_), "%u km (not dashboard Trip A/B)", ab);
    else if (item_ == 268 || item_ == 0x42) std::snprintf(description_, sizeof(description_), "%.3f V", ab/1000.0);
    else if (item_ >= 264 && item_ <= 266) {
        size_t offset = 0, count = 0;
        const bool valid = dtcLayout(payload_, length_, offset, count);
        unsigned actual = 0;
        if (valid) for (size_t i = 0; i < count; ++i) if (payload_[offset+2*i] || payload_[offset+2*i+1]) ++actual;
        if (valid) {
            std::snprintf(description_, sizeof(description_), "reported_codes=%u%s", actual, actual ? "" : "; no codes reported");
            for (size_t i = 0; i < count && std::strlen(description_) < 100; ++i) {
                const auto high = payload_[offset+2*i], low = payload_[offset+2*i+1];
                if (!high && !low) continue;
                const auto used = std::strlen(description_);
                std::snprintf(description_ + used, sizeof(description_) - used, " %c%X%X%02X", "PCBU"[high >> 6], (high >> 4) & 3, high & 15, low);
            }
        }
        else std::snprintf(description_, sizeof(description_), "response received; DTC framing unverified");
    } else if (item_ == 267) std::snprintf(description_, sizeof(description_), "VIN %.17s", reinterpret_cast<const char*>(payload_+1));
    else std::snprintf(description_, sizeof(description_), "%u data bytes; %s", static_cast<unsigned>(length_), candidate() ? "Hyundai mapping UNVERIFIED" : "raw decoding retained");
}
void DiagnosticRun::refreshStatus() {
    status_.tested = status_.succeeded = status_.failed = status_.deferred = 0;
    status_.samples = 0;
    if (candidateFocus_ && started_) {
        bool complete = true;
        for (auto id : kFocusedItems) if (!report_.records[id].successes && !report_.records[id].failures) complete = false;
        report_.sweepComplete = complete;
    }
    for (unsigned id = 0; id < kEntries; ++id) {
        const auto& r = report_.records[id];
        if (id == 269 || id == 270 || id == 280) status_.samples += r.successes;
        if (candidateFocus_ && id != 269 && id != 270 && id != 280) continue;
        if (r.successes || r.failures) ++status_.tested;
        if (r.successes) ++status_.succeeded;
        else if (r.failures) ++status_.failed;
        if (r.outcome == DiagnosticOutcome::Deferred) ++status_.deferred;
    }
    status_.complete = report_.sweepComplete;
}
void DiagnosticRun::prepareSave() { report_.checksum = checksum(report_); }
bool DiagnosticRun::acceptStoredReport() {
    if (!validReport(report_)) {
        std::memset(static_cast<void*>(&report_), 0, sizeof(report_));
        report_.magic = 0x44494147; report_.version = 1; return false;
    }
    refreshStatus(); return true;
}
bool DiagnosticRun::validReport(const Report& report) {
    return report.magic == 0x44494147 && report.version == 1 && report.checksum == checksum(report);
}
} // namespace obd
