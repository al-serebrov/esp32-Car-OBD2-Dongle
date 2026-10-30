#pragma once

#include <cstddef>
#include <cstdint>
#include "Telemetry.h"

namespace obd {
enum class DiagnosticOutcome : uint8_t {
    Untested, Success, NoData, Negative, Malformed, Conflicting, Timeout,
    RoutingError, Protocol, Deferred, NotAdvertised, Cancelled, NotSelected
};
const char* diagnosticOutcomeName(DiagnosticOutcome outcome);
DiagnosticOutcome parseDiagnosticPayload(const char* command, const char* response,
                                        uint8_t* payload, size_t capacity, size_t& length);

class DiagnosticRun {
public:
    static constexpr unsigned kCustomCount = 14, kEntries = 269 + kCustomCount;
    enum class Event { None, Result, Fatal };
    struct Record {
        uint16_t successes = 0, failures = 0;
        DiagnosticOutcome outcome = DiagnosticOutcome::Untested;
        uint8_t length = 0, flags = 0;
        uint8_t sample[8] = {};
    };
    struct Report {
        uint32_t magic = 0x44494147, version = 1, session = 0;
        uint32_t activeMs = 0, speedMs = 0, speedGaps = 0;
        uint64_t distanceTwiceKmhMs = 0;
        uint16_t maxSpeed = 0;
        // Profile-tagged bitmap: PID capability maps in DISCOVERY, or the full
        // 256-value primary raw-gear observation set in CANDIDATES.
        uint8_t support[32] = {}, knownBanks = 0;
        bool sweepComplete = false;
        Record records[kEntries] = {};
        uint8_t customSamples[kCustomCount][64] = {};
        uint8_t customFirst[kCustomCount][64] = {};
        uint8_t customChanged[kCustomCount][64] = {};
        uint8_t extraSamples[5][64] = {};
        uint32_t checksum = 0;
    };
    void setEnabled(bool enabled, uint64_t now);
    void setCandidateFocus(bool enabled);
    void connected(uint64_t now);
    void tick(uint64_t now, bool connected);
    void observeSpeed(int kmh, uint64_t now);
    void breakSpeed() { haveSpeed_ = false; }
    const char* nextCommand(uint64_t now, int kmh);
    Event receive(const char* response, uint64_t now);
    bool timedOut(uint64_t now);
    void abort(DiagnosticOutcome outcome, uint64_t now);
    bool active() const { return stage_ != Stage::Idle; }
    bool enabled() const { return enabled_; }
    DiagnosticStatus status() const { return status_; }
    const char* name() const;
    const char* request() const { return request_; }
    const char* description() const { return description_; }
    unsigned item() const { return item_; }
    unsigned target() const;
    bool candidate() const { return item_ >= 269; }
    const uint8_t* payload() const { return payload_; }
    size_t payloadLength() const { return length_; }
    DiagnosticOutcome outcome() const { return report_.records[item_].outcome; }
    const Report& report() const { return report_; }
    Report& storageReport() { return report_; }
    bool acceptStoredReport();
    static bool validReport(const Report& report);
    bool dirty() const { return dirty_; }
    void prepareSave();
    void saved() { dirty_ = false; }
    static const char* entryName(unsigned item);
    static void entryRequest(unsigned item, char* destination, size_t capacity);
    // Record zero was unused in version 1; its first sample byte stores the
    // run profile without changing the on-flash layout or invalidating old reports.
    static bool candidateReport(const Report& report) { return report.records[0].sample[0] == 1; }
    MetricReading referenceRpm() const { return referenceRpm_; }
    MetricReading referenceIntake() const { return referenceIntake_; }
private:
    enum class Stage { Idle, Protocol, Header, Filter, Read, RestoreHeader, RestoreFilter };
    enum class Phase { Capabilities, Standard, Extra, Custom, Repeat };
    bool choose(uint64_t now, int kmh);
    void record(DiagnosticOutcome outcome, uint64_t now);
    void describe();
    void refreshStatus();
    void startSession();
    Report report_;
    DiagnosticStatus status_;
    Stage stage_ = Stage::Idle;
    Phase phase_ = Phase::Capabilities;
    bool enabled_ = false, started_ = false, dirty_ = false;
    bool protocolChecked_ = false, can11_ = false, haveSpeed_ = false;
    bool preferStandard_ = false;
    bool candidateFocus_ = false;
    unsigned focusedSlot_ = 0;
    uint64_t lastTcmMs_ = 0, lastOilMs_ = 0;
    MetricReading referenceRpm_, referenceIntake_;
    unsigned item_ = 256, bank_ = 0, standard_ = 1, extra_ = 264, custom_ = 0;
    uint32_t nextAt_[kEntries] = {};
    uint64_t deadline_ = 0, budget_ = 0, nextProbe_ = 0, lastTick_ = 0, lastSpeed_ = 0;
    int previousSpeed_ = 0;
    uint8_t payload_[128] = {};
    size_t length_ = 0;
    char request_[12] = {}, command_[20] = {}, description_[132] = {};
};
} // namespace obd
