#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include "obd/Elm327.h"
#include "fixtures/hyundai_ride_2026_10_30.h"
using obd::DiagnosticRun;
using O = obd::DiagnosticOutcome;
using E = DiagnosticRun::Event;

struct Run {
    DiagnosticRun run;
    uint64_t now = 100;
    int speed = 0;
    Run(bool focused = false) { run.setCandidateFocus(focused); run.connected(now); run.setEnabled(true, now); run.observeSpeed(0, now); }
    std::string command() { const auto* c = run.nextCommand(now, speed); return c ? c : ""; }
    E reply(const char* response) { now += 20; const auto e = run.receive(response, now); now += 20; return e; }
    void advance() { now += 1100; run.observeSpeed(speed, now); }
    void extras() {
        assert(command() == "0100\r"); reply("410000080000\r"); advance();
        for (const auto* c : {"03\r", "07\r", "0A\r", "0902\r", "ATRV\r"}) {
            assert(command() == c); reply("NO DATA\r"); advance();
        }
    }
    void restore() { assert(command() == "ATSH7DF\r"); reply("OK\r"); assert(command() == "ATCRA\r"); reply("OK\r"); }
    void route(bool protocol = false) {
        if (protocol) { assert(command() == "ATDPN\r"); reply("A6\r"); }
        const auto header = command(); assert(header.rfind("ATSH", 0) == 0); reply("OK\r");
        assert(command().rfind("ATCRA", 0) == 0); reply("OK\r");
    }
};

std::string multiline(const std::vector<uint8_t>& message) {
    char line[32]; std::snprintf(line, sizeof(line), "%03X\r", unsigned(message.size()));
    std::string out = line;
    size_t offset = 0; unsigned frame = 0;
    while (offset < message.size()) {
        std::snprintf(line, sizeof(line), "%X:", frame % 16); out += line;
        const size_t end = std::min(message.size(), offset + (frame ? 7 : 6));
        for (; offset < end; ++offset) { std::snprintf(line, sizeof(line), "%02X", message[offset]); out += line; }
        out += '\r'; ++frame;
    }
    return out;
}

int main() {
    uint8_t payload[128] = {}; size_t length = 0;
    const auto parse = [&](const char* cmd, const char* response) { return obd::parseDiagnosticPayload(cmd, response, payload, sizeof(payload), length); };
    assert(parse("010C", "010C\r410C1234\r") == O::Success && length == 2 && payload[1] == 0x34);
    assert(parse("010C", "410C1234\r410C1234\r") == O::Success);
    assert(parse("010C", "410C1234\r410C1235\r") == O::Conflicting);
    assert(parse("21A0", "7F2112\r") == O::Negative);
    assert(parse("2201A0", "NO DATA\r") == O::NoData);
    for (auto response : {"6201A100", "6201A0GG", "junk6201A000", "7E9056201A000", "009\r0:6201A0000000\r", "009\r1:6201A0000000\r1:000000\r"})
        assert(parse("2201A0", response) == O::Malformed);
    std::vector<uint8_t> message{0x62, 0x01, 0xa0};
    for (unsigned i = 0; i < 128; ++i) message.push_back(i);
    const auto frames = multiline(message);
    assert(parse("2201A0", frames.c_str()) == O::Success && length == 128 && payload[127] == 127);
    assert(parse("2201A0", frames.substr(0, frames.size()-4).c_str()) == O::Malformed);
    message.push_back(0xff);
    assert(parse("2201A0", multiline(message).c_str()) == O::Malformed);

    // Exact car replies: padded frames must preserve declared payloads, including
    // replies whose published semantic mapping does not fit this vehicle.
    for (const auto& fixture : kHyundaiRideFixtures) {
        assert(parse(fixture.command, fixture.response) == O::Success);
        assert(length*2 == std::strlen(fixture.payload));
        char hex[257];
        for (size_t i=0;i<length;++i) std::snprintf(hex+2*i,3,"%02X",payload[i]);
        assert(!std::strcmp(hex, fixture.payload));
    }
    assert(parse("2201A0", "009\r0:6201A0000000\r1:000000AAAAAAAA\r") == O::Success && length==6);
    for (const auto* bad : {"009\r0:6201A0000000\r1:000000AA\r", // Partial extra padding.
                            "009\r0:6201A0000000\r1:000000AAAAAAAG\r", // Invalid padding hex.
                            "009\r0:6201A0000000\r1:000000AAAAAAAA\r2:00\r", // Extra frame.
                            "009\r0:6201A0000000\r2:000000AAAAAAAA\r"})
        assert(parse("2201A0",bad)==O::Malformed);
    const auto maximum = multiline(std::vector<uint8_t>(message.begin(),message.end()-1));
    auto paddedMaximum=maximum;
    paddedMaximum.insert(paddedMaximum.size()-1,"AA");
    assert(parse("2201A0",paddedMaximum.c_str())==O::Success && length==128);

    Run focused(true);
    static_assert(sizeof(DiagnosticRun::Report)==7608,"Keep existing NVS reports readable");
    assert(DiagnosticRun::candidateReport(focused.run.report()));
    assert(focused.run.report().records[274].outcome==O::NotSelected);
    const unsigned slots[]={269,0x0c,269,270,269,280,269,0x0f};
    for (unsigned slot=0;slot<8;++slot) {
        const auto id=slots[slot];
        if (id>=269) focused.route(slot==0);
        char request[12];DiagnosticRun::entryRequest(id,request,sizeof(request));
        assert(focused.command()==std::string(request)+"\r");
        assert(focused.run.item()==id);
        const char* response=id==0x0c ? "410C0D50\r" : "410F46\r";
        if(id>=269) for(const auto& fixture:kHyundaiRideFixtures) if(fixture.item==id){response=fixture.response;break;}
        assert(focused.reply(response)==E::Result);
        if(id>=269) focused.restore();
        focused.advance();
    }
    focused.run.tick(focused.now,true);
    auto status=focused.run.status();
    assert(status.candidateFocus && status.complete && status.succeeded==3 && status.failed==0 && status.samples==6);
    assert(status.gearValid && status.gear==14 && status.commandedGear==0 && status.atfC==32);
    assert(status.oilValid && status.oilQuarterC==132);
    assert(focused.run.report().support[14/8] & (1u << (14%8)));
    assert(focused.run.referenceRpm().valid && focused.run.referenceRpm().value==852);
    assert(focused.run.referenceIntake().valid && focused.run.referenceIntake().value==30);
    focused.run.tick(focused.now+6000,true);assert(!focused.run.status().gearValid && focused.run.status().oilValid);
    focused.run.tick(focused.now+21000,true);assert(!focused.run.status().oilValid);
    focused.run.tick(focused.now,false);assert(!focused.run.status().gearValid);
    focused.run.prepareSave();DiagnosticRun reloaded;reloaded.storageReport()=focused.run.report();
    assert(reloaded.acceptStoredReport() && DiagnosticRun::candidateReport(reloaded.report()));
    const auto before=reloaded.report().checksum;
    reloaded.setCandidateFocus(true);reloaded.setEnabled(true,focused.now);
    assert(!reloaded.nextCommand(focused.now,0)); // No new car speed since boot.
    reloaded.tick(focused.now,false);assert(reloaded.status().seconds==0 && reloaded.status().samples==0);
    assert(reloaded.report().checksum==before && DiagnosticRun::validReport(reloaded.report()));
    // VIN's FF placeholders must remain invalid even though padding now parses.
    Run vin;
    assert(vin.command()=="0100\r");vin.reply("410000080000\r");vin.advance();
    for(const auto* cmd:{"03\r","07\r","0A\r"}){assert(vin.command()==cmd);vin.reply("NO DATA\r");vin.advance();}
    assert(vin.command()=="0902\r");
    for(const auto& fixture:kHyundaiRideFixtures)if(fixture.item==267){vin.reply(fixture.response);break;}
    assert(vin.run.outcome()==O::Malformed);

    // Advertised capability banks are unioned; unsupported banks/PIDs never queried.
    Run banks;
    assert(banks.command() == "0100\r"); banks.reply("410080080001\r"); banks.advance();
    assert(banks.command() == "0120\r"); banks.reply("412080000000\r412000000002\r"); banks.advance();
    assert(banks.command() == "0101\r"); banks.reply("410100000000\r410181000000\r"); banks.advance();
    assert(banks.run.report().records[1].successes == 1);
    assert(std::strstr(banks.run.description(), "MIL ON"));
    assert(banks.command() == "0121\r"); banks.reply("4121000A\r"); banks.advance();
    assert(banks.command() == "013F\r"); banks.reply("413F1234\r"); banks.advance();
    assert(banks.command() == "03\r");
    assert(banks.run.report().records[2].outcome == O::NotAdvertised);
    assert(banks.run.report().knownBanks == 3);

    Run run; run.extras();
    run.route(true); assert(run.command() == "21A0\r");
    message = {0x61, 0xa0}; for (unsigned i = 0; i < 40; ++i) message.push_back(i);
    assert(run.reply(multiline(message).c_str()) == E::Result);
    assert(run.run.outcome() == O::Success && run.run.payloadLength() == 40);
    assert(run.run.report().records[269].sample[4] == 4);
    assert(run.run.active()); run.restore(); run.advance();
    run.route(); assert(run.command() == "21A1\r");
    assert(run.reply("NO DATA\r") == E::Result); run.restore(); run.advance();
    assert(run.run.report().records[270].outcome == O::NoData);
    // Leaving the screen during a routing transaction restores functional routing.
    assert(run.command() == "ATSH7E1\r"); run.reply("OK\r");
    run.run.setEnabled(false, run.now);
    assert(run.command() == "ATCRA7E9\r" || run.command() == "ATSH7DF\r");
    run.reply("OK\r");
    while (run.run.active()) { const auto cmd = run.command(); assert(cmd == "ATSH7DF\r" || cmd == "ATCRA\r"); run.reply("OK\r"); }
    assert(run.command().empty());

    // Every missing prompt phase quarantines the candidate, then allows a new candidate after reconnect.
    for (unsigned phase = 0; phase < 6; ++phase) {
        Run fail; fail.extras();
        const char* commands[] = {"ATDPN\r", "ATSH7E1\r", "ATCRA7E9\r", "21A0\r", "ATSH7DF\r", "ATCRA\r"};
        for (unsigned i = 0; i <= phase; ++i) {
            assert(fail.command() == commands[i]);
            if (i < phase) fail.reply(i == 0 ? "6\r" : i == 3 ? "NO DATA\r" : "OK\r");
        }
        assert(!fail.run.timedOut(fail.now + 1));
        assert(fail.run.timedOut(fail.now + 1300));
        assert(fail.run.outcome() == O::Timeout && fail.run.report().records[269].flags & 1);
        fail.now += 2000; fail.run.connected(fail.now);
        assert(fail.command() == "ATDPN\r"); fail.reply("6\r");
        assert(fail.command() == "ATSH7E1\r" && fail.run.item() == 270);
    }
    Run rejected; rejected.extras();
    assert(rejected.command() == "ATDPN\r"); rejected.reply("A3\r");
    assert(rejected.run.outcome() == O::Protocol && !rejected.run.active());

    Run moving; moving.extras(); moving.speed = 30;
    for (unsigned i = 0; i < DiagnosticRun::kCustomCount; ++i) {
        if (i == 5 || i == 6) continue;
        moving.route(i == 0);
        assert(moving.run.item() == 269 + i);
        moving.command(); moving.reply("NO DATA\r"); moving.restore(); moving.advance();
    }
    moving.command();
    assert(moving.run.status().complete && moving.run.status().deferred == 2);
    moving.speed = 0; moving.advance(); moving.route();
    assert(moving.run.item() == 274); // Deferred brake/belt candidate is retried below 10.

    // End-to-end single-command ELM scheduler: speed between each optional
    // transaction, all repeated standard readings get time alongside custom data.
    for (bool focus : {false,true}) {
    obd::Elm327 elm; elm.diagnostics().setCandidateFocus(focus); elm.reset(0); elm.setEngineMonitorEnabled(true);
    elm.diagnostics().setEnabled(true, 0);
    uint64_t pendingAt = 0, previousSpeed = 0, maxGap = 0;
    unsigned speedCount = 0, optionalCount = 0;
    bool optionalSinceSpeed = false, routed = false;
    std::string pending;
    for (uint64_t clock = 0; clock < 300000; clock += 10) {
        elm.diagnostics().tick(clock, true);
        assert(!elm.timedOut(clock));
        if (!pending.empty() && clock >= pendingAt) {
            std::string response;
            if (pending == "ATZ\r") response = "ELM327 v2.3\r>";
            else if (pending == "ATDPN\r") response = "A6\r>";
            else if (pending.rfind("ATSH", 0) == 0) { routed = pending != "ATSH7DF\r"; response = "OK\r>"; }
            else if (pending == "ATRV\r") response = "14.2V\r>";
            else if (pending.rfind("AT", 0) == 0) response = "OK\r>";
            else if (pending == "0100\r") response = "410080180001\r>"; // MIL, RPM, speed, next bank.
            else if (pending == "0120\r") response = "412080000000\r>"; // Distance with MIL.
            else if (pending == "010D\r") response = "410D00\r>";
            else if (pending == "0101\r") response = "410100000000\r>";
            else if (pending == "0105\r") response = "41057D\r>";
            else if (pending == "010C\r") response = "410C1000\r>";
            else if (pending == "010F\r") response = "410F46\r>";
            else if (pending == "0121\r") response = "41210000\r>";
            else if (pending == "03\r") response = "4300\r>";
            else if (pending == "07\r") response = "4700\r>";
            else if (pending == "0A\r") response = "4A00\r>";
            else if (pending == "0902\r") response = "4902014B4D483132333435363738393031323334\r>";
            else {
                const auto request = pending.substr(0, pending.size()-1);
                std::vector<uint8_t> positive;
                for (size_t i = 0; i < request.size(); i += 2) positive.push_back(std::stoul(request.substr(i,2), nullptr,16));
                positive[0] += 0x40;
                for (unsigned i = 0; i < 64; ++i) positive.push_back(static_cast<uint8_t>(i + clock/1000));
                response = multiline(positive) + ">";
            }
            bool diagnosticResult = false;
            for (const auto byte : response) {
                const auto result = elm.receive(byte, clock);
                diagnosticResult |= result == obd::Elm327::Result::Diagnostic;
                if (result == obd::Elm327::Result::Speed) {
                    assert(!routed); elm.diagnostics().observeSpeed(elm.speed(), clock);
                    if (previousSpeed) maxGap = std::max(maxGap, clock - previousSpeed);
                    previousSpeed = clock; ++speedCount; optionalSinceSpeed = false;
                }
                if (result == obd::Elm327::Result::Monitor || diagnosticResult) {
                    assert(!optionalSinceSpeed); optionalSinceSpeed = true; ++optionalCount; diagnosticResult = false;
                }
            }
            assert(elm.alive()); pending.clear();
        }
        if (const auto* next = elm.nextCommand(clock)) {
            assert(pending.empty()); pending = next; pendingAt = clock + 60;
            assert(elm.speedTransaction()==(pending=="010D\r"));
            if (pending == "010D\r") assert(!routed);
        }
    }
    assert(elm.diagnostics().status().complete);
    assert(elm.diagnostics().report().records[0x0c].successes > 3);
    assert(elm.diagnostics().report().records[269].successes > 3);
    assert(focus ? elm.diagnostics().report().records[267].outcome==O::NotSelected : elm.diagnostics().report().records[267].successes > 0);
    assert(elm.diagnostics().report().customChanged[0][21] != 0);
    assert(elm.diagnostics().report().customFirst[0][21] != elm.diagnostics().report().customSamples[0][21]);
    assert(speedCount > 300 && maxGap < config::kLiveStaleMs);
    if(focus) {
        const auto& records=elm.diagnostics().report().records;
        assert(records[269].successes>100 && records[269].successes>records[270].successes*3);
        assert(records[270].successes>20 && records[280].successes>20 && records[0x0f].successes>20);
        assert(records[274].outcome==O::NotSelected && records[256].outcome==O::NotSelected);
        std::printf("FOCUSED_SAMPLES tcm21A0=%u tcm21A1=%u engine2101=%u RPM=%u IAT=%u\n",records[269].successes,records[270].successes,records[280].successes,records[0x0c].successes,records[0x0f].successes);
    }
    std::printf("DIAGNOSTIC_SCHEDULER focused=%d duration_ms=300000 reply_ms=60 speed_samples=%u optional=%u max_speed_gap_ms=%llu\n", focus, speedCount, optionalCount, static_cast<unsigned long long>(maxGap));
    }


    // Local trip integration excludes stale intervals, paused pages and disconnects.
    Run trip;
    trip.run.observeSpeed(36, 200); trip.run.breakSpeed(); trip.run.observeSpeed(36, 300);
    trip.run.observeSpeed(36, 1300);
    assert(trip.run.report().distanceTwiceKmhMs / 7200 == 10);
    trip.run.observeSpeed(36, 3300);
    assert(trip.run.report().distanceTwiceKmhMs / 7200 == 10 && trip.run.report().speedGaps == 1);
    trip.run.setEnabled(false, 3400); trip.run.observeSpeed(36, 4000);
    trip.run.setEnabled(true, 4500); trip.run.observeSpeed(36, 4500);
    assert(trip.run.report().distanceTwiceKmhMs / 7200 == 10);
    trip.run.prepareSave();
    DiagnosticRun loaded; loaded.storageReport() = trip.run.report();
    assert(loaded.acceptStoredReport());
    loaded.setEnabled(true, 5000); loaded.tick(5100, false);
    assert(loaded.report().distanceTwiceKmhMs / 7200 == 10); // Viewing at PC preserves prior run.
    loaded.observeSpeed(0, 5200);
    assert(loaded.report().session == trip.run.report().session + 1 && loaded.report().distanceTwiceKmhMs == 0);
    auto corrupt = trip.run.report(); ++corrupt.activeMs;
    assert(!DiagnosticRun::validReport(corrupt));
    std::printf("PASS: discovery parsing/capabilities, bounded routing and timeout quarantine, page exit, speed gate, trip, persisted report (%zu bytes)\n", sizeof(DiagnosticRun::Report));
}
