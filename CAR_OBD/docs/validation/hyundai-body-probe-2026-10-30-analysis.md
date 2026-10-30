# Hyundai handbrake / driver-seatbelt probe, 2026-10-30

Firmware 0.3.8 adds a read-only data-gathering experiment. Car Scanner's selected
profile is Hyundai Tucson LM, ix35 EL 2.0/2.4 MPI 2WD (2009–2015). The owner has
not checked whether that app actually exposes working handbrake/buckle sensors.

## Evidence and limits

- [OBDb Tucson signal definitions](https://github.com/OBDb/Hyundai-Tucson/blob/main/signalsets/v3/default.json)
  document request ID 770, response 778, service 22 DID BC03 with eight data bytes.
  Handbrake is bit 36 (byte 4 mask 08) and driver buckle is bit 46 (byte 5 mask 02),
  with MSB-first bit numbering. Published examples are newer vehicles; the LM
  request and bit semantics remain unverified. These factual mappings are used
  only as candidate labels in logs, never as display states.
- [ScanDoc's LM SJB coverage](https://scandoc.online/last/0/32/2/119/1/4/14)
  lists parking-brake and driver/passenger buckle switches for a 2011 LM with a
  different engine. It establishes that diagnostic tools can expose these signals
  on some LM variants, but publishes no usable request/decoder definition.
- [Car Scanner custom PID documentation](https://www.carscanner.info/custompids/)
  specifies that a manufacturer PID needs the correct header, command and decode
  formula. A matching vehicle-profile name by itself is insufficient evidence.
- [ELM327 datasheet](https://www.elmelectronics.com/wp-content/uploads/2016/07/ELM327DSH.pdf)
  documents ATDPN, ATSH, ATCRA/default receive-filter restoration, CAN formatting
  and flow control. These change adapter behavior, not car configuration.

## Behavior

The pure CPU `HyundaiBodyProbe` state machine shares the single outstanding
request buffer in `Elm327`. It requires a successful speed reply and ten seconds
of connection uptime before its first attempt. A detail request clears the speed
eligibility gate so the probe cannot be stacked immediately after another
optional measurement. Before changing any CAN route, ATDPN must return 6 or A6;
other/unknown protocols disable that connection's probe.

The transaction targets 770, filters 778 and reads only 22BC03. It restores 7DF
and default filters before issuing another speed/detail request. Completed setup
or read errors also restore both settings. No diagnostic session/security access,
code clear, actuator or manufacturer configuration operation is used.

The parser requires the correct positive service/DID and exactly eight data
bytes. It accepts complete headerless data or ELM CAF1/ATH0 length-and-indexed
lines (00B, 0: with six message bytes, 1: with five). Truncated, oversized,
out-of-order, duplicate, wrong-DID and malformed data are rejected. The full
raw response plus candidate payload/bits are retained in flash logs, explicitly
marked unverified. No candidate response changes UI icons or claims all-clear.

Successful probes repeat after at least 5 seconds; completed failures cool down
for 60 seconds. Each command has a 600 ms deadline, with a 1,000 ms overall
transaction budget. A missing/late prompt, body overflow or failed restoration
forces a reconnect and disables probes for the remainder of the boot, avoiding
repeated disruption of speed on every reconnect. ATZ establishes fresh defaults
on the new connection. Stale speed continues to produce DEMO as before.

## Validation and next car test

Host checks cover both response formats, exact payload boundaries, wrong DID,
malformed/duplicate/indexed frames, all six transaction failure points, protocol
gating, successful routing restoration, returning to speed, unsupported cooldown,
every missing-prompt phase, late replies, overall budget expiry between commands
and suppression surviving reconnect. Existing gauge/UI/button, OBD detail and
flash-journal checks remain required.

All host checks passed with ASAN/UBSAN. The default image-dimming PlatformIO
build passed without compiler warnings: 44,224 bytes static RAM and 743,641
bytes application flash. Car access and mapping validation remain outstanding.

Upload to `/dev/cu.usbserial-0001` succeeded with flash data hashes verified.
Serial boot identifies 0.3.8 and `BODY_PROBE enabled=1`, request 770/778 BC03,
10,000 ms initial delay, 5,000 ms repeat interval, 60,000 ms retry interval,
`mapping_verified=0 log_only=1`. No V-LINK/car is connected at the PC, so no
real body response was tested. Observed health: 47,476 bytes free heap,
45,056-byte largest block, 648 bytes main-task stack high-water space, maximum
loop draw time 67,037 microseconds. No panic, watchdog or assertion was observed.
The serial port was closed after capture; the field-log partition was not erased.

Final ELF SHA256:
`7875f8cf91e161d5b07401f0030b9b97f47bea206f339a133955160c7fe93aec`.

Artifacts share the `hyundai-body-probe-2026-10-30-` prefix: `host-checks.txt`,
`build.txt`, `upload.txt` and `serial.txt`.

All four warning icons remain the existing UI prototype. No real MIL or overheat
monitoring has been introduced. The proposed 105°C early-temperature threshold
is not a verified factory overheat threshold and is not active in this build.

For field validation, park with ignition on. Hold the foot brake while toggling
the handbrake, and test driver buckle changes separately, several times with
15-second holds per state. Note the sequence/times, then extract the retained
logs. Verify request access and independent, repeatable bit changes before
connecting the states to icons. If this newer-family request is unsupported, use
Car Scanner's working sensor definition/ELM debug capture to identify the LM
request; do not substitute arbitrary addresses or data bits.

## Subsequent field result

The owner's 2026-10-30 test is captured in
`hyundai-body-car-test-2026-10-30-analysis.md`. Both body reads returned NO DATA;
no mapping was validated. Standard speed and routing restoration worked.
Obtain a working LM-specific definition/capture before repeating this candidate
test or connecting any brake/belt state to the UI.
