#include "TransmissionPoll.h"
#include "DiagnosticRun.h"
#include <cctype>
#include <cstring>
namespace obd {
namespace {
bool exact(const char* s,const char* expected) {
    for(;*s;++s)if(!std::isspace(static_cast<unsigned char>(*s)) && *s!=*expected++)return false;
    return !*expected;
}
}
const char* TransmissionPoll::command() {
    if(!enabled_ && (stage_==Stage::Header || stage_==Stage::Filter || stage_==Stage::Read))stage_=Stage::RestoreHeader;
    switch(stage_) {
        case Stage::Protocol:return "ATDPN\r";
        case Stage::Header:return "ATSH7E1\r";
        case Stage::Filter:return "ATCRA7E9\r";
        case Stage::Read:return "21A0\r";
        case Stage::RestoreHeader:return "ATSH7DF\r";
        case Stage::RestoreFilter:return "ATCRA\r";
        default:return nullptr;
    }
}
TransmissionPoll::Result TransmissionPoll::receive(const char* s,uint64_t now) {
    if(stage_==Stage::Protocol) {
        if(!exact(s,"6") && !exact(s,"A6")){stage_=Stage::Idle;suppressed_=true;reason_="unsupported transmission protocol";return Result::Error;}
        stage_=enabled_?Stage::Header:Stage::Idle;return Result::None;
    }
    if(stage_==Stage::Header || stage_==Stage::Filter) {
        if(!exact(s,"OK")){stage_=Stage::RestoreHeader;suppressed_=true;reason_="transmission routing command rejected";return Result::Error;}
        stage_=!enabled_?Stage::RestoreHeader:stage_==Stage::Header?Stage::Filter:Stage::Read;return Result::None;
    }
    if(stage_==Stage::RestoreHeader || stage_==Stage::RestoreFilter) {
        if(!exact(s,"OK")){stage_=Stage::Idle;suppressed_=true;reason_="transmission route restore rejected";return Result::Fatal;}
        stage_=stage_==Stage::RestoreHeader?Stage::RestoreFilter:Stage::Idle;return Result::None;
    }
    if(stage_!=Stage::Read)return Result::None;
    uint8_t bytes[128]={};size_t size=0;
    const auto parsed=parseDiagnosticPayload("21A0",s,bytes,sizeof(bytes),size);
    stage_=Stage::RestoreHeader;
    if(parsed!=DiagnosticOutcome::Success || size<23) {
        // A short prompted reply on this proven block is a transient failure.
        due_=now+((parsed==DiagnosticOutcome::Malformed || parsed==DiagnosticOutcome::Success)?1000:10000);
        reason_="invalid or unavailable transmission reply";return Result::Error;
    }
    reading_={bytes[20],bytes[21],now,true};due_=now+1500;reason_="valid 21A0";return Result::Sample;
}
} // namespace obd
