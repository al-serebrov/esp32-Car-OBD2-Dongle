# Button, themes and engine-data page, 2026-10-27

Firmware 0.3.0 implements the GPIO21 -> normally open button -> GND controls:

- Single click toggles day/night on either page.
- Double click switches speed/engine-data pages without a theme toggle.
- Hold past one second toggles 50%/100% once, without a release click.

The owner confirmed all three controls work correctly on the physical device.
Serial logs independently record DAY/NIGHT transitions, DETAILS/SPEED page
changes and a successful 50% hold event. Startup remains speed/night/100%.

## Data and rendering

Day uses black numbers on white; night uses white numbers on black. The new
page arranges adapter supply voltage / coolant above lambda 1 / lambda 2.
Labels identify the oxygen sensors as B1S1 and B1S2. The voltage is the ELM's
ATRV input-voltage reading, not ECU PID 42. Coolant uses 0105 and conventional
oxygen-sensor voltages use 0114/0115. Wideband equivalence ratios/current are
not substituted for voltage. There is no fabricated engine-data demo.

Only the data page enables additional requests. Speed and optional readings
alternate in one outstanding-command session. Optional errors invalidate that
metric only and cool down its retries for 30 seconds; a missing prompt still
requires reconnection. Tile data expires after ten seconds and displays `--`.
Speed retains its existing 1.5-second freshness policy. Transport disconnection
invalidates both speed and all tile readings.

## Validation

- Host ASAN/UBSAN checks passed for existing gauge, OBD and flash-journal behavior.
- Added gesture checks cover bounce, single/double, the second-click deadline,
  a long hold, second-click-then-hold, independent clicks, and a button held at boot.
- Added UI checks cover repeated theme/page transitions, clean redraw equivalence,
  framebuffer guards, expired/missing data and valid zero readings.
- Added OBD checks cover strict ATRV decoding, malformed/conflicting voltages,
  four-detail/speed scheduling, signed coolant and zero/full-scale lambda,
  detail-error cooldown, page-off speed-only polling and prompt timeout recovery.
- Native-renderer previews were visually inspected for day/night speed and
  populated/missing detail pages. Their numeric examples are synthetic QA fixtures.
- Native ESP-IDF build and upload succeeded, including flash hash verification.
- Startup logs identify firmware 0.3.0, GPIO21 initialization, 5 kHz GPIO4 PWM
  and a successful 100% startup setting. Health after the gesture test reported
  46,820 bytes free heap and 712 bytes main-task stack high-water space.
  A full theme/page repaint reached approximately 152 ms; ordinary animation
  remained active. V-LINK was absent in the desk environment.
- The USB device became unavailable during the read, ending the host capture
  with `Device not configured`. The serial port was closed by the reader's
  cleanup. Offline checks of the saved capture confirmed boot, both themes,
  both pages, successful 50% and 100% hold actions, health and no firmware crash.
  Its final health report was SPEED / DAY / 100% brightness.

The four live sensor readings and their latency still require a car-side field
test. The owner has confirmed physical button behavior and page/theme changes,
not the availability of the newly requested metrics in the vehicle.

Final ELF SHA256:
`281a7284bd80482fefa097e975357766725e20636f3d10094eb4d49f731f163b`.

## Artifacts

- `button-pages-2026-10-27-upload.txt`: final incremental build and verified upload.
- `button-pages-2026-10-27-serial.txt`: boot, button actions and runtime health.
- `button-pages-2026-10-27-preview.png`: synthetic rendered examples.

See the README's button and data-source documentation for command references.
