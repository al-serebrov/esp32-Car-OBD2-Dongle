#include "HyundaiBody.h"
#include "Elm327.h"
#include "Config.h"

#include <cctype>
#include <cstring>

namespace obd {
namespace {
int hex(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}
bool compact(const char* first, const char* last, char (&out)[48]) {
    unsigned used = 0;
    for (; first != last; ++first) {
        if (std::isspace(static_cast<unsigned char>(*first))) continue;
        if (used + 1 == sizeof(out)) return false;
        out[used++] = *first;
    }
    out[used] = 0;
    return true;
}
bool exact(const char* response, const char* expected) {
    char value[48];
    return compact(response, response + std::strlen(response), value) && !std::strcmp(value, expected);
}
} // namespace

Parse parseHyundaiBody(const char* response, uint8_t (&data)[8]) {
    if (std::strstr(response, "NO DATA") || std::strstr(response, "ERROR") ||
        std::strstr(response, "STOPPED") || std::strstr(response, "UNABLE") ||
        std::strstr(response, "BUFFER FULL") || std::strstr(response, "BUS BUSY") ||
        std::strchr(response, '?')) return Parse::AdapterError;
    uint8_t message[11] = {};
    unsigned used = 0, frame = 0;
    bool lengthSeen = false, complete = false;
    for (auto* first = response; *first;) {
        auto* last = first;
        while (*last && *last != '\r' && *last != '\n' && *last != '>') ++last;
        char value[48];
        if (!compact(first, last, value)) return Parse::Malformed;
        if (value[0] && std::strcmp(value, "22BC03")) {
            const char* bytes = value;
            const unsigned length = std::strlen(value);
            if (!std::strcmp(value, "00B") || !std::strcmp(value, "00b")) {
                if (lengthSeen || used || complete) return Parse::Malformed;
                lengthSeen = true;
            } else {
                if (lengthSeen) {
                    // ELM CAF1 with headers off: length, six-byte first line,
                    // then the five remaining bytes, with sequential indexes.
                    if (complete || frame > 1 || value[0] != static_cast<char>('0' + frame) || value[1] != ':' ||
                        length != (frame == 0 ? 14u : 12u)) return Parse::Malformed;
                    bytes += 2;
                    ++frame;
                } else if (length != 22 || complete) return Parse::Malformed;
                for (unsigned i = 0; bytes[i]; i += 2) {
                    const int high = hex(bytes[i]), low = hex(bytes[i + 1]);
                    if (high < 0 || low < 0 || used == sizeof(message)) return Parse::Malformed;
                    message[used++] = static_cast<uint8_t>(high * 16 + low);
                }
                complete = used == sizeof(message);
            }
        }
        first = *last ? last + 1 : last;
    }
    if (!used) return Parse::Missing;
    if (!complete || (lengthSeen && frame != 2)) return Parse::Malformed;
    if (message[0] != 0x62 || message[1] != 0xbc || message[2] != 0x03) return Parse::Missing;
    std::memcpy(data, message + 3, sizeof(data));
    return Parse::Valid;
}

void HyundaiBodyProbe::reset(uint64_t now) {
    stage_ = Stage::Idle;
    nextAt_ = now + config::kBodyProbeStartupMs;
    protocolChecked_ = failed_ = false;
    reason_ = "not attempted";
}
void HyundaiBodyProbe::start() {
    failed_ = false;
    stage_ = protocolChecked_ ? Stage::Target : Stage::Protocol;
}
const char* HyundaiBodyProbe::command() const {
    switch (stage_) {
        case Stage::Protocol: return "ATDPN\r";
        case Stage::Target: return "ATSH770\r";
        case Stage::Filter: return "ATCRA778\r";
        case Stage::Read: return "22BC03\r";
        case Stage::RestoreHeader: return "ATSH7DF\r";
        case Stage::RestoreFilter: return "ATCRA\r";
        case Stage::Idle: return nullptr;
    }
    return nullptr;
}
HyundaiBodyProbe::Result HyundaiBodyProbe::receive(const char* response, uint64_t now) {
    if (stage_ == Stage::Protocol) {
        if (!exact(response, "A6") && !exact(response, "6")) {
            stage_ = Stage::Idle;
            nextAt_ = UINT64_MAX; // No CAN routing changes on another protocol.
            reason_ = "body probe requires CAN 11-bit / 500 kbit protocol 6; disabled until reconnect";
            return Result::Error;
        }
        protocolChecked_ = true;
        stage_ = Stage::Target;
        return Result::Progress;
    }
    if (stage_ == Stage::Read) {
        const auto result = parseHyundaiBody(response, data_);
        failed_ = result != Parse::Valid;
        reason_ = failed_ ? parseName(result) : "candidate response; LM bit mapping unverified";
        stage_ = Stage::RestoreHeader;
        return failed_ ? Result::Error : Result::Sample;
    }
    const bool ok = exact(response, "OK");
    if (stage_ == Stage::RestoreHeader || stage_ == Stage::RestoreFilter) {
        if (!ok) {
            stage_ = Stage::Idle;
            reason_ = "standard OBD routing restoration rejected; reconnect required";
            return Result::Fatal;
        }
        if (stage_ == Stage::RestoreHeader) stage_ = Stage::RestoreFilter;
        else {
            stage_ = Stage::Idle;
            nextAt_ = now + (failed_ ? config::kBodyProbeRetryMs : config::kBodyProbePollMs);
        }
        return Result::Progress;
    }
    if (stage_ == Stage::Target || stage_ == Stage::Filter) {
        if (!ok) {
            failed_ = true;
            reason_ = "candidate body routing rejected; restoring standard OBD routing";
            stage_ = Stage::RestoreHeader;
            return Result::Error;
        }
        stage_ = stage_ == Stage::Target ? Stage::Filter : Stage::Read;
    }
    return Result::Progress;
}
} // namespace obd
