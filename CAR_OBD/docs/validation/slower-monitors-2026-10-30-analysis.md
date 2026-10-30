# Slower background monitors, DEMO icons and voltage alert, 2026-10-30

Firmware 0.3.10 implements the requested lower-traffic schedule and carries
forward the user's DEMO-preview and battery-icon requests made before the
polling discussion. Handbrake/seatbelt probes remain disabled.

## Polling and speed

| Reading | Command | Repeat target | Initial offset after first valid speed |
| --- | --- | --- | --- |
| Speed | 010D | 250 ms pause after each speed response | First request after initialization |
| Check Engine / reported fault count | 0101 | 10 seconds | 0 ms |
| Coolant | 0105 | 20 seconds | 1,500 ms |
| Adapter supply voltage | ATRV | 20 seconds | 3,000 ms |

The scheduler always receives a successful speed before another optional
request, with no batch of optional readings between speeds. There is one
outstanding request at a time. Rendering runs in the separate display/main task;
adapter requests do not execute in parallel. Optional replies therefore still
lengthen individual speed gaps. Repeat targets can slip with response latency
and speed priority. This reduces interference, not measurement error. A new
coolant/voltage condition may take about 20 seconds plus scheduling/response
time to appear. A green connection dot continues to indicate fresh speed only.

Voltage and coolant background results also populate the second page, which
does not issue duplicates. Oxygen sensors retain their existing page-dependent
polling and 10-second freshness. MIL freshness is now 30 seconds; voltage and
coolant freshness, including their tiles, is 60 seconds. Each allows three poll
periods, avoiding expiry before the next 20-second refresh. Prompted failures
invalidate their own reading and retry after the corresponding 10/20-second
interval. Existing one-second monitor deadlines and reconnect recovery remain.

The retained Hyundai candidate probe now has an additional start condition:
the most recently received valid speed must be strictly below 10 km/h. Tests
cover 0, 9, 10 and 87 km/h. It remains disabled in the actual build; the gate
does not claim the candidate request is compatible with the LM ix35.

## Alerts and preview

The red battery icon uses the existing adapter-supply measurement, ATRV. It
turns on strictly below 12,600 mV, clears at 12,800 mV, and retains its previous
state in between. This is the user's low-voltage threshold with hysteresis,
not the car's charge-warning signal or a battery-health diagnosis. At an initial
12,600 mV reading it is off. A running engine is not required: the same rule
applies whenever fresh speed keeps the device LIVE. Previously active alerts
remain visible through UNKNOWN readings until fresh clear values arrive; the
real-alert latches reset on DEMO. Temperature thresholds remain 105/100°C.

DEMO now has five synthetic samples: Check Engine, parking brake, overheat,
seatbelt and battery. It uses three rows of two compact 32x26 cells, with the
fifth icon at row 3, column 1. Four or fewer active icons use the original larger
44x36 cells in two rows. The 76-pixel speed numerals and the connection dot are
unchanged. Warning redraw clears the union of both layouts, ending before the
dot at row 228. Transitions and count changes leave no old icon pixels behind.

The renderer explicitly switches DEMO preview off before applying LIVE warning
states, so sample brake/belt icons cannot leak into LIVE. It restores all samples
on returning to DEMO. The current LIVE sources can produce at most three icons,
which retain activation order and gap closure. Page/theme changes preserve the
appropriate preview/real state. Static previews do not show actual vehicle data.

## Verification

All host checks passed under ASAN/UBSAN. Added coverage includes 120-second
simulated schedules on both pages, each polling interval, one-speed-between-
optional-queries, no duplicate ATRV/0105, monitor errors/timeouts, voltage
thresholds/recovery/staleness, metric-specific freshness, future body speed
gating, all five DEMO slots, count-driven layout changes, frame guards and
DEMO/LIVE/page transitions. Existing parser, button and journal checks pass.

The scheduler benchmark uses identical **synthetic 60 ms replies**, a 120-second
window and the speed page. These are model results, not car measurements:

| Model | Speed samples | Optional requests | Speed rate | Maximum speed gap |
| --- | ---: | ---: | ---: | ---: |
| Speed only | 388 | 0 | 3.226 Hz | 310 ms |
| New 10/20/20 schedule | 379 | 24 | 3.155 Hz | 420 ms |

The model rate reduction is approximately 2.2%; slower real adapter responses
can have a larger effect. A car-side log is still needed to measure it. Native
day/night previews were inspected for DEMO with five samples, LIVE without
alerts, battery-only and three active real-alert examples.

Default image-dimming PlatformIO build passed without compiler warnings:
44,224 bytes static RAM and 746,821 bytes application flash. Upload to
`/dev/cu.usbserial-0001` succeeded with flash data hashes verified. The field-log
partition was retained, including the separately saved prior car test.

Serial boot identifies 0.3.10, `mil_ms=10000 coolant_ms=20000 voltage_ms=20000`,
`demo_preview=1`, `BODY_PROBE enabled=0`, and the 12,600/12,800 mV voltage
thresholds. Observed health: free heap 47,476 bytes, largest block 45,056 bytes,
main-task stack waterline 520 bytes, maximum loop draw time 39,773 microseconds,
image brightness 100%. No panic, watchdog or assertion was observed. The serial
port was closed after capture. This PC-side verification confirms startup and
configuration; actual vehicle monitor readings/timing remain to be tested.

Final ELF SHA256:
`afc7add1221476f55a3b78a010018faa412d7cf09d4525c473aed27e802cbb54`.

Artifacts use the `slower-monitors-2026-10-30-` prefix: `host-checks.txt`,
`build.txt`, `upload.txt`, `serial.txt`, `preview.png`. The general
`docs/gauge-preview.png` is updated to the five-icon DEMO.

Source references:

- [ELM327 datasheet](https://www.elmelectronics.com/wp-content/uploads/2016/07/ELM327DSH.pdf),
  ATRV supply measurement and adapter calibration.
- [DENSO charging diagnostics](https://www.densoautoparts.com/charging-system-diagnosis/),
  distinguishing battery voltage measurements from charging-lamp diagnosis.
