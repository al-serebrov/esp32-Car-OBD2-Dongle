# Focused Hyundai candidate run — firmware 0.3.13

Firmware ELF SHA-256: `474cd80bf9d2b6090fc7767c3ef369e24ab9a7f4171a62a4d559afea104dca82`.

The third page now prioritises the responsive 7E1/21A0 transmission block,
7E1/21A1 alternative transmission block and 7E0/2101 engine block. Standard RPM
and IAT are sampled for correlation. Normal speed and 10/20/20-second engine
monitors continue. The broad discovery profile remains implemented but the
installed profile is CANDIDATES (`kCandidateDiagnostics=true`).

## Changes

- Fixed final-frame CAN padding: parse only declared data, validate the complete
  final frame's hex, accept trimmed or fully padded last frames. Strict sequence,
  bounds, short-frame, extra-frame and partial-padding checks remain.
- Replayed all 21 positive multi-frame replies saved from the first car ride:
  five TCM21A0, five TCM21A1, four engine2101, four short engine2103 and three VIN
  placeholder replies. Every payload matches the offline recovery byte-for-byte.
  VIN placeholders still fail the VIN semantic check.
- Eight optional slots: primary TCM / RPM / primary TCM / alternative TCM /
  primary TCM / engine block / primary TCM / IAT. Speed separates optional
  transactions. The primary block has a 500 ms repeat floor, others 1 second.
  Failed items have a 60-second floor, timeouts/routing errors quarantine until
  reboot. Routing remains atomic with original deadlines and restoration.
- Log each candidate payload/result, selected raw/formula fields and nearby
  standard references with validity/ages. Every speed value is retained; only
  repetitive successful speed TX/RX text is suppressed during focused capture.
  Candidate and routing wire data remain retained; bad speed frames are logged.
- The page shows GEAR?, CMD?, ATF?, OIL?, sample count and elapsed time. These are
  experimental labels. No candidate becomes a normal warning/PRND indication.
  Values expire when no usable samples arrive (gear/ATF 5 s, oil 20 s).
- Preserve the 7608-byte NVS report ABI. Unused record zero tags the profile;
  the 32-byte bitmap stores capability maps for DISCOVERY or observed raw gear
  values for CANDIDATES. Export distinguishes the profile and lists gear values.
  First/latest/changed-byte samples and per-item counters are preserved.
  Merely viewing the page at the PC does not overwrite the previous ride.
- The adaptive 120/104/88/76px speed layout is retained.

## Validation

All host checks passed with AddressSanitizer and UndefinedBehaviorSanitizer:
existing gauge/button/themes/adaptive layouts, OBD monitoring, journal retention,
discovery and new focused scheduling/real-reply fixtures. New checks include
unknown/expired candidate display, negative fractional temperature formatting,
focused report reload, profile selection without starting a new run, and speed
wire-log transaction classification. The focused parser/scheduler checks were
rerun after the final startup-display adjustment.

Synthetic five-minute run, 60 ms replies: 218 primary TCM samples, 54 alternative
TCM samples, 54 engine samples, 55 RPM references, 54 IAT references and 495 speed
samples. Largest speed gap was 790 ms. The car's actual rates may differ. The
baseline broad-profile scheduler still passes unchanged.

Native previews of both themes were inspected. PlatformIO ESP-IDF build passed;
app usage 852861 bytes, static RAM 54544 bytes. Upload to /dev/cu.usbserial-0001
passed flash hash verification. See the serial capture for hardware startup and
saved-report readback. New candidate values still require this next field run.

## Ride procedure

Select DIAGNOSTICS while parked and leave it selected during the drive. At the
end, while safely stationary with foot brake applied, pause about 10 seconds in
each P/R/N/D position if practical, noting elapsed times and raw gear values.
Do that check near the end so relevant transitions remain near the end of the
finite rolling log. Return to speed, wait five seconds, then remove power.
Export before another ride; first/latest/changed summaries do not preserve the
timing of every gear change once raw records roll over.

Full details and export command: [README](../../README.md#focused-candidate-ride-third-page-version-0313).
Definition provenance: [OBDb Hyundai-Tucson](https://github.com/OBDb/Hyundai-Tucson/blob/main/signalsets/v3/default.json).

Hardware startup confirmed: 0.3.13 / ELF prefix 474cd80b; CANDIDATES profile
with three active custom blocks. The preceding DISCOVERY report re-exported
completely, unchanged session 1 / active_ms 317057 / 760 m, and its NVS checksum
validated. Early free heap 36620 bytes, largest block 27648, app stack headroom
1144 bytes. No panic/reset loop was observed during the bench capture.
