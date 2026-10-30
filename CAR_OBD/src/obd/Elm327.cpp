#include "Elm327.h"

#include <cctype>
#include <cstring>
#include <algorithm>
#include "Config.h"

namespace obd {
namespace {
constexpr const char* kCommands[] = {"ATZ\r", "ATE0\r", "ATL0\r", "ATS0\r", "ATH0\r", "ATSP0\r", "0100\r", "010D\r"};
constexpr const char* kDetailCommands[] = {"ATRV\r", "0105\r", "0114\r", "0115\r"};
constexpr uint8_t kDetailPids[] = {0, 0x05, 0x14, 0x15};
constexpr const char* kMonitorCommands[] = {"0101\r", "0105\r", "ATRV\r"};
constexpr int kMonitorPeriods[] = {config::kMilPollMs, config::kCoolantPollMs, config::kVoltagePollMs};
bool errorText(const char* s) {
    constexpr const char* errors[] = {"NO DATA", "UNABLE TO CONNECT", "STOPPED", "ERROR", "BUS BUSY", "CAN ERROR", "BUFFER FULL", "?"};
    for (const auto* error : errors) if (std::strstr(s, error)) return true;
    return false;
}
int hex(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}
}  // namespace

static Parse parsePidFrames(const char* response, uint8_t pid, uint8_t* payload, size_t payloadSize,
                            bool aggregateMil = false, unsigned* responseLines = nullptr) {
    if (payloadSize > 4) return Parse::Malformed;
    if (errorText(response)) return Parse::AdapterError;
    bool found = false;
    bool malformed = false;
    for (const char* start = response; *start;) {
        const char* end = start;
        while (*end && *end != '\r' && *end != '\n' && *end != '>') ++end;
        char digits[40] = {};
        size_t count = 0;
        bool hexOnly = true;
        for (auto* p = start; p != end; ++p) {
            if (std::isspace(static_cast<unsigned char>(*p))) continue;
            if (hex(*p) < 0 || count >= sizeof(digits) - 1) { hexOnly = false; break; }
            digits[count++] = *p;
        }
        // Only whole, headerless hex lines are candidates. Never search for
        // "410D" embedded in arbitrary text, a header or the wrong PID.
        if (count >= 4 && hex(digits[0]) == 4 && hex(digits[1]) == 1 &&
            (hex(digits[2]) * 16 + hex(digits[3])) == pid) {
            if (!hexOnly || count != (payloadSize + 2) * 2) {
                malformed = true;
            } else {
                uint8_t value[4] = {};
                for (size_t i = 0; i < payloadSize; ++i)
                    value[i] = static_cast<uint8_t>(hex(digits[4 + i * 2]) * 16 + hex(digits[5 + i * 2]));
                if (found && aggregateMil) {
                    payload[0] = static_cast<uint8_t>(((payload[0] | value[0]) & 0x80) |
                        std::max(payload[0] & 0x7f, value[0] & 0x7f));
                } else if (found && pid % 32 == 0 && payloadSize == 4) {
                    // Capability maps describe each responding controller, so
                    // different 0100 replies are expected. Discover the union
                    // of available PIDs instead of requiring identical maps.
                    for (size_t i = 0; i < payloadSize; ++i) payload[i] |= value[i];
                } else {
                    // Measurements (including speed) must still agree. Never
                    // combine numeric values or silently pick an arbitrary ECU.
                    if (found && std::memcmp(payload, value, payloadSize)) return Parse::Conflicting;
                    std::memcpy(payload, value, payloadSize);
                }
                if (responseLines) ++*responseLines;
                found = true;
            }
        }
        start = *end ? end + 1 : end;
    }
    if (malformed) return Parse::Malformed;
    return found ? Parse::Valid : Parse::Missing;
}

Parse parsePid(const char* response, uint8_t pid, uint8_t* payload, size_t payloadSize) {
    return parsePidFrames(response, pid, payload, payloadSize);
}
Parse parseMilStatus(const char* response, uint8_t& status, unsigned& responseLines) {
    uint8_t data[4] = {};
    responseLines = 0;
    const auto result = parsePidFrames(response, 0x01, data, 4, true, &responseLines);
    if (result == Parse::Valid) status = data[0];
    return result;
}

const char* parseName(Parse result) {
    switch (result) {
    case Parse::Valid: return "valid";
    case Parse::Missing: return "expected PID missing / non-hex response";
    case Parse::Malformed: return "wrong response length";
    case Parse::AdapterError: return "adapter/ECU error response";
    case Parse::Conflicting: return "conflicting ECU responses";
    }
    return "unknown";
}

Parse parseVoltage(const char* response, int& millivolts) {
    if (errorText(response)) return Parse::AdapterError;
    bool found = false;
    for (const char* start = response; *start;) {
        const char* end = start;
        while (*end && *end != '\r' && *end != '\n' && *end != '>') ++end;
        const char* p = start;
        while (p < end && std::isspace(static_cast<unsigned char>(*p))) ++p;
        if (p < end && std::isdigit(static_cast<unsigned char>(*p))) {
            int whole = 0, fraction = 0, places = 0, digits = 0;
            while (p < end && std::isdigit(static_cast<unsigned char>(*p))) {
                if (++digits > 2) return Parse::Malformed;
                whole = whole * 10 + (*p++ - '0');
            }
            if (p == end || *p++ != '.') return Parse::Malformed;
            while (p < end && std::isdigit(static_cast<unsigned char>(*p))) {
                if (++places > 3) return Parse::Malformed;
                fraction = fraction * 10 + (*p++ - '0');
            }
            if (!places) return Parse::Malformed;
            while (p < end && std::isspace(static_cast<unsigned char>(*p))) ++p;
            if (p == end || *p++ != 'V') return Parse::Malformed;
            while (p < end && std::isspace(static_cast<unsigned char>(*p))) ++p;
            if (p != end) return Parse::Malformed;
            while (places++ < 3) fraction *= 10;
            const int value = whole * 1000 + fraction;
            if (found && millivolts != value) return Parse::Conflicting;
            millivolts = value; found = true;
        }
        start = *end ? end + 1 : end;
    }
    return found ? Parse::Valid : Parse::Missing;
}

void Elm327::reset(uint64_t now) {
    step_ = 0; used_ = 0; response_[0] = 0;
    waiting_ = overflow_ = false; alive_ = true; due_ = now;
    reason_ = "initializing";
    metric_ = -1; nextMetric_ = 0; detailTurn_ = false;
    body_.reset(now); speedReady_ = false;
    diagnostics_.connected(now); diagnosticTransaction_ = false;
    transmission_.reset(now); drivingMetric_ = -1; nextDriving_ = 0;
    for(auto& when:drivingAt_)when=0;
    monitor_ = -1; monitorStarted_ = false; milResponseLines_ = 0;
    for (auto& when : monitorAt_) when = 0;
    for (auto& retry : retryMetricAt_) retry = 0;
}

const char* Elm327::nextCommand(uint64_t now) {
    if (!alive_ || waiting_ || now < due_) return nullptr;
    used_ = 0; response_[0] = 0; overflow_ = false; waiting_ = true;
    metric_ = monitor_ = drivingMetric_ = -1;
    diagnosticTransaction_ = false;
    if (diagnostics_.active()) {
        const char* command = diagnostics_.nextCommand(now, speed_);
        if (command) { diagnosticTransaction_ = true; return command; }
    }
    if (transmission_.active()) {
        drivingMetric_=3;deadline_=std::min(now+600,transmissionDeadline_);return transmission_.command();
    }
    if (step_ == 7 && monitorEnabled_ && monitorStarted_ && speedReady_ && !body_.active()) {
        int first = 0;
        for (int candidate = 1; candidate < 3; ++candidate)
            if (monitorAt_[candidate] < monitorAt_[first]) first = candidate;
        if (now >= monitorAt_[first]) monitor_ = first;
        if (monitor_ >= 0) {
            monitorAt_[monitor_] = now + kMonitorPeriods[monitor_];
            speedReady_ = false;
            deadline_ = now + config::kEngineMonitorTimeoutMs;
            return kMonitorCommands[monitor_];
        }
    }
    if (step_ == 7 && speedReady_ && !body_.active()) {
        if (const char* command = diagnostics_.nextCommand(now, speed_)) {
            diagnosticTransaction_ = true; return command;
        }
    }
    if(step_==7 && speedReady_ && drivingActive_ && !body_.active()) {
        for(unsigned attempt=0;attempt<4;++attempt) {
            const auto id=nextDriving_;nextDriving_=(nextDriving_+1)%4;
            if(id==3) {
                if(!transmission_.due(now))continue;
                transmission_.start();transmissionDeadline_=now+1200;drivingMetric_=3;
                deadline_=std::min(now+600,transmissionDeadline_);speedReady_=false;
                return transmission_.command();
            }
            if(now<drivingAt_[id])continue;
            constexpr const char* requests[]={"010C\r","0104\r","0111\r"};
            drivingMetric_=static_cast<int>(id);speedReady_=false;deadline_=now+1000;
            return requests[id];
        }
    }
    if (step_ == 7 && bodyEnabled_ && speedReady_ && speed_ < config::kBodyProbeMaxSpeedKmh &&
        !body_.active() && body_.due(now)) {
        body_.start();
        bodyDeadline_ = now + config::kBodyProbeBudgetMs;
    }
    if (body_.active()) {
        // A routing transaction is indivisible: restore 7DF and the receive
        // filter before sending another speed/detail command.
        deadline_ = std::min(now + config::kBodyProbeCommandTimeoutMs, bodyDeadline_);
        return body_.command();
    }
    if (step_ == 7 && detailsActive_ && detailTurn_) {
        for (int attempt = 0; attempt < 4; ++attempt) {
            const int candidate = nextMetric_;
            nextMetric_ = (nextMetric_ + 1) % 4;
            if (monitorEnabled_ && candidate <= 1) continue; // Background monitors own voltage and coolant on every page.
            if (now >= retryMetricAt_[candidate]) { metric_ = candidate; break; }
        }
    }
    if (step_ == 7) detailTurn_ = metric_ < 0;
    if (metric_ >= 0) speedReady_ = false; // Never stack a body probe behind a detail poll.
    deadline_ = now + (step_ == 6 ? 30'000 : step_ == 0 ? 5'000 : 3'000);
    return metric_ >= 0 ? kDetailCommands[metric_] : kCommands[step_];
}

bool Elm327::timedOut(uint64_t now) {
    if(transmission_.active() && (now>=transmissionDeadline_ || (waiting_ && now>=deadline_))) {
        transmission_.timeout();waiting_=false;alive_=false;reason_="gear routing timeout; reconnect to restore default route";return true;
    }
    if (diagnosticTransaction_) {
        if (!diagnostics_.timedOut(now)) return false;
        waiting_ = false; alive_ = false;
        reason_ = "diagnostic request timed out; reconnect to restore routing";
        return true;
    }
    const bool bodyOverBudget = body_.active() && now >= bodyDeadline_;
    if (!bodyOverBudget && (!waiting_ || now < deadline_)) return false;
    if (body_.active()) { bodySuppressed_ = true; bodyEnabled_ = false; }
    waiting_ = false; alive_ = false;
    reason_ = bodyOverBudget ? "body probe exceeded total time budget" : "missing prompt / response timeout";
    return true;
}

Elm327::Result Elm327::receive(char byte, uint64_t now) {
    if (!waiting_) return Result::None;
    if(drivingMetric_>=0 && now>=deadline_) { timedOut(now);return Result::DrivingError; }
    if (diagnosticTransaction_ && diagnostics_.timedOut(now)) {
        waiting_ = false; alive_ = false;
        reason_ = "late diagnostic reply; reconnect to restore routing";
        return Result::Diagnostic;
    }
    if ((body_.active() || monitor_ >= 0) && now >= deadline_) {
        timedOut(now);
        return monitor_ >= 0 ? Result::MonitorError : Result::BodyError;
    }
    if (byte != '>') {
        if (used_ + 1 < sizeof(response_) && byte != '\0') {
            response_[used_++] = byte;
            response_[used_] = 0;
        } else overflow_ = true;
        return Result::None;
    }
    waiting_ = false;
    if (overflow_) {
        if(transmission_.active())transmission_.timeout();
        if (diagnosticTransaction_) {
            diagnostics_.abort(DiagnosticOutcome::Malformed, now);
            alive_ = false; reason_ = "diagnostic response overflow / embedded NUL";
            return Result::Diagnostic;
        }
        if (body_.active()) { bodySuppressed_ = true; bodyEnabled_ = false; }
        alive_ = false; reason_ = "response overflow / embedded NUL";
        return Result::Error;
    }
    if(transmission_.active()) {
        const auto event=transmission_.receive(response_,now);due_=now+20;
        reason_=transmission_.reason();
        if(!transmission_.active()){speedReady_=false;detailTurn_=false;}
        if(event==TransmissionPoll::Result::Fatal){alive_=false;reason_="gear route restoration failed";}
        if(event==TransmissionPoll::Result::Sample)return Result::Driving;
        if(event==TransmissionPoll::Result::Error || event==TransmissionPoll::Result::Fatal)return Result::DrivingError;
        return Result::None;
    }
    if (diagnosticTransaction_) {
        const auto event = diagnostics_.receive(response_, now);
        due_ = now + 20;
        if (!diagnostics_.active()) { speedReady_ = false; detailTurn_ = false; }
        if (event == DiagnosticRun::Event::Fatal) { alive_ = false; reason_ = "diagnostic routing restoration failed"; }
        return event == DiagnosticRun::Event::None ? Result::None : Result::Diagnostic;
    }
    if (body_.active()) {
        const auto result = body_.receive(response_, now);
        reason_ = body_.reason();
        due_ = now + 20;
        if (!body_.active()) { speedReady_ = false; detailTurn_ = false; } // Speed next, before another optional request.
        if (result == HyundaiBodyProbe::Result::Fatal) {
            alive_ = false;
            bodySuppressed_ = true; bodyEnabled_ = false;
        }
        if (result == HyundaiBodyProbe::Result::Sample) return Result::BodySample;
        if (result == HyundaiBodyProbe::Result::Error || result == HyundaiBodyProbe::Result::Fatal) return Result::BodyError;
        return Result::None;
    }
    if (step_ < 6) {
        const bool valid = !errorText(response_) &&
            (step_ == 0 ? (std::strstr(response_, "ELM") || std::strstr(response_, "OBD"))
                        : std::strstr(response_, "OK") != nullptr);
        if (!valid) {
            alive_ = false; reason_ = "initialization rejected";
            return Result::Error;
        }
        ++step_; due_ = now + 50;
        return Result::Initialized;
    }
    uint8_t data[4] = {};
    if(drivingMetric_>=0 && drivingMetric_<3) {
        constexpr uint8_t pids[]={0x0c,0x04,0x11};
        const auto parsed=parsePid(response_,pids[drivingMetric_],data,drivingMetric_==0?2:1);
        reason_=parseName(parsed);due_=now+50;speedReady_=false;detailTurn_=false;
        drivingAt_[drivingMetric_]=now+(parsed==Parse::Valid?500:10000);
        if(parsed!=Parse::Valid)return Result::DrivingError;
        value_=drivingMetric_==0?(int(data[0])*256+data[1])/4:(int(data[0])*100+127)/255;
        return Result::Driving;
    }
    if (monitor_ >= 0) {
        const auto parsed = monitor_ == 0 ? parseMilStatus(response_, data[0], milResponseLines_)
                          : monitor_ == 1 ? parsePid(response_, 0x05, data, 1)
                                          : parseVoltage(response_, value_);
        reason_ = parseName(parsed);
        due_ = now + 50; detailTurn_ = false; speedReady_ = false;
        if (parsed != Parse::Valid) {
            monitorAt_[monitor_] = now + kMonitorPeriods[monitor_];
            return Result::MonitorError;
        }
        if (monitor_ < 2) value_ = monitor_ == 0 ? data[0] : static_cast<int>(data[0]) - 40;
        return Result::Monitor;
    }
    Parse parsed;
    if (metric_ == 0) parsed = parseVoltage(response_, value_);
    else if (metric_ > 0) parsed = parsePid(response_, kDetailPids[metric_], data, metric_ == 1 ? 1 : 2);
    else parsed = parsePid(response_, step_ == 6 ? 0x00 : 0x0d, data, step_ == 6 ? 4 : 1);
    reason_ = parseName(parsed);
    if (parsed != Parse::Valid) {
        // A prompt has arrived, so retry without repeatedly resetting the ELM.
        if (metric_ >= 0) {
            retryMetricAt_[metric_] = now + 30'000;
            due_ = now + 50;  // An optional unsupported PID must not hold up speed.
        } else { due_ = now + (step_ == 6 ? 5'000 : 1'000); speedReady_ = false; }
        return Result::Error;
    }
    if (metric_ >= 0) {
        if (metric_ == 1) value_ = static_cast<int>(data[0]) - 40;
        else if (metric_ >= 2) value_ = data[0] * 5;
        due_ = now + 50;
        return Result::Detail;
    }
    if (step_ == 6) {
        step_ = 7; due_ = now + 50;
        // PID 0D is bit 19 of the 01..20 bitmap. Still attempt 010D if a
        // clone's bitmap is incomplete: only an actual 410D response goes LIVE.
        return (data[1] & 0x08) ? Result::Supported : Result::Unsupported;
    }
    speed_ = data[0]; speedReady_ = true; due_ = now + config::kSpeedPollMs;
    if (!monitorStarted_) {
        monitorStarted_ = true;
        monitorAt_[0] = now;
        monitorAt_[1] = now + 1500;
        monitorAt_[2] = now + 3000;
    }
    return Result::Speed;
}
}  // namespace obd
