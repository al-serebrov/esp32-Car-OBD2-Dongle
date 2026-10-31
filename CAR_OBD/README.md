# ESP32 OBD speedometer

PlatformIO + ESP-IDF 5.5.0, original ESP32 DevKit V1, one 240×240 GC9A01 SPI TFT,
Vgate iCar Pro Bluetooth 3.0 / `V-LINK`. Source version: **0.4.1**.
The car-verified firmware is 0.2.1. GPIO4 brightness control was visually confirmed
in 0.2.3. Version 0.2.4 removes the temporary loop and percentage overlay and
holds brightness at 100% after startup.
Version 0.3.0 adds a GPIO21 button, day/night themes and an engine-data page.
Version 0.3.1 enlarges speed digits from 60 to 120 pixels high, removes the
small dial numbers/ticks and SPEED/DEMO/LIVE labels, and adds a 10x10 round
status light: yellow for demo, green for live. The arc and KM/H unit remain.
Version 0.3.2 centers readings that start with 1 by their visible width, removing
the leading digit's 44-pixel blank prefix. The following digits retain fixed
positions throughout 100..199, so changing readings do not reflow those cells.
Version 0.3.3 adds smooth Roboto Condensed Bold speed numerals and replaces the
moving demo sweep with 11, 22, 33, 44, 55, 66, 77, 88, 99, 111, holding each for
three seconds and repeating every 30 seconds. Demo restarts at 11 when a live
speed becomes unavailable. Real OBD speed retains its normal update rate.
Version 0.3.6 reduces digits to 96 pixels high for a warning-layout prototype,
with permanently lit sample Check Engine and parking-brake icons below the unit.
Version 0.3.7 adds overheat and seatbelt sample icons in a two-column, two-row
grid and reduces the speed digits to 76 pixels high.
Warnings fill slots in activation order: left, right, then the next row. Clearing
a warning closes the gap; reactivation appends it after the remaining warnings.
Version 0.3.8 adds a targeted Hyundai body-data experiment that logs responses
and candidate bits, without interpreting them as verified vehicle warnings.
Version 0.3.9 disables that experiment and sample icons, adding live Check Engine
and coolant monitoring on both pages with a three-second target interval per PID.
Version 0.3.10 restores all five sample icons in DEMO, adds a battery-voltage
alert, and changes background polling to 10 seconds for MIL and 20 seconds each
for coolant and voltage. Disabled body probes may start only below 10 km/h if
explicitly enabled in a future build.
Version 0.3.11 adds an opt-in third diagnostics page and a durable discovery report.
Version 0.3.12 restores large speed digits when there are no warnings and resizes
the speed/unit/icon layout with the number of occupied warning rows.
Version 0.3.13 fixes padded multi-frame decoding and focuses the third page on
the responsive Hyundai transmission/engine candidates from the first ride.
Version 0.4.0 adds the driving page and hollow pulsing MIL overlay.
Version 0.4.1 moves speed-page warnings into side columns and keeps the largest
120px speed reading, with no resizing when warnings appear or clear.
The current seven-pin display has no LED/BL pin; GPIO4 is disconnected.
The owner observed that GC9A01 SPI brightness commands changed the test label
but had no effect on light output. The selected environment is now
`esp32doit-devkit-v1-image-dimming`, which scales rendered RGB intensity.
It does not reduce LED backlight brightness, power or black-level glow.
The owner visually confirmed image dimming on 2026-10-28. The test cycle has
been stopped; normal operation starts at 100%, with hold-to-toggle 50%/100%.
The previous `esp32doit-devkit-v1-backlight` profile retains GPIO4 PWM, and the
base `esp32doit-devkit-v1` profile leaves brightness control disabled.

## Current pages (0.4.1)

Double-click cycles **speed → driving → engine data → diagnostics → speed**.
Single click toggles day/night, and holding the button past one second toggles
100%/50% image brightness. Startup remains the original speed page.

The new driving page implements the approved prototype:

- Upper-left engine-load arc, teal, filling from the left toward the top.
- Upper-right throttle-position arc, blue, filling from the right toward the top.
- Curved LOAD/THROTTLE labels and independent 0–100% readings.
- Smooth centred speed, gear below (P/R/N/D1–D6), then centred RPM with a small
  RPM unit beside it. The unit does not offset the number.
- Smaller coolant and adapter-voltage readings on either side of the gear.
- The same live/demo status dot and theme/brightness controls as the speed page.

DEMO uses the approved example: 88 km/h, D4, 2600 RPM, load 38%, throttle 24%,
coolant 85 C and 13.8 V, with a yellow dot. LIVE uses actual readings. Missing
or stale secondary values show `--`; D with an unknown forward gear shows D,
and an unknown selector code shows `--`. These placeholders do not turn an
otherwise valid speed into DEMO. Secondary RPM/load/throttle/gear data expires
after five seconds; coolant and voltage retain their existing monitor lifetimes.

Standard requests 010C (RPM), 0104 (calculated load), and 0111 (throttle position,
not accelerator-pedal position) are interleaved with speed and the read-only
7E1/7E9 21A0 transaction for the driver-validated Hyundai LM transmission. Byte
20 maps 8=D, 9=N, 10=R, 11=P; byte 21 supplies forward gears 1–6. Unknown values
remain unknown. This manufacturer-specific mapping is for the tested car.

Only the driving page starts these extra polls. A successful speed reading
separates optional transactions, and MIL/coolant/voltage monitoring retains
priority. Route setup/read/restore stays atomic even if the page changes. Gear
reads are bounded by the 1.2-second routing budget; missing prompts force a
reconnect and quarantine the gear probe until reboot. Prompted malformed gear
replies retry after a short delay; negative/unavailable replies wait longer.
All requests share the adapter, so the richer page has a slower speed cadence
than the original speed-only workload. In a 60 ms reply model, the driving page
obtained each extra reading about every 2.2 seconds and the maximum speed gap
was 790 ms. Actual ECU timing may differ.

When the ECU commands the Check Engine/MIL on, a **large hollow engine outline**
is blended over any page. Only the outline pulses: 40% → 75% → 40% opacity over
2 seconds. The dashboard and icon interior stay visible at their original
brightness. Blending happens during display transfer; no second framebuffer is
allocated and no pulse is painted into the source pixels. Clearing the alert
restores the underlying UI without trails. Synthetic DEMO warning icons do not
activate this full-screen overlay. Existing live warning freshness/latching
rules remain in force.

The curved labels/static artwork and four-bit dynamic font masks are generated
from the approved prototype's Roboto Condensed Bold font. Regenerate with
`python3 tools/generate_driving_assets.py`; normal builds use the checked-in
header and require no Python imaging runtime. Glyph assets reside in flash.

USB bench commands include `page 1` through `page 4` and `overlaytest on/off`.
The overlay test is only a visual preview; it does not set/clear ECU codes and
starts disabled after reboot. Existing `logs`, `diagreport`, brightness and
manual dimming commands remain available. The previous NVS diagnostics report
format and data partition are unchanged.

The older diagnostics procedures below refer to earlier page positions. In
0.4.0 diagnostics is **page four** (three double-clicks from speed); engine data
is page three. The focused candidate profile otherwise retains its behavior.

## Behavior

1. Boot into the **DEMO** speedometer: 11, 22, 33, ... 99, 111, three seconds per value.
2. Discover nearby **discoverable Bluetooth Classic** devices and log their
   advertised names, MAC addresses and RSSI when available. This is not a BLE scan
   or an inventory of every powered Bluetooth device.
3. Match the name `V-LINK`, discover its SPP serial service, and connect with
   authenticated legacy PIN pairing (`1234`). A complete inquiry is allowed to
   finish so other devices are included in the log. Retry with 3/6/12/24/30-second
   backoff when discovery, pairing or connection fails.
4. Initialize ELM327 using `ATZ`, `ATE0`, `ATL0`, `ATS0`, `ATH0`, `ATSP0`, then
   request the supported-PID bitmap with `0100`. Every ASCII command ends with
   carriage return; only one request is outstanding until the `>` prompt arrives.
   If several controllers return different valid `0100` bitmaps, their supported
   bits are combined. Numeric measurement replies still have to agree; MIL
   status has a separate any-commanded-on aggregation rule.
5. Request standard Mode 01 speed with `010D`. A complete, valid `41 0D A`
   response supplies speed directly as unsigned byte A in km/h. For example,
   `41 0D 57` is 87 km/h; `41 0D 00` is a valid stationary reading.
6. Switch the status light to **green** only after receiving valid speed. If the connection closes,
   a speed response is invalid, or the reading is at least 1.5 seconds old, return
   to **demo with a yellow status light**. Never show stale speed with a green light.

The speed page keeps its largest **120px-high** smooth reading with warning
icons in two side columns. Each column has three 24x20px slots, filled in
activation order: upper left, upper right, then the next pair down. Clearing
one warning closes the gap. Speed and KM/H keep the same size and position
regardless of the number of warnings, with a fixed gap between icons and digits.
Readings through 199 km/h retain the original glyph width; wider 200–255 readings
fit horizontally into the central strip while retaining the same 120px height.
The outer speed arc and status dot remain in place.

DEMO shows synthetic Check Engine, parking-brake, overheat, seatbelt and battery
icons, like a bulb-check preview. LIVE uses measured Check Engine, temperature
and low-voltage warnings. Only the side slots redraw when warning state changes;
speed updates leave those slots intact. The complete frame is composed in memory
before transfer, without an intermediate blank display.
Normal live parking-brake and seatbelt polling/display remain disabled pending an
LM-specific definition. The opt-in diagnostics page may probe these candidates for logs only. Switching to LIVE clears all demo samples first.
The renderer's `setWarning(icon, active)` updates a compact, activation-ordered
list. Repeated active readings keep their position, and order persists across
theme/page switches. Clearing one icon closes the gap; reactivation appends it.

### Check Engine, coolant and voltage monitoring

Background requests run on all four pages:

| Reading | Request | Target interval |
| --- | --- | --- |
| Check Engine / reported fault count | `0101` | 10 seconds |
| Coolant | `0105` | 20 seconds |
| Adapter supply voltage | `ATRV` | 20 seconds |

Initial requests are staggered by 1,500 ms after the first speed. A successful speed reply
precedes each optional request; another speed request follows it. Commands are
serialized, so the actual start times can slip with response latency and speed
priority. There are no duplicate page-specific voltage/coolant queries. The
normal speed cadence still uses a 250 ms post-response pause, typically giving
roughly 0.3-second updates. Optional requests add response time and a short pause
to individual speed gaps. The display task runs separately, but the adapter
does not execute OBD requests in parallel. Longer intervals reduce interference
without changing measurement accuracy; a new coolant/voltage condition may take
about 20 seconds plus scheduling/response time to appear.

The Check Engine icon follows PID 01 byte A bit 7, not merely a nonzero fault
count. Multiple valid responders are combined with any-MIL-on logic; differing
readiness bytes are expected. The logs retain the maximum reported DTC count
and number of response lines. With headers off, this is not a count of unique
codes or unique ECUs. Reading actual code lists (Mode 03) is not implemented.
This reflects commanded MIL state, not the dashboard bulb-test/flash behavior.
See the [OBD inspection flow, pages 8–9](https://training.wep-inc.com/GenTraining/MoGVIPInspectorTraining/story_content/external_files/OBD%20IM%20Flowchart.pdf).

Coolant is decoded as A minus 40°C. The configurable early-temperature alert
turns on at **105°C** and clears at **100°C**, providing hysteresis. These are
project thresholds, not a verified Hyundai factory overheat threshold.
See [coolant decoding](https://docs.emtronaustralia.com.au/displays/functions/obd2/index.html).

The battery icon turns on strictly below **12.6 V**, clearing at **12.8 V** to
avoid threshold chatter. This uses the adapter's ATRV supply measurement, not a
direct reading of the car's alternator/charge warning lamp or a battery-health
diagnosis. The chosen threshold also applies with the engine stopped if speed
data remains LIVE. See [ELM voltage command](https://www.elmelectronics.com/wp-content/uploads/2016/07/ELM327DSH.pdf)
and [DENSO charging-system diagnostics](https://www.densoautoparts.com/charging-system-diagnosis/).

Missing/invalid warning readings are UNKNOWN. Age limits are 30 seconds for MIL
and 60 seconds for coolant/voltage, allowing three poll periods. While speed
remains LIVE, an already illuminated warning stays on until a fresh reading
clears it. Entering DEMO resets real-alert latches and restores the sample icons. The connection dot still
indicates valid speed only, not successful monitoring of every sensor. Warning
state/visibility changes, MIL responses, coolant/voltage values and errors are logged.
Prompted monitor failures retry after their respective interval without invalidating speed;
a missing/late prompt has a one-second deadline and requires resynchronization
via reconnect. No code clearing or control commands are issued.

The selected Car Scanner profile is **Hyundai Tucson LM, ix35 EL 2.0/2.4 MPI 2WD
(2009–2015)**. This identifies the intended vehicle for future enhanced-diagnostic
research. A profile name alone does not establish compatible diagnostic requests.

### Hyundai handbrake/seatbelt data experiment

**Disabled:** `config::kHyundaiBodyProbeEnabled=false`. No body-data or
header/filter probe commands are sent. The following describes the retained
experimental implementation if explicitly enabled in a future build.

The flag enables a bounded read-only probe for
`22BC03` at CAN request ID `770`, response ID `778`. The
[OBDb Hyundai Tucson definition](https://github.com/OBDb/Hyundai-Tucson/blob/main/signalsets/v3/default.json)
documents this request and candidate handbrake/driver-buckle bits on newer
vehicles. **It is not verified on the 2012 ix35 LM.** No guessed LM-specific
addresses, actuator tests, diagnostic-session changes or configuration writes
are sent. Adapter header/filter commands only change the ELM327 routing.

If re-enabled, after at least ten seconds from connection, a successful speed
response and a speed strictly below 10 km/h,
the scheduler can perform this transaction instead of its next ordinary poll:

```
ATDPN       # Require protocol 6 / A6: CAN 11-bit, 500 kbit/s (once per connection)
ATSH770
ATCRA778
22BC03
ATSH7DF     # Restore standard functional request header
ATCRA       # Restore default receive filtering
010D        # Speed before another optional request
```

The existing ATZ defaults supply CAN auto-formatting and flow control. The
parser accepts only `62 BC 03` plus exactly eight data bytes, as a complete
headerless line or the ELM's `00B`, `0:`, `1:` multi-line format. Raw TX/RX,
`BODY_CANDIDATE` and `BODY_PROBE_ERROR` records identify the request, full data,
candidate handbrake and driver-buckle bits, and `mapping_verified=0`.
Candidate body bits never update LIVE warning icons. The current Check Engine,
temperature and battery icons use the independent monitoring described above.

A successful probe repeats no sooner than five seconds after restoration.
Prompted read/setup failures back off for sixty seconds while speed continues.
Each command has a 600 ms deadline and the whole transaction a 1,000 ms budget;
there are no overlapping requests. A missing/late prompt, response overflow or
failed routing restoration requires reconnection and suppresses body probes for
the rest of that boot. Different CAN/ISO protocols are not probed using these
headers. Slow optional requests can briefly reduce speed update frequency; stale
speed still triggers the normal DEMO fallback.

For field validation, with the car stationary and ignition on, hold the foot
brake while toggling the handbrake, then fasten/unfasten the driver's belt in
separate phases, several times, holding each state for at least 15 seconds.
Record the order and approximate durations. Return the ESP32 for log extraction.
Also check whether Car Scanner exposes these two changing live sensors with
the selected profile. A positive response alone is not enough to validate its
bit mapping; unsupported/no-data responses leave the states unknown.

The 2026-10-30 car test returned **NO DATA on both 22BC03 attempts**. Normal speed
polling and routing restoration worked, but no brake/belt mapping was validated.
See `docs/validation/hyundai-body-car-test-2026-10-30-analysis.md`. The next step
is a working LM-specific sensor definition or Car Scanner ELM capture, rather
than repeating the same candidate with different toggle timing.

The engine-data page (now third) shows adapter supply
voltage, coolant temperature and two Bank 1 oxygen-sensor voltages. There is no
Wi-Fi, phone interface, RPM or SD feature. The numeric speed supports the full
OBD byte range 0–255; the configurable arc saturates at
160 km/h. Bluetooth/OBD work runs separately from display drawing. No ECU control,
code clearing, configuration writes or actuator commands are sent.

The first vehicle-protocol search has a 30-second response deadline; reset has
5 seconds and ordinary commands have 3 seconds. Complete error responses can be
retried without resetting the adapter. A missing prompt requires reconnection
so a delayed reply cannot be mistaken for the next request. `ATZ` is sent once
per new SPP session, not on every poll. Successful speed polls are separated by
at least 250 ms, so actual rate depends on ECU/adapter response latency.

## Button, themes and pages

Wire a normally open momentary button between **GPIO21 and GND**. The firmware
enables an internal pull-up; do not connect the button to a supply rail. For a
four-leg button use a pair of contacts that only connects when pressed.

- Single click: toggle day (black numbers, white background) / night (white
  numbers, black background). All pages use the selected theme.
- Double click: cycle speed / driving / engine data / diagnostics, with no theme change.
- Hold past one second: toggle 100% / 50% brightness once per hold. Releasing
  a held button does not also produce a click.

Input is sampled in a separate task every 10 ms, with 30 ms debouncing and a
300 ms second-click window. Single clicks are applied after that window.
A button held during boot must first be released. Startup remains night mode,
speed page, 100% brightness; choices are not persisted across power cycles.
The button does not automatically adjust brightness when changing theme.

The engine-data layout is:

| Upper left | Upper right |
| --- | --- |
| Adapter supply voltage (V) | Coolant temperature (C) |
| Lambda 1 / B1S1 voltage (V) | Lambda 2 / B1S2 voltage (V) |

Oxygen-sensor requests run only while this page is selected, alternating with
speed over the serialized session. Voltage and coolant use the background
20-second monitors on every page:

- `ATRV`: adapter input-voltage reading, displayed to 0.1 V. This is the adapter's
  supply measurement, not an ECU PID or a calibrated battery measurement.
- `0105`: coolant, `A - 40` degrees Celsius.
- `0114` / `0115`: conventional B1S1 / B1S2 oxygen-sensor voltages, `A * 5` mV;
  the second response byte is fuel trim and is not shown. These are voltages,
  not equivalence ratios, and do not represent two different cylinder banks.

References: [ELM input-voltage command](https://www.elmelectronics.com/wp-content/uploads/2016/07/ELM327L_Data_Sheet.pdf),
[SAE J1979 sensor definitions](https://law.resource.org/pub/us/cfr/ibr/005/sae.j1979.2002.pdf),
[Emtron OBD decoding documentation](https://docs.emtronaustralia.com.au/displays/functions/obd2/index.html).

Missing, invalid or stale tile readings display `--`; voltage/coolant age out
after 60 seconds and oxygen sensors after 10 seconds. There are no
simulated engine-data values. A failed optional request invalidates only its
own tile and receives a 30-second retry cooldown for oxygen sensors.
Voltage/coolant monitor errors retry after 20 seconds. Speed polling continues;
ordinary prompt-less timeouts still require reconnecting to resynchronize the
adapter. Returning to the speed page stops new detail requests after any
already-outstanding command completes; background MIL/coolant/voltage polling continues.
The speed page's 1.5-second freshness rule is unchanged. Version 0.3.10's monitoring
and warning behavior still need a car-side field test.

## Field test without the PC

1. Flash this firmware at the desk using the instructions below.
2. In the parked car, plug in/power the V-LINK adapter and power the ESP32 via USB.
3. Turn ignition on. Disconnect other OBD phone applications from the adapter.
4. Leave the ESP32 running for about 1–2 minutes for discovery, pairing and ECU
   protocol selection. With a valid stationary speed response, the gauge reads
   **0 km/h with a green light**. Yellow means it has not obtained a current valid speed.
5. You may unplug the ESP32 and bring it back to the PC. Committed flash logs
   survive power loss and subsequent boots. A short tail still queued in RAM can
   be lost on abrupt power removal; earlier committed records remain readable.
6. **Export the logs before reflashing, erasing flash, or running a long new test.**
   Reconnecting USB does not clear the old session, but new events consume space.

The first car logs confirmed V-LINK discovery, authentication, SPP connection and
ELM initialization. Version 0.2.0 stopped at differing supported-PID bitmaps;
0.2.1 accepts those maps and proceeds to `010D`. A follow-up field test confirmed
1,936 valid speed readings over the retained 10-minute window. See the
[car-test diagnosis](docs/validation/car-test-2026-10-27-analysis.md) and
[successful live test](docs/validation/car-live-test-2026-10-27-analysis.md).

## Focused candidate ride (third page, version 0.3.13)

The third page now runs **CANDIDATES**, selected by `kCandidateDiagnostics=true`.
The broad discovery profile described below remains available in source, but is
not the active ride workload. Double-click three times from speed to enter diagnostics;
leaving that page stops new probes after in-flight routing is restored. Other
button gestures and the speed/warning layout remain unchanged.

The focused schedule uses only the blocks that returned positive replies in
boot 191 of the first ride, plus two standard references:

| Target/reply | Request | Purpose |
| --- | --- | --- |
| 7E1/7E9 | 21A0 | Priority transmission block: raw current/commanded gear, candidate ATF, RPM/speed correlation |
| 7E1/7E9 | 21A1 | Alternative transmission block; full bytes retained, published gear offset unverified |
| 7E0/7E8 | 2101 | Engine block with candidate oil-temperature byte |
| 7DF | 010C, 010F | Standard engine RPM and intake temperature for comparison |

Normal speed polling and MIL/coolant/voltage monitoring still run. No capability
sweep, full standard-PID sweep, VIN/DTC-list discovery, failed body/lighting
requests, rejected service-22 transmission requests, or short 2103 odometer
candidate is scheduled by this profile. In particular, lights/handbrake/belt
support has not been established by reusing unrelated byte offsets.

The eight optional slots are `21A0 → RPM → 21A0 → 21A1 → 21A0 → 2101 → 21A0 → IAT`.
Successful primary transmission reads have a 500 ms minimum repeat delay;
other selected reads have a 1 s minimum. These are eligibility delays, not fixed
sample periods. One speed request still separates every optional transaction.
The old one-second discovery gap is removed only in this focused profile.
Failures wait 60 seconds; timeout/routing failures quarantine that item until
reboot and restore the default route by reconnecting. Original per-command and
1.2-second total routing budgets remain.

With synthetic 60 ms replies over five minutes, the focused model produced
218 primary TCM samples, 54 alternative TCM samples, 54 engine-block samples,
55 RPM references, 54 IAT references and 495 speed samples. The largest speed
gap was 790 ms. Real V-LINK/ECU timing may differ. This faster capture trades
some normal speed cadence for candidate samples while page three is selected.

### What the page and logs mean

The page shows `GEAR? raw / CMD? raw`, `ATF?` and `OIL?`, with sample count and
elapsed time. Question marks mean **unverified candidates**, not confirmed D1/D2,
PRND or temperature readings for this car. Gear/ATF values expire five seconds
after their last usable sample; the oil candidate expires after 20 seconds.
No experimental value feeds the normal speed-page warning logic.

`DIAG_RESULT`, `DIAG_DATA` and `DIAG_FIELD` retain every candidate result and
payload. `TCM_CANDIDATE` adds raw gear/commanded bytes, candidate ATF Celsius,
TCM RPM and speed. `ENGINE_CANDIDATE` records candidate oil temperature in quarter
degrees Celsius (`raw*3 - 192`). `CANDIDATE_CONTEXT` records surrounding standard
speed/RPM/coolant/IAT values, validity and age to support comparison afterward.

Every speed value remains logged. During focused capture only, repetitive
successful 010D TX/RX text is suppressed; malformed replies are preserved as
`RX_SPEED_ERROR`. Candidate queries, route setup/restoration and their raw
responses stay logged. This extends the finite rolling-log window while keeping
the candidate evidence. The log policy is announced at activation.

The durable 7608-byte report remains compatible with the previous report layout.
A profile tag in the previously unused record zero distinguishes CANDIDATES from
DISCOVERY. In a focused report the unused PID-support bitmap stores all 256
possible primary raw-gear values observed; export lists these as
`DIAG_GEAR_RAW_VALUES`. First/latest/changed-byte samples and success/failure
counts remain retained for each responsive block. Unselected entries are marked
NOT_SELECTED internally and omitted from export. The previous ride's NVS report
survives the update; a new report starts only after valid car speed while the
third page is selected. The earlier full export is saved separately in this repo.

### Procedure for the next ride

1. Disconnect other phone OBD apps and select the third screen while parked.
   The first candidate samples should start within seconds after valid OBD speed,
   rather than waiting for the broad capability sweep.
2. Leave diagnostics selected during the short drive. No screen interaction is
   needed while moving.
3. **At the end, while safely stationary with the foot brake firmly held**, pause
   approximately 10 seconds in each selector position P, R, N and D if practical.
   Note the on-screen elapsed times and raw gear/commanded values at each position.
   This is a comparison exercise; the device never controls the transmission.
4. Doing this check at the end keeps the relevant transitions near the end of
   the finite raw log if earlier driving data rolls over. First/latest/changed
   summaries and the observed raw-gear set remain in NVS, but do not preserve the
   timing of every transition on their own.
5. Return to the speed page while parked, wait five seconds, then remove power.
   Reconnect to the PC for export before starting another car run.

```sh
/Users/alex/.platformio/penv/bin/python tools/read_field_logs.py \
  --diagnostics --port /dev/cu.usbserial-0001 \
  --output docs/validation/candidate-ride-2026-10-30.txt
```

The padded-frame fix follows the declared message length, accepts either a
trimmed last line or a complete padded final CAN frame, and still checks the
entire line's hex, frame sequence and buffer bounds. Short/missing/extra frames
and partial extra padding remain errors. Regression fixtures contain all 21
positive multi-frame replies reconstructed from the first ride. VIN FF
placeholders are still invalid; fixing framing does not turn them into a VIN.

## Diagnostics ride (third page, version 0.3.11)

Double-click through **speed → driving → engine data → diagnostics → speed**. Selecting the
third page enables discovery and shows RECORDING after valid car speed arrives;
leave this page visible for the test. Returning to either normal page stops new
discovery requests after any in-progress routing transaction is restored. Boot
always starts on the speed page, with discovery disabled. Theme/brightness
controls retain their normal behavior. Phone OBD applications must disconnect
from V-LINK while the ESP32 uses it.

The first sweep discovers the advertised Mode 01 capability banks (00 through
E0, following the continuation bit), then reads each advertised measurement.
Speed is reused from normal polling. It also tries confirmed, pending and
permanent fault codes (03/07/0A), VIN (0902), adapter voltage, and 14 published
Hyundai/Tucson candidate blocks:

| Target/reply | Request | Candidate information |
| --- | --- | --- |
| 7E1/7E9 | 21A0, 21A1 | Actual/commanded gear, transmission temperature |
| 7E1/7E9 | 2201A0 | Gear selector (PRND raw value), actual/commanded gear, ATF |
| 7E1/7E9 | 2201A4 | Alternative current/next gear definition |
| 7E0/7E8 | 21A0, 2101 | Alternative ATF, oil temperature and other raw engine data |
| 770/778 | 22BC03, 22BC04 | Brake, belts, doors, reverse; deferred at 10 km/h or more |
| 770/778 | 22BC05, 22BC06, 22BC08, 22BC09 | Switches, brake lamps, indicators, low/high beam |
| 7C6/7CE | 22B002 | Odometer and fuel-volume candidate |
| 7E0/7E8 | 2103 | Alternative odometer candidate |

Definitions come from the [OBDb Hyundai-Tucson signal set](https://github.com/OBDb/Hyundai-Tucson/blob/main/signalsets/v3/default.json),
which includes multiple generations and unverified definitions. **A positive
reply confirms a responsive request, not that its offsets or enums apply to this
ix35.** Candidate fields stay raw and never drive the normal warning icons.
NO_DATA/negative/timeout results describe this attempt; they cannot establish
that a feature is universally unavailable. No address/PID brute force, security
access, diagnostic-session changes, DTC clearing or actuator commands are used.
Manufacturer probes require the verified 11-bit 500 kbit/s CAN protocol (6).

Speed remains interleaved, with one optional reading/transaction between speed
responses. Custom header/filter/read/restore commands run together so ordinary
requests cannot accidentally go to the wrong ECU. Normal engine monitors retain
priority. Discovery starts at most one second after the preceding discovery
reading completes; repeats become eligible after 3 seconds for successful custom
blocks, 10 seconds for successful standard readings, or 60 seconds after a
prompted failure. These are minimum delays, not guaranteed sample periods.
Repeat slots are shared between standard/custom data. Each read has a one-second
deadline, control commands 600 ms, and a complete custom transaction 1.2 seconds.
Missing prompts force a reconnect and quarantine that item until reboot. Speed
updates can therefore have larger gaps during this experiment; leaving page three
restores the usual workload. In a synthetic five-minute run with 60 ms replies,
770 speed samples were obtained and the largest gap was 790 ms; actual car/adapter
latency may differ.

`DIAG_RESULT` records contain name, request, ECU address, outcome and decoded
standard value or an explicit unverified label. `DIAG_DATA` retains parsed bytes;
`DIAG_FIELD` labels published candidate offsets. TX/RX remains logged separately.
Responses are bounded to 128 payload bytes/512 adapter-text bytes; larger or
ambiguous replies are recorded as parse errors rather than guessed.

A checksum-protected **latest-run report in NVS** survives reboot and rolling-log
wrap. It includes per-item success/failure counts, most recent positive samples,
and first/latest/changed-bit samples for custom blocks (up to 64 bytes each).
Standard samples retain eight bytes, extra fault/VIN samples 64. Long samples
are marked truncated; full accepted payloads are in the finite rolling log.
The report is checkpointed at most every two seconds when new items finish,
otherwise every 30 seconds, and on leaving discovery or losing the connection.
Writes wait for an idle request boundary. Abrupt power removal can lose the
uncommitted tail. Viewing diagnostics without valid car speed preserves the
previous saved run; a new boot's first valid speed while discovery is enabled
starts a new run. Export the previous run before another car test.

Trip distance, average speed and maximum speed are calculated locally from fresh
OBD speed samples **while discovery is enabled**. Stale gaps are excluded; this
is not a readout of the vehicle's Trip A/B counters.

Before departure, select diagnostics while parked and allow the initial sweep to
run for a couple of minutes. If practical, note the times of stationary selector,
light and belt/brake changes so raw candidates can be correlated later. Leave
the page enabled during the ride. At the end, while parked, double-click back to
speed and wait five seconds before removing power. Reconnect to the PC and export:

```sh
/Users/alex/.platformio/penv/bin/python tools/read_field_logs.py \
  --diagnostics --port /dev/cu.usbserial-0001 \
  --output docs/validation/diagnostics-ride.txt
```

This saves `diagnostics-ride.diagnostics.txt` and `diagnostics-ride.txt` without
clearing either store. A missing durable report is reported while raw export
still proceeds. Analysis should distinguish responsive/decoded standard data,
responsive but unverified custom data, and unsuccessful/deferred attempts.

## Read the retained logs

Open this **CAR_OBD** directory in a terminal. Close PlatformIO Serial Monitor
and any other application using the ESP32 serial port, then run:

```sh
/Users/alex/.platformio/penv/bin/python tools/read_field_logs.py \
  --port /dev/cu.usbserial-0001 \
  --output docs/validation/car-test.txt
```

The script uses PlatformIO's installed Python/pyserial, opens at 115200 baud,
avoids an explicit reset, requests a coherent snapshot and saves it between
`LOG_BEGIN` and `LOG_END`. It does **not** clear the flash log. A full export can
need several minutes at 115200 baud; the default timeout is 300 seconds. A
partial export is retained with an error if completion is not received. Share
the saved file for diagnosis. Port names may change; use `pio device list`.

Alternatively, open PlatformIO Serial Monitor at 115200 and send `logs` followed
by Enter. `diagreport` exports the durable discovery summary; `help` lists the commands. Normal runtime events also print to serial.
No raw-command console or erase-log command is exposed.

The source additionally accepts `brightness 0..100` when manual backlight PWM is
enabled and safely wired; see the optional test below. It reports an error in
the default 3V3 backlight configuration rather than configuring GPIO4.

Each stored line contains:

```text
sequence boot=<session number> ms=<uptime in that boot> event details
```

Events cover firmware/reset identity, Classic device discovery, target MAC,
SPP service/channel, PIN/authentication results (not the PIN itself), connection
attempts/failures/disconnects, escaped raw TX/RX chunks, PID support, decoded
speed, extra metrics, button/theme/page/brightness events, parser/timeouts,
DEMO/LIVE transitions, and periodic heap/drawing health.
`\r`, `\n`, backslashes and nonprintable bytes are escaped, so split replies can
be reconstructed. Names are bounded and sanitized. Unknown/unadvertised names
are explicitly identified. Retries log API/status codes and their reason.

The dedicated **1,984 KiB flash ring** holds **7,936 records**. Oldest sectors
are overwritten when full; capacity is reduced by up to one 16-record sector
while rotating. At roughly four polls/sec and three records per speed exchange,
retention is around **10 minutes**, depending on fragmentation and other events.
Unsuccessful discovery sessions usually generate much less data. This is bounded
field-debug logging, not permanent trip recording. A full chip erase or partition
layout change can remove it; ordinary uploads with this same layout do not target
its partition.

Flash records use a CRC and a commit marker written last. Incomplete records are
ignored after a power cut, and writing resumes in erased space. Logging has a
bounded asynchronous queue so Bluetooth callbacks never wait for flash. If it
fills, a drop count is recorded. Export temporarily freezes appends for a coherent
snapshot; new events can fill that queue during a long export, so export at the
PC rather than treating it as concurrent live telemetry capture.

## Wiring and verified hardware

USB identification of the connected board reported **ESP32-D0WD-V3, revision
v3.1, Bluetooth Classic capability, 4 MB BOYA flash**. The printed module marking
has not been inspected here. The owner connected a new seven-pin GC9A01 display
on 2026-10-28 and confirmed it displays correctly. It has no LED/BL connection;
GPIO4 is unused. The table below describes its current SPI wiring.

Use printed GPIO numbers, not header positions. Disconnect power before rewiring.

| GC9A01 label | ESP32 DevKit V1 | Purpose |
| --- | --- | --- |
| VCC | 3V3 | Display power, verified for the current module |
| GND | GND | Shared ground |
| CS | GPIO25 | Only initialized display |
| RST | GPIO26 | Active-low reset |
| DC | GPIO27 | Data/command |
| SDA | GPIO23 | SPI MOSI, not I²C |
| SCL | GPIO18 | SPI clock, not I²C |
| LED/BL | Not present | No GPIO4 connection |

GPIO32 and GPIO33 remain reserved and HIGH. Keep SPI wires short.
A future three-display system needs its own power and memory budget.

## Image dimming on the seven-pin display

The default `esp32doit-devkit-v1-image-dimming` profile sets
`OBD_BACKLIGHT_IMAGE=1`; GPIO4 remains unused. The hold gesture toggles 50%/100%
image intensity. USB `brightness 0..100` also controls image intensity.

RGB565 channels are scaled while copying the unchanged framebuffer into the
existing DMA strip. Changing intensity forces a complete display transfer even
if the demo value has not changed, so every page updates uniformly. Returning to
100% uses the original full-color pixels; there is no cumulative darkening.
The percentage describes RGB intensity, not calibrated physical luminance.

`dimtest on` repeats 100%, 50%, 0%, six seconds per level. Its temporary label
is `IMG n%` on the speed page. At zero all image pixels become black, but the
backlight remains powered. `dimtest off` ends a running cycle, restores 100%
and clears the label. Startup never automatically runs a test.

## SPI dimming experiment on the seven-pin display (no visible effect)

The optional `esp32doit-devkit-v1-spi-dimming` supplies `-DOBD_BACKLIGHT_SPI=1`. It does not
configure GPIO4. Normal startup commands 100%; a button hold and the USB
`brightness 0..100` command now send SPI brightness settings.

The driver writes target brightness to register `51h` (0..255), then writes
`2Ch` to `53h` (BCTRL, DD and BL enabled). Whole-frame transfers and brightness
commands share a mutex, so USB commands cannot interleave with drawing.
The GC9A01's brightness block controls its LEDPWM output; the panel/module must
connect that output to its backlight driver for physical dimming to occur.
Reference: [GC9A01 datasheet](https://files.waveshare.com/wiki/common/GC9A01A.pdf),
sections 5.10.3, 6.2.26 and 6.2.27.

For the visual test, send `dimtest on` over USB. It repeats **100% -> 50% -> 0%**,
six seconds per level, with a temporary `DIM n%` label above the speed reading.
The label is shown on the speed page only. `dimtest off` stops the test, restores
100% and removes the label; a reboot also starts normal operation at 100%.
Manual brightness and hold gestures are suspended while the test is active;
single/double clicks still select theme/page. No loop runs automatically at boot.

SPI is write-only. `ESP_OK`, `BRIGHTNESS_OK`, the percentage label and health
brightness values establish commanded settings, not measured optical brightness.
This experiment does not fade pixel colors, change gamma or PWM the display's
power supply. If the picture remains equally bright throughout the test, board
wiring likely bypasses the controller's brightness output.
The owner confirmed exactly this result on 2026-10-28: only the label changed,
including at 0%; brightness stayed the same. The test was stopped at 100%.

## GPIO4 backlight control on the previous eight-pin display

SCK stays on **GPIO18**. The resolved black-screen incident was an SCK wire moved
to 3V3; it did not establish the LED pin's electrical role. The seller's supplied
drawings describe different variants and do not establish whether this red
board's LED pin carries the LED current or is a low-current control input.

The owner confirmed that the repeating brightness test visibly worked, then
reported removing the 1 kOhm resistor and retaining a working direct GPIO4 -> LED
connection. This verifies brightness control, not the pin's electrical role or
load current. Retaining current limiting or using a verified driver remains
recommended until that interface is established.

The current-limited connection used for the initial test was:

```text
ESP32 GPIO4 / D4 ---- 1 kOhm series resistor ---- TFT LED
ESP32 3V3 ------------------------------------ TFT VCC
ESP32 GND ------------------------------------ TFT GND
```

The resistor bounds current from 3.3 V to about 3.3 mA even if LED is a power
input. Only one supply path should feed LED; do not connect it to 3V3 and GPIO4
simultaneously. All other display connections remain as listed above.

`OBD_BACKLIGHT_PWM` defaults to **0** in the source. The optional legacy
`esp32doit-devkit-v1-backlight` environment supplies `-DOBD_BACKLIGHT_PWM=1`.
For the original LED -> 3V3 connection, select/build/upload the base
environment explicitly with `pio run -e esp32doit-devkit-v1` and
`pio run -e esp32doit-devkit-v1 -t upload`.

PWM uses native ESP-IDF LEDC, 5 kHz and 10-bit duty resolution. The pin starts
LOW, then the backlight is set to `kBacklightStartupPercent` (100 by default)
after the first complete frame is transferred and remains there until changed
by a button hold or USB command. There is no brightness loop or brightness
overlay. Optional USB diagnostic commands
remain available for manually changing duty:

```text
brightness 100
brightness 50
brightness 25
brightness 0
brightness 100
```

Zero turns the backlight off; another `brightness 100` or reboot restores the
startup value. Commands return `BRIGHTNESS_OK` or `BRIGHTNESS_ERROR`, and changes
are retained in the diagnostic log. No ambient sensor or automatic day/night
selection is implemented. A successful command confirms that the ESP32 applied
the PWM duty, rather than measuring physical light output or current.

Both the default-disabled and separately enabled PWM variants compiled
successfully. Existing host checks for the gauge, OBD session and flash journal
also passed. See `docs/validation` for recorded hardware-test results.

## Build, upload and serial monitor

The platform is pinned to `espressif32@6.12.0` with ESP-IDF 5.5.0. Component Manager
fetches Espressif's GC9A01 driver 2.0.4 via `src/idf_component.yml` and
`dependencies.lock`. No Arduino framework, LovyanGFX or LVGL is used.

```sh
pio run
pio device list
pio run -t upload --upload-port /dev/cu.usbserial-0001
pio device monitor --port /dev/cu.usbserial-0001 --baud 115200
```

If `pio` is unavailable in your normal shell, use
`/Users/alex/.platformio/penv/bin/pio` instead. Exit the monitor with Ctrl+C before
upload/export. `esp32_exception_decoder` is enabled for monitor backtraces. Press
EN while monitoring to capture boot. If automatic download mode fails, hold BOOT
during the connection attempt, release when writing starts, then press EN if
needed to restart.

`partitions.csv` allocates 2 MiB for firmware and the remaining 1,984 KiB for
logs, preserving the NVS/PHY offsets. There is no OTA partition. Do not use the old
single-app partition table when installing this build. If PlatformIO reuses an
old component configuration after adding components, `pio run -t clean` followed
by `pio run` regenerates it.

## Code and settings

- `include/Config.h`: V-LINK name/PIN, poll/stale timing, panel settings and demo.
- `include/Pins.h`: signal assignments; `include/Telemetry.h`: speed/metric freshness.
- `src/input/*`: GPIO21 button task and pure debounced gesture recognizer.
- `src/obd/ObdClient.*`: native Bluedroid Classic SPP callbacks, event queue,
  discovery, pairing, reconnects and transport; no display drawing in callbacks.
- `src/obd/Elm327.*`: bounded prompt framing, initialization/scheduling and strict
  Mode 01/ATRV parsing; no dependency on hardware or Bluetooth for host tests.
- `src/logging/FlashJournal.*`: NOR-flash ring algorithm and record validation.
- `src/logging/FieldLog.*`: asynchronous flash writer and USB snapshot export.
- `src/display/*`: four-page themed RGB565 renderer, backlight and native `esp_lcd` SPI driver.
- `tools/read_field_logs.py`: read-only export utility.

The display uses a 115,200-byte RGB565 framebuffer split into fifteen independent
7,680-byte row bands, plus a 7,680-byte DMA strip; no PSRAM is assumed. This avoids
requiring a contiguous 115 KB region after Classic BT reserves DRAM. Completed
dirty rectangles are sent in bounded strips with explicit
ISR wake-up after DMA, without visibly clearing the panel first. Radio stacks
and logging add RAM usage; periodic HEALTH records report actual free heap,
largest block and main-task stack waterline.

Speed numerals use [Roboto Condensed](https://github.com/google/fonts/tree/main/ofl/robotocondensed)
at weight 700, rasterized into 56x120, 48x104, 42x88 and 36x76 cells with
four-bit alpha for smooth edges. The 90,720-byte digit masks stay in flash; no
runtime font engine or extra framebuffer is used. Right-aligned glyph masks and leading-bearing metadata
keep the visible number centered while retaining stable tabular positions
through 100..199. The unit text and engine-data page retain their compact font.
The font source, OFL license and provenance are in `assets/fonts/roboto-condensed`.
To regenerate the checked-in `src/display/SpeedFont.h`, run
`python3 tools/generate_speed_font.py` with Pillow 12.3.0/FreeType available.
Normal PlatformIO builds do not require Pillow or font downloads.

The tested TFT requires `kMirrorX=true`. Inversion and BGR order are enabled.
Other modules may need different inversion/order/orientation or supplier-specific
initialization. SPI starts at 20 MHz; check wiring/power before lowering it to
10 MHz to diagnose corruption. SPI is write-only: an initialization log cannot
prove a physical panel is present. The earlier desktop renderer preview is in
[docs/gauge-preview.png](docs/gauge-preview.png).

## Checks

```sh
sh test/run_host_checks.sh
```

Host C++ checks run with address/undefined-behavior sanitizers and cover:

- Framebuffer guards, dirty tracking, DEMO/LIVE indicator transitions, and incremental
  versus fresh render equivalence through ascending/descending speed changes.
- Strict speed/support parsing, spaced/compact hex, echoes/search text, fragmented
  prompts, zero/maximum speed, wrong PIDs/lengths, multiple capability maps,
  conflicting speed replies, adapter
  errors, receive overflow, timeouts, and stale/disconnected LIVE policy.
- Actual flash-journal rollover and ordered export against a simulated NOR flash,
  reboot retention, CRC rejection, all 256 interrupted-write positions, and
  interrupted sector erases during rollover.

Original display-only bench capture is retained in
[docs/validation/serial-log.txt](docs/validation/serial-log.txt). Current Bluetooth
bench results and field logs are stored separately under `docs/validation/`.

References: [ESP-IDF SPP API](https://docs.espressif.com/projects/esp-idf/en/v5.5.2/esp32/api-reference/bluetooth/esp_spp.html),
[GC9A01 driver](https://github.com/espressif/esp-bsp/tree/master/components/lcd/esp_lcd_gc9a01),
[Vgate V-LINK pairing guide](https://forum.vgatemall.com/showthread.php?tid=8432).
