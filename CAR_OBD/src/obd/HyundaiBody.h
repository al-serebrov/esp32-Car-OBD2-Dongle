#pragma once

#include <cstdint>

namespace obd {
enum class Parse;
// Candidate Hyundai-family 770/778, 22BC03 layout. Not verified for the LM ix35.
// Data bytes exclude the positive response prefix 62 BC 03.
Parse parseHyundaiBody(const char* response, uint8_t (&data)[8]);

class HyundaiBodyProbe {
public:
    enum class Result { Progress, Sample, Error, Fatal };
    void reset(uint64_t now);
    bool due(uint64_t now) const { return stage_ == Stage::Idle && now >= nextAt_; }
    bool active() const { return stage_ != Stage::Idle; }
    void start();
    const char* command() const;
    Result receive(const char* response, uint64_t now);
    const char* reason() const { return reason_; }
    const uint8_t* data() const { return data_; }
private:
    enum class Stage { Idle, Protocol, Target, Filter, Read, RestoreHeader, RestoreFilter };
    Stage stage_ = Stage::Idle;
    uint64_t nextAt_ = 0;
    bool protocolChecked_ = false, failed_ = false;
    uint8_t data_[8] = {};
    const char* reason_ = "not attempted";
};
} // namespace obd
