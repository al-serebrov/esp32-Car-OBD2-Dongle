# Diagnostics ride — 2026-10-30

Both exports completed. Firmware 0.3.11, car session boot 191, discovery
session 1. The rolling snapshot contains 7,933 consecutive records,
sequences 174135–182067, with LOG_BEGIN/LOG_END framing.
The durable report has DIAG_REPORT_BEGIN/DIAG_REPORT_END framing. No firmware
changes or flash erases were performed during this export.

- Raw log SHA-256: `293e73a32d7543dd3c9141999dcacc011ac3fada12e9afabe32e42c76cc2d5a7`
- Durable report SHA-256: `a4320bcea924493a1bb73f9219d1fcce59e90557ee3e619e3e3e4a6c20ec2d5a`
- Discovery active: 317.057 seconds; initial sweep completed.
- Local OBD-speed trip: 760 m, maximum 39 km/h, average 8.64 km/h over valid
  sample coverage. These are local statistics, not dashboard Trip A/B.
- 37 Mode 01 PIDs returned successful readings; 55 were not advertised in the
  discovered banks. One advertised PID returned conflicting numeric replies.
- Confirmed/pending/permanent DTC requests all reported zero codes; MIL stayed off
  in the captured replies. This covers those OBD requests, not every vehicle ECU.
- The report survived subsequent boots: boot 192 and 193 both logged
  DIAG_REPORT_LOAD valid=1, bytes=7608, error=ESP_OK.

## Standard data we can build screens around

| Data | Successful requests |
| --- | --- |
| Speed, engine RPM, calculated load | 010D, 010C, 0104 |
| Coolant, intake-air temperature, ambient temperature | 0105, 010F, 0146 |
| Intake manifold and barometric pressure | 010B, 0133 |
| Throttle, relative throttle, throttle B, accelerator positions D/E, commanded throttle | 0111, 0145, 0147, 0149, 014A, 014C |
| Fuel-system status, short/long fuel trim B1, ignition timing, evaporative purge | 0103, 0106, 0107, 010E, 012E |
| Oxygen sensor presence and B1S1/B1S2 voltages | 0113, 0114, 0115 |
| Catalyst temperature channels | 013C, 013E |
| ECU voltage; adapter voltage separately | 0142; ATRV |
| Runtime, distance with MIL, warmups since clear, time with MIL, time since clear | 011F, 0121, 0130, 014D, 014E |
| MIL/readiness, OBD standard, current-cycle readiness | 0101, 011C, 0141 |
| Absolute load, commanded equivalence ratio | 0143, 0144 |
| Additional raw replies requiring length/decoder review | 0155, 0156 |

Names describe standard PID meanings. Success proves that these requests return
consistent frames in this run; raw-only/variant fields still need decoder checks
before display. Oil temperature, fuel level and fuel-rate standard PIDs were not
advertised; this does not rule out manufacturer-specific alternatives.

PID 0131 (distance since codes cleared) replied with differing values from more
than one responder, e.g. 198 versus 199 km. The parser correctly rejected an
ambiguous value. A future targeted query could identify each ECU's value.

## Four positive Hyundai blocks were rejected by our parser

The firmware required the final indexed multi-frame line to contain exactly the
remaining declared payload bytes. This adapter includes the remainder of the CAN
frame, padded with AA or 00. The indexed frame order and declared byte counts
allow the saved replies to be reconstructed offline. This is a firmware decoder
bug, not evidence that these four requests are unavailable. Raw RX fragments are
preserved in full for every recovered sample.

| Target/request | Recovered replies | Actual payload bytes | Padding |
| --- | ---: | ---: | --- |
| 7E1 / 21A0 | 5 | 28 | Four AA bytes |
| 7E1 / 21A1 | 5 | 50 | Three AA bytes |
| 7E0 / 2101 | 4 | 38 | One 00 byte |
| 7E0 / 2103 | 4 | 9 | Two 00 bytes |

The recovered JSON contains each request time, prior speed sample, raw response,
trimmed payload and discarded padding. These samples can be reanalysed without
another ride. Production firmware is still 0.3.11; its decoder has not been
changed or reflashed as part of pulling the logs.

## Candidate meanings and limits

- **21A0 at transmission ECU 7E1 is promising.** Published offsets give RPM values
  of 852, 867, 1151, 1656 and 667. Its speed byte reads 1, 0, 15, 20 and 0 km/h,
  approximately tracking surrounding speed readings (sample times differ).
  The candidate ATF byte minus 40 gives 32, 34, 35, 37 and 38 C.
- Its candidate current-gear byte is **14, 0, 2, 2, 0** and commanded-gear byte
  **0, 0, 2, 2, 0**. Seeing 2 while moving is promising, but 14 at low speed and
  the selector representation need validation. Do not turn these into an
  asserted D2/R/P display yet. No PRND mapping was established.
- **21A1 responds**, but the published candidate gear offset remains zero in all
  five samples, including while moving. That definition is not validated here.
- **2101 responds** with changing engine data. Its published candidate oil
  temperature offset gives 33, 39, 42.75 and 48 C. Plausible progression alone
  does not establish that this byte is oil temperature on this ECU.
- **2103 responds**, but its nine-byte payload cannot contain the published
  odometer offset at byte 57. This definition does not apply to this reply.
- Transmission 2201A0/2201A4 and engine 21A0 received negative responses. The
  TCM's 22 requests returned 7F2211 (service not supported).
- Brake/belts/doors, reverse/rear belts, light switch, brake lights, indicators,
  headlights and cluster odometer/fuel candidates returned NO DATA for the
  addresses/requests tried. No live body-state mapping was found.
- VIN replies contain FF placeholders. Even after frame reconstruction these
  are not a usable VIN; do not classify this as just the padding bug.

The candidate offsets were taken from the [OBDb Tucson definitions](https://github.com/OBDb/Hyundai-Tucson/blob/main/signalsets/v3/default.json)
which span several model generations. Frame recovery, response availability and
semantic validation are separate conclusions.

## Speed continuity and device health

There were 871 valid speed replies in the car boot. Median
inter-sample gap was 323 ms and the maximum was
1094 ms, below the 1500 ms stale threshold. No adapter errors,
monitor errors, Bluetooth retry/drop events or log-queue-overflow events were
found in this car session. OBD worker stack headroom was at least 1652 bytes in
logged samples; the event queue was empty in those checks. App stack headroom
was 1112 bytes. These are observed samples, not a guarantee for every workload.

## Follow-up

Fix padded final-frame handling using these recorded responses as regression
fixtures, then validate the 21A0 gear/selector encoding and temperature offsets.
No need to repeat the ride merely to recover these already-recorded replies.
Further vehicle testing is needed to confirm their meaning and faster sampling
would help observe gear transitions.
