#pragma once
#include <cstddef>
#include <cstdint>
#include "Telemetry.h"
namespace obd {
// Read-only 7E1/7E9, 21A0 transaction for the driver-validated LM ix35 profile.
class TransmissionPoll {
public:
    enum class Result { None, Sample, Error, Fatal };
    void reset(uint64_t now) { stage_=Stage::Idle; due_=now; }
    void enable(bool enabled) { enabled_=enabled; }
    bool due(uint64_t now) const { return enabled_ && !suppressed_ && now>=due_; }
    bool active() const { return stage_!=Stage::Idle; }
    void start() { stage_=Stage::Protocol; }
    const char* command();
    Result receive(const char* response,uint64_t now);
    void timeout() { suppressed_=true;stage_=Stage::Idle; }
    const TransmissionReading& reading() const { return reading_; }
    const char* reason() const { return reason_; }
private:
    enum class Stage { Idle, Protocol, Header, Filter, Read, RestoreHeader, RestoreFilter };
    Stage stage_=Stage::Idle;
    bool enabled_=false,suppressed_=false;
    uint64_t due_=0;
    TransmissionReading reading_;
    const char* reason_="not sampled";
};
// Unknown codes remain unknown; D with an unknown forward gear is shown as D.
inline void transmissionLabel(const TransmissionReading& value,char (&text)[4]) {
    text[0]='-';text[1]='-';text[2]=0;
    if (!value.valid) return;
    switch(value.selector) {
        case 8: text[0]='D';text[1]=0;if(value.gear>=1 && value.gear<=6){text[1]=char('0'+value.gear);text[2]=0;}break;
        case 9: text[0]='N';text[1]=0;break;
        case 10:text[0]='R';text[1]=0;break;
        case 11:text[0]='P';text[1]=0;break;
    }
}
} // namespace obd
