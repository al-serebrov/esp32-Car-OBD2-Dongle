# Diagnostics discovery firmware 0.3.11

Firmware ELF SHA-256: `8648db7bdf0a6e13595e488e59b543b1ed5f041f9c46315bba1b5195860842d1`.

Built and flashed to `/dev/cu.usbserial-0001` on 2026-10-30 using the default
`esp32doit-devkit-v1-image-dimming` environment. Upload hash verification passed.
Flash image: 772361 bytes of app usage; static RAM: 54432 bytes. The installed
image includes all changes described in the README diagnostics-ride section.

## Verification

- Address/undefined-behavior-sanitized host checks passed for the existing gauge,
  button/themes/details, OBD parser/scheduler, and flash journal.
- New discovery checks passed for strict single/multiframe payload parsing,
  frame-index wrapping, capability-bank unions, advertised-PID selection,
  multi-ECU MIL, routing restoration, all six timeout phases, quarantine across
  reconnect, page cancellation, below-10 km/h deferral, trip integration and
  checksum-protected report reload. First/latest/changed candidate samples were
  checked during repeated responses. Fresh-render equivalence and hidden-page
  guards passed for the third page in both themes.
- Synthetic five-minute ELM run, 60 ms replies: 770 speed responses, 282 optional
  readings, maximum speed gap 790 ms. Standard and custom repeat samples both
  continued; no routed speed query or adjacent optional reading was allowed.
  This is a scheduling model, not a car latency measurement.
- Export script checked with mocked serial for framed summary + raw exports,
  missing-summary fallback and inactive DTR/RTS configuration.
- Native renderer preview was inspected with a circular display mask.
- Hardware boot reported version 0.3.11 / ELF prefix 8648db7b, 37264 free heap,
  34816 largest free block, app_main stack headroom 1112 bytes, OBD worker stack
  headroom 1936 bytes, queue_pending=0. No panic/reset loop was observed.
- `help` lists `diagreport`; requesting it before the first car discovery run
  correctly returns `ESP_ERR_NVS_NOT_FOUND`. No synthetic car report was written
  into device storage. Actual discovery NVS writes/reload still need the field run.

## Scope and limits

The third page enables advertised Mode 01 discovery, fault-code lists, VIN,
adapter voltage and 14 published Hyundai candidate blocks. Those definitions
span multiple Tucson generations and remain unverified for this ix35. A positive
response confirms transport/framing only until field correlation validates the
signal. Failed attempts cannot prove universal unavailability. The normal live
warning icons never use these experimental body/gear definitions.

A 7608-byte latest-run report is checkpointed to NVS at idle request boundaries,
separately from the finite rolling raw log. It retains counters, standard samples,
64-byte custom first/latest/changed samples, extra VIN/DTC samples and a local
OBD-speed trip estimate. Abrupt power removal may lose an uncommitted tail.
The page is disabled at boot; viewing it without car speed preserves a prior run.

Source and request catalogue: [OBDb Hyundai-Tucson](https://github.com/OBDb/Hyundai-Tucson/blob/main/signalsets/v3/default.json).

Field procedure and export command are in [README](../../README.md#diagnostics-ride-third-page-version-0311).
