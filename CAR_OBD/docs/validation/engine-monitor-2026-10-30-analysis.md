# Standard engine warning monitoring, 2026-10-30

Firmware 0.3.9 disables the unsuccessful Hyundai handbrake/seatbelt probe and
all sample warning icons. Background standard-PID monitoring is enabled on both
pages: 0101 (commanded MIL plus reported DTC count) and 0105 (coolant), each with
a 3,000 ms target interval. Their initial due times are staggered by 1,500 ms
after the first valid speed. Starts can slip with serialized response latency;
speed must succeed between optional requests. The details page reuses coolant
and does not issue extra 0105 requests. Existing voltage/oxygen queries remain
page-dependent. The normal 250 ms post-speed pause and 1,500 ms speed freshness
limit are unchanged.

MIL responses must be exact four-byte payloads for 4101. Unlike numeric
measurements, controllers may legitimately report different readiness bytes and
MIL states. The specialized parser aggregates byte A bit 7 with OR and keeps
the maximum lower-seven-bit DTC count; it logs response-line count. This avoids
silently selecting a controller or double-counting repeated headerless replies.
It cannot establish unique ECU/code counts with headers off. Malformed matching
frames fail the entire reading even when another matching frame is valid.
The icon follows commanded MIL, not merely the presence of stored codes.
There is no Mode 03 code-list retrieval, code clearing or actuator operation.

Coolant uses the existing strict single-byte decoder, A minus 40. The early
temperature alert is configurable at 105°C on and 100°C off. These project
thresholds are not a verified Hyundai factory overheat threshold. Hysteresis
retains the prior state between the boundaries. MIL and temperature freshness
is nine seconds; stale, invalid and future-dated readings are UNKNOWN. An already
active warning stays illuminated through unknown readings while speed is LIVE,
until a fresh clear value arrives. DEMO hides both icons and resets these
latches. A green connection dot still only confirms fresh speed.

Monitor failures with a prompt invalidate only that monitor and retry after
three seconds. A missing/late prompt has a one-second deadline and triggers
the existing transport reconnect/reset to resynchronize replies. Raw requests/
responses, MIL state/count, coolant values, monitor errors and warning-state/
visibility changes go into persistent field logs. Body-probe configuration logs
explicitly report disabled.

Host checks passed with ASAN/UBSAN, including:

- Multi-responder MIL on/off and order independence, differing readiness bytes,
  maximum-count behavior, duplicate lines, malformed and wrong-header frames.
- Eighteen-second simulated schedules on both pages: each PID repeats with a
  three-second target, no consecutive optional polls, a speed reply between
  queries, no duplicate details coolant, no body/header probe commands.
- Optional monitor error recovery, coolant conflicts and timeout resync.
- Temperature activation/clear boundaries, hysteresis, nine-second expiry,
  invalid/future timestamps, retaining active warnings through unknown readings,
  fresh clear values and DEMO/new-session reset.
- Existing rendering, button, field-log and disabled-probe implementation checks.

Sources:

- [OBD inspection flowchart, pages 8–9](https://training.wep-inc.com/GenTraining/MoGVIPInspectorTraining/story_content/external_files/OBD%20IM%20Flowchart.pdf):
  per-controller PID 01 byte A bit 7 and lower-seven-bit DTC count.
- [Emtron OBD documentation](https://docs.emtronaustralia.com.au/displays/functions/obd2/index.html):
  PID 05 and coolant A minus 40 decoding.

Artifacts use the `engine-monitor-2026-10-30-` prefix. Real monitor responses and
warnings still require a car-side field test; the existing car session was saved
before this firmware change and must not be interpreted as 0.3.9 verification.

The default image-dimming PlatformIO build passed without compiler warnings:
44,224 bytes static RAM and 745,309 bytes application flash. Upload to the
connected `/dev/cu.usbserial-0001` succeeded with flash data hashes verified;
the field-log partition was retained.

Serial startup identifies 0.3.9, `ENGINE_MONITOR enabled=1 poll_ms=3000`,
`preview=0` and `BODY_PROBE enabled=0`. Observed health: free heap 47,460 bytes,
largest block 45,056 bytes, main-task stack waterline 552 bytes and maximum loop
draw time 39,743 microseconds, at 100% image intensity. No crash was observed;
the serial port was closed after capture. No car/adapter is present at the PC,
so these logs confirm configuration/startup rather than real PID responses.

Final ELF SHA256:
`67eb2ce93d9ebe54e4d48fb22252ca7cbb84f6260f0c52637e6e6635e7263308`.
