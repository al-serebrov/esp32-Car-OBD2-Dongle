#include <cassert>
#include <cstring>
#include <cstdio>
#include <string>
#include <vector>
#include <algorithm>
#include "obd/Elm327.h"
#include "Telemetry.h"

using obd::Elm327;
Elm327::Result reply(Elm327& elm, const char* response, uint64_t now) {
    auto result = Elm327::Result::None;
    for (const char* p = response; *p; ++p) {
        const auto next = elm.receive(*p, now);
        if (next != Elm327::Result::None) result = next;
    }
    return result;
}
uint64_t initialize(Elm327& elm, bool advertise = true, const char* supportedResponse = nullptr) {
    uint64_t now = 100;
    elm.reset(now);
    const char* commands[] = {"ATZ\r", "ATE0\r", "ATL0\r", "ATS0\r", "ATH0\r", "ATSP0\r", "0100\r"};
    for (int i = 0; i < 7; ++i) {
        const auto* command = elm.nextCommand(now);
        assert(command && !std::strcmp(command, commands[i]));
        assert(!elm.nextCommand(now)); // Never overlap commands, even after a partial reply.
        const auto result = reply(elm, i == 0 ? "ATZ\rELM327 v2.1\r>" : i == 6 ?
            (supportedResponse ? supportedResponse : advertise ? "SEARCHING...\r410000080000\r>" : "410000000000\r>") : "OK\r>", now + 10);
        assert(result == (i == 6 ? (advertise ? Elm327::Result::Supported : Elm327::Result::Unsupported) : Elm327::Result::Initialized));
        assert(!elm.nextCommand(now + 10));
        now += 100;
    }
    return now;
}

int main() {
    assert(!config::kHyundaiBodyProbeEnabled); // Production must not issue body probes.
    assert(config::kMilPollMs == 10000 && config::kCoolantPollMs == 20000 && config::kVoltagePollMs == 20000);
    uint8_t mil = 0;
    unsigned responseLines = 0;
    assert(obd::parseMilStatus("41 01 81 07 65 00\r>", mil, responseLines) == obd::Parse::Valid);
    assert(mil == 0x81 && responseLines == 1);
    for (auto* responses : {"410100000000\r410185076500>", "410185076500\r410100000000>"}) {
        assert(obd::parseMilStatus(responses, mil, responseLines) == obd::Parse::Valid);
        assert(mil == 0x85 && responseLines == 2); // Any ECU's MIL on counts; readiness bytes may differ.
    }
    assert(obd::parseMilStatus("410181000000\r410105076500>", mil, responseLines) == obd::Parse::Valid && mil == 0x85);
    assert(obd::parseMilStatus("410105000000\r410105000000>", mil, responseLines) == obd::Parse::Valid && mil == 5);
    for (auto* bad : {"NO DATA>", "4101000000>", "41010000000000>", "7E806410100000000>",
                      "junk410180000000>", "410100000000\r4101GG000000>",
                      "410100000000\r4101000000000000000000000000000000000000000000000000>"})
        assert(obd::parseMilStatus(bad, mil, responseLines) != obd::Parse::Valid);
    uint8_t speed[1] = {};
    assert(obd::parsePid("010D\rSEARCHING...\r41 0D 57\r>", 0x0d, speed, 1) == obd::Parse::Valid && speed[0] == 87);
    assert(obd::parsePid("410d00", 0x0d, speed, 1) == obd::Parse::Valid && speed[0] == 0);
    assert(obd::parsePid("410DFF\r410DFF", 0x0d, speed, 1) == obd::Parse::Valid && speed[0] == 255);
    assert(obd::parsePid("410D10\r410D11", 0x0d, speed, 1) == obd::Parse::Conflicting);
    for (const auto* response : {"", "010D", "410C1234", "7F0112", "7E803410D57", "junk410D57", "410DGG"})
        assert(obd::parsePid(response, 0x0d, speed, 1) != obd::Parse::Valid);
    assert(obd::parsePid("410D", 0x0d, speed, 1) == obd::Parse::Malformed);
    assert(obd::parsePid("410D5700", 0x0d, speed, 1) == obd::Parse::Malformed);
    for (const auto* response : {"NO DATA", "UNABLE TO CONNECT", "STOPPED", "?", "CAN ERROR"})
        assert(obd::parsePid(response, 0x0d, speed, 1) == obd::Parse::AdapterError);

    // Regression: boot 108, records 2304..2307 in the 2026-10-27 car export.
    // The vehicle returned two valid capability maps, in either order.
    uint8_t supported[4] = {};
    const uint8_t expected[] = {0xbe, 0x3e, 0xb8, 0x13};
    for (const auto* response : {"SEARCHING...\r410080000001\r4100BE3EB813\r\r>",
                                 "4100BE3EB813\r410080000001\r\r>"}) {
        assert(obd::parsePid(response, 0x00, supported, 4) == obd::Parse::Valid);
        assert(!std::memcmp(supported, expected, 4));
    }
    const uint8_t disjointExpected[] = {0x80, 0x08, 0x00, 0x01};
    assert(obd::parsePid("410080000001\r410000080000", 0, supported, 4) == obd::Parse::Valid);
    assert(!std::memcmp(supported, disjointExpected, 4));
    assert(obd::parsePid("410080000001\r4100BE3EB8", 0, supported, 4) == obd::Parse::Malformed);

    Elm327 elm;
    auto recorded = initialize(elm, true, "SEARCHING...\r410080000001\r4100BE3EB813\r\r>");
    assert(!std::strcmp(elm.nextCommand(recorded), "010D\r")); // Must leave capability discovery.
    assert(reply(elm, "410D00\r>", recorded + 100) == Elm327::Result::Speed && elm.speed() == 0);
    auto now = initialize(elm);
    assert(!std::strcmp(elm.nextCommand(now), "010D\r"));
    assert(reply(elm, "41 0", now) == Elm327::Result::None);
    assert(!elm.nextCommand(now + 100));
    assert(reply(elm, "D 00\r", now + 120) == Elm327::Result::None);
    assert(reply(elm, ">", now + 150) == Elm327::Result::Speed && elm.speed() == 0);
    assert(!elm.nextCommand(now + 150 + config::kSpeedPollMs - 1));
    assert(!std::strcmp(elm.nextCommand(now + 150 + config::kSpeedPollMs), "010D\r"));
    assert(reply(elm, "NO DATA\r>", now + 500) == Elm327::Result::Error);
    assert(!elm.nextCommand(now + 1499));
    assert(!std::strcmp(elm.nextCommand(now + 1500), "010D\r")); // No reset for NO DATA.
    assert(!elm.timedOut(now + 4499));
    assert(elm.timedOut(now + 4500) && !elm.alive());
    assert(!elm.nextCommand(now + 99999)); // Reconnect must explicitly start a new session.

    now = initialize(elm, false);
    assert(!std::strcmp(elm.nextCommand(now), "010D\r"));
    assert(reply(elm, "410DFF\r>", now) == Elm327::Result::Speed && elm.speed() == 255);
    assert(reply(elm, "410D10\r>", now) == Elm327::Result::None); // Unsolicited data cannot replace reading.
    assert(elm.speed() == 255);
    assert(elm.nextCommand(now + config::kSpeedPollMs));
    const std::string oversized(600, 'A');
    assert(reply(elm, oversized.c_str(), now + 500) == Elm327::Result::None);
    assert(reply(elm, ">", now + 500) == Elm327::Result::Error && !elm.alive());

    elm.reset(0); assert(elm.nextCommand(0));
    assert(reply(elm, "?\r>", 1) == Elm327::Result::Error && !elm.alive());
    now = initialize(elm);
    assert(elm.nextCommand(now));
    elm.receive('\0', now);
    assert(reply(elm, "410D57>", now) == Elm327::Result::Error);

    int mv = -1;
    assert(obd::parseVoltage("ATRV\r12.6V\r>", mv) == obd::Parse::Valid && mv == 12600);
    assert(obd::parseVoltage(" 14.25 V\r14.250V\r>", mv) == obd::Parse::Valid && mv == 14250);
    assert(obd::parseVoltage("0.0V", mv) == obd::Parse::Valid && mv == 0);
    assert(obd::parseVoltage("12.6V\r12.7V", mv) == obd::Parse::Conflicting);
    for (const auto* bad : {"?", "NO DATA", "12V", "12.3456V", "12.6Vjunk", "123456789.1V", "nanV", "-12.0V"})
        assert(obd::parseVoltage(bad, mv) != obd::Parse::Valid);

    now = initialize(elm);
    elm.setDetailsActive(true);
    const char* commands[] = {"010D\r", "ATRV\r", "010D\r", "0105\r", "010D\r", "0114\r", "010D\r", "0115\r"};
    const char* responses[] = {"410D00>", "14.2V>", "410D57>", "410500>", "410D57>", "411400FF>", "410D57>", "4115FFFF>"};
    const int values[] = {0, 14200, 0, -40, 0, 0, 0, 1275};
    for (int i = 0; i < 8; ++i) {
        assert(!std::strcmp(elm.nextCommand(now), commands[i]));
        assert(!elm.nextCommand(now + 1));
        const auto result = reply(elm, responses[i], now + 10);
        assert(result == (i % 2 ? Elm327::Result::Detail : Elm327::Result::Speed));
        if (i % 2) { assert(elm.value() == values[i]); assert(elm.metric() == i / 2); }
        now += 1000;
    }
    assert(!std::strcmp(elm.nextCommand(now), "010D\r"));
    assert(reply(elm, "410D57>", now + 10) == Elm327::Result::Speed);
    now += 1000;
    assert(!std::strcmp(elm.nextCommand(now), "ATRV\r"));
    assert(reply(elm, "NO DATA>", now + 10) == Elm327::Result::Error && elm.alive());
    assert(elm.metric() == 0 && elm.speed() == 87);
    now += 100;
    assert(!std::strcmp(elm.nextCommand(now), "010D\r"));
    reply(elm, "410D57>", now + 10);
    elm.setDetailsActive(false);
    for (int i = 0; i < 6; ++i) {
        now += 1000;
        assert(!std::strcmp(elm.nextCommand(now), "010D\r"));
        assert(reply(elm, "410D57>", now + 10) == Elm327::Result::Speed);
    }
    elm.setDetailsActive(true);
    now += 1000;
    // The failed voltage command is cooled down, so the next detail is coolant.
    assert(!std::strcmp(elm.nextCommand(now), "0105\r"));
    assert(reply(elm, "41057B>", now + 10) == Elm327::Result::Detail && elm.value() == 83);
    now += 1000;
    assert(!std::strcmp(elm.nextCommand(now), "010D\r"));
    reply(elm, "410D57>", now + 10);
    now += 1000;
    assert(!std::strcmp(elm.nextCommand(now), "0114\r"));
    assert(reply(elm, "41148000\r41148100>", now + 10) == Elm327::Result::Error && elm.alive());
    // A missing prompt still requires transport recovery; it is never silently skipped.
    now += 1000;
    assert(elm.nextCommand(now));
    assert(elm.timedOut(now + 3000) && !elm.alive());

    SpeedReading reading{0, 100, true};
    assert(isLive(reading, 100)); // Zero is a valid real reading.
    assert(isLive(reading, 100 + config::kLiveStaleMs - 1));
    assert(!isLive(reading, 100 + config::kLiveStaleMs));
    assert(!isLive(reading, 99));
    reading.valid = false;
    assert(!isLive(reading, 100));

    // Candidate Hyundai body layout: accept only the exact DID and eight data
    // bytes, including ELM's length/indexed ISO-TP formatting with CAF1/ATH0.
    uint8_t body[8] = {};
    const uint8_t expectedBody[] = {0xff, 0xee, 0x20, 0x63, 8, 2, 4, 0};
    assert(obd::parseHyundaiBody("62BC03FFEE206308020400\r>", body) == obd::Parse::Valid);
    assert(!std::memcmp(body, expectedBody, sizeof(body)));
    assert(obd::parseHyundaiBody("22BC03\r00B\r0:62 BC 03 FF EE 20\r1:63 08 02 04 00\r>", body) == obd::Parse::Valid);
    assert(!std::memcmp(body, expectedBody, sizeof(body)));
    assert((body[4] & 8) && (body[5] & 2)); // Candidate bits, not vehicle-validated states.
    for (auto* bad : {"", "NO DATA\r>", "7F2211\r>", "62BC04FFEE206308020400>",
                      "62BC03FFEE2063080204>", "62BC03FFEE20630802040000>",
                      "62BC03FFEE2063080204GG>", "00B\r1:62BC03FFEE20\r0:6308020400>",
                      "00B\r0:62BC03FFEE20>", "00B\r0:62BC03FFEE20\r1:6308020400\r1:6308020400>",
                      "00B\r0:62BC03FFEE20\r1:63080204000000>",
                      "62BC03FFEE206308020400\r62BC03FFEE206308020400>"})
        assert(obd::parseHyundaiBody(bad, body) != obd::Parse::Valid);

    // Transactions are read-only, gated by a successful speed and protocol 6,
    // with routing restored before any ordinary poll. Test each failure point.
    for (int failure = -1; failure < 6; ++failure) {
        Elm327 probe;
        now = initialize(probe);
        probe.setBodyProbeEnabled(true);
        assert(!std::strcmp(probe.nextCommand(now), "010D\r"));
        assert(reply(probe, "410D00>", now) == Elm327::Result::Speed);
        probe.setDetailsActive(true); // Restoration must prioritize speed even on page two.
        now += config::kBodyProbeStartupMs;
        const char* bodyCommands[] = {"ATDPN\r", "ATSH770\r", "ATCRA778\r", "22BC03\r", "ATSH7DF\r", "ATCRA\r"};
        const char* bodyReplies[] = {"A6>", "OK>", "OK>", "00B\r0:62BC03FFEE20\r1:6308020400>", "OK>", "OK>"};
        bool sampleSeen = false;
        for (int i = 0; i < 6; ++i) {
            // Setup errors skip the read and go directly to restoration.
            if ((failure == 1 && (i == 2 || i == 3)) || (failure == 2 && i == 3)) continue;
            assert(!std::strcmp(probe.nextCommand(now), bodyCommands[i]));
            assert(!probe.nextCommand(now + 1));
            const auto result = reply(probe, i == failure ? (i == 0 ? "A7>" : "NO DATA>") : bodyReplies[i], now + 10);
            if (result == Elm327::Result::BodySample) {
                sampleSeen = true;
                assert(!std::memcmp(probe.bodyData(), expectedBody, sizeof(expectedBody)));
            }
            if (failure == i) assert(result == Elm327::Result::BodyError);
            now += 100;
            if (failure == 0 || (i == failure && i >= 4)) break;
        }
        if (failure >= 4) { assert(!probe.alive()); assert(!probe.nextCommand(now + 99999)); continue; }
        assert(probe.alive());
        assert(sampleSeen == (failure == -1));
        assert(!std::strcmp(probe.nextCommand(now), "010D\r"));
        assert(reply(probe, "410D00>", now + 10) == Elm327::Result::Speed);
        probe.setDetailsActive(false);
        // Completed negative replies cool down; normal speed polling continues.
        if (failure >= 0) {
            now += config::kBodyProbePollMs;
            assert(!std::strcmp(probe.nextCommand(now), "010D\r"));
            reply(probe, "410D00>", now + 10);
        } else {
            now += config::kBodyProbePollMs;
            assert(!std::strcmp(probe.nextCommand(now), "ATSH770\r"));
            assert(probe.timedOut(now + 1200) && !probe.alive()); // Never send speed with uncertain routing.
        }
    }
    {
        Elm327 probe;
        now = initialize(probe);
        probe.setBodyProbeEnabled(true); probe.setDetailsActive(true);
        assert(!std::strcmp(probe.nextCommand(now), "010D\r"));
        reply(probe, "410D00>", now);
        assert(!std::strcmp(probe.nextCommand(now + 1000), "ATRV\r"));
        reply(probe, "14.2V>", now + 1010);
        now += config::kBodyProbeStartupMs;
        assert(!std::strcmp(probe.nextCommand(now), "010D\r")); // No body burst stacked after an optional detail.
        reply(probe, "410D00>", now);
        assert(!std::strcmp(probe.nextCommand(now + config::kSpeedPollMs), "ATDPN\r"));
    }
    // No prompt at any body phase must force a reconnect, not another command.
    for (unsigned phase = 0; phase < 6; ++phase) {
        Elm327 probe;
        now = initialize(probe);
        probe.setBodyProbeEnabled(true);
        assert(!std::strcmp(probe.nextCommand(now), "010D\r"));
        reply(probe, "410D00>", now);
        now += config::kBodyProbeStartupMs;
        for (unsigned i = 0; i <= phase; ++i) {
            assert(probe.nextCommand(now));
            if (i == phase) {
                assert(probe.timedOut(now + 1200) && !probe.alive());
                assert(!probe.nextCommand(now + 99999));
                assert(probe.bodyProbeSuppressed());
                // A troublesome optional probe must not repeatedly interrupt
                // speed on every Bluetooth reconnect during the same boot.
                now = initialize(probe);
                probe.setBodyProbeEnabled(true);
                assert(!std::strcmp(probe.nextCommand(now), "010D\r"));
                reply(probe, "410D57>", now);
                assert(!std::strcmp(probe.nextCommand(now + 99999), "010D\r"));
            } else {
                reply(probe, i == 0 ? "6>" : i == 3 ? "62BC03FFEE206308020400>" : "OK>", now + 10);
                now += 100;
            }
        }
    }
    {
        Elm327 probe;
        now = initialize(probe);
        probe.setBodyProbeEnabled(true);
        assert(!std::strcmp(probe.nextCommand(now), "010D\r"));
        reply(probe, "410D00>", now);
        now += config::kBodyProbeStartupMs;
        const auto start = now;
        const char* responses[] = {"6>", "OK>", "OK>", "62BC03FFEE206308020400>"};
        for (unsigned i = 0; i < 4; ++i) {
            assert(probe.nextCommand(start + i * 250));
            reply(probe, responses[i], start + i * 250 + 10);
        }
        // Even between commands, a slow transaction cannot exceed its budget.
        assert(probe.timedOut(start + config::kBodyProbeBudgetMs));
        assert(!probe.alive() && probe.bodyProbeSuppressed());
    }
    {
        Elm327 probe;
        now = initialize(probe);
        probe.setBodyProbeEnabled(true);
        assert(probe.nextCommand(now)); reply(probe, "410D00>", now);
        now += config::kBodyProbeStartupMs;
        assert(!std::strcmp(probe.nextCommand(now), "ATDPN\r"));
        assert(reply(probe, "6>", now + config::kBodyProbeCommandTimeoutMs) == Elm327::Result::BodyError);
        assert(!probe.alive() && probe.bodyProbeSuppressed()); // Late replies cannot bypass timeout recovery.
    }
    // Real scheduler traces: each warning PID is polled on both pages, with a
    // speed reply between optional queries and no duplicate page coolant poll.
    for (bool details : {false, true}) {
        Elm327 watched;
        now = initialize(watched);
        watched.setEngineMonitorEnabled(true);
        watched.setBodyProbeEnabled(config::kHyundaiBodyProbeEnabled);
        watched.setDetailsActive(details);
        const auto end = now + 120000;
        const unsigned periods[] = {10000, 20000, 20000};
        uint64_t lastPoll[3] = {};
        unsigned countPoll[3] = {};
        bool previousWasSpeed = false;
        while (now < end) {
            const auto* command = watched.nextCommand(now);
            if (!command) { now += 10; continue; }
            assert(!watched.nextCommand(now + 1));
            if (!std::strcmp(command, "010D\r")) {
                assert(reply(watched, "410D57>", now + 60) == Elm327::Result::Speed);
                previousWasSpeed = true;
            } else {
                assert(previousWasSpeed); // No consecutive optional queries.
                previousWasSpeed = false;
                if (!std::strcmp(command, "0101\r") || !std::strcmp(command, "0105\r") || !std::strcmp(command, "ATRV\r")) {
                    const int index = !std::strcmp(command, "0101\r") ? 0 : !std::strcmp(command, "0105\r") ? 1 : 2;
                    assert(watched.monitor() == index && watched.metric() == -1);
                    if (countPoll[index]) assert(now - lastPoll[index] >= periods[index] && now - lastPoll[index] <= periods[index] + 1000);
                    lastPoll[index] = now; ++countPoll[index];
                    const auto result = reply(watched, index == 0 ? "410100000000\r410181076500>" : index == 1 ? "41057D>" : "14.2V>", now + 60);
                    assert(result == Elm327::Result::Monitor);
                    assert(watched.value() == (index == 0 ? 0x81 : index == 1 ? 85 : 14200));
                    assert(watched.speed() == 87);
                } else {
                    assert(details && watched.monitor() == -1 && watched.metric() >= 2);
                    const char* response = !std::strcmp(command, "0114\r") ? "41148000>" :
                        !std::strcmp(command, "0115\r") ? "41158000>" : nullptr;
                    assert(response); // Also excludes every disabled Hyundai header/data request.
                    assert(reply(watched, response, now + 60) == Elm327::Result::Detail);
                }
            }
            now += 60;
        }
        assert(countPoll[0] >= 10 && countPoll[1] >= 5 && countPoll[2] >= 5);
    }
    {
        Elm327 watched;
        now = initialize(watched);
        watched.setEngineMonitorEnabled(true);
        assert(!std::strcmp(watched.nextCommand(now), "010D\r"));
        reply(watched, "410D57>", now);
        now += config::kSpeedPollMs;
        assert(!std::strcmp(watched.nextCommand(now), "0101\r"));
        assert(reply(watched, "NO DATA>", now + 10) == Elm327::Result::MonitorError);
        assert(watched.alive() && watched.speed() == 87 && watched.monitor() == 0);
        now += 60;
        assert(!std::strcmp(watched.nextCommand(now), "010D\r"));
        reply(watched, "410D57>", now);
        now += 1500;
        assert(!std::strcmp(watched.nextCommand(now), "0105\r"));
        assert(reply(watched, "41057D\r41057E>", now + 10) == Elm327::Result::MonitorError);
        assert(watched.alive() && watched.speed() == 87);
        now += 60;
        assert(!std::strcmp(watched.nextCommand(now), "010D\r"));
        reply(watched, "410D57>", now);
        now += 1500;
        assert(!std::strcmp(watched.nextCommand(now), "ATRV\r"));
        assert(reply(watched, "NO DATA>", now + 10) == Elm327::Result::MonitorError);
        assert(watched.alive() && watched.speed() == 87 && watched.monitor() == 2);
        now += 60;
        assert(!std::strcmp(watched.nextCommand(now), "010D\r"));
        reply(watched, "410D57>", now);
        // Skip ahead to the overdue MIL, then exercise its timeout path.
        now += config::kMilPollMs;
        assert(!std::strcmp(watched.nextCommand(now), "0101\r"));
        assert(watched.timedOut(now + config::kEngineMonitorTimeoutMs) && !watched.alive());
    }
    for (int speedKmh : {0, 9, 10, 87}) {
        Elm327 probe;
        now = initialize(probe);
        probe.setBodyProbeEnabled(true); // Explicitly exercise the otherwise-disabled future gate.
        assert(probe.nextCommand(now));
        char response[16]; std::snprintf(response, sizeof(response), "410D%02X>", speedKmh);
        reply(probe, response, now);
        const auto* command = probe.nextCommand(now + config::kBodyProbeStartupMs);
        assert(command && !std::strcmp(command, speedKmh < 10 ? "ATDPN\r" : "010D\r"));
    }
    // Quantify the scheduler cost against speed-only, with identical synthetic
    // 60 ms replies. These are model measurements, not car-side timings.
    for (bool monitoring : {false, true}) {
        Elm327 benchmark;
        now = initialize(benchmark);
        benchmark.setEngineMonitorEnabled(monitoring);
        const auto start = now, end = now + 120000;
        std::vector<uint64_t> samples;
        unsigned optional = 0;
        while (now < end) {
            const auto* command = benchmark.nextCommand(now);
            if (!command) { now += 10; continue; }
            const bool speed = !std::strcmp(command, "010D\r");
            const char* response = speed ? "410D57>" : !std::strcmp(command, "0101\r") ? "410100000000>" :
                !std::strcmp(command, "0105\r") ? "41057D>" : !std::strcmp(command, "ATRV\r") ? "14.2V>" : nullptr;
            assert(response);
            reply(benchmark, response, now + 60);
            if (speed) samples.push_back(now + 60); else ++optional;
            now += 60;
        }
        uint64_t maxGap = 0;
        for (size_t i = 1; i < samples.size(); ++i) maxGap = std::max(maxGap, samples[i] - samples[i-1]);
        std::printf("SCHEDULER_MODEL monitoring=%d duration_ms=%llu reply_ms=60 speed_samples=%zu optional=%u max_speed_gap_ms=%llu rate_hz=%.3f\n",
                    monitoring, static_cast<unsigned long long>(end-start), samples.size(), optional,
                    static_cast<unsigned long long>(maxGap), (samples.size()-1)*1000.0/(samples.back()-samples.front()));
        assert(maxGap <= (monitoring ? 420u : 310u));
    }
    std::puts("PASS: strict PIDs, multi-ECU MIL, 10/20/20s monitoring, speed priority, voltage, optional recovery, body speed gate");
}
