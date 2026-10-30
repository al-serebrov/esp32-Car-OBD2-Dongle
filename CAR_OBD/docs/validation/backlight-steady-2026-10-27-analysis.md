# Steady GPIO4 brightness, 2026-10-27

The owner confirmed that the labelled 0/50/100 test visibly worked, then
reported removing the series resistor and requested normal operation at 100%
without the test loop or on-screen percentage.

## Change

Firmware 0.2.4 removes the automatic brightness cycle, its configuration flag,
the percentage overlay, its renderer state and its percent glyph. The original
gauge layout, including the bottom separator, is restored. GPIO4 is configured
at startup and set to 100% after the first display frame; no periodic brightness
changes remain. USB diagnostic brightness commands remain available.

The default environment is now `esp32doit-devkit-v1-backlight`, with PWM enabled.
The base environment retains the original LED -> 3V3 configuration.

## Verification

- Existing host checks passed for gauge rendering, OBD and flash-journal behavior.
- The default PlatformIO environment built successfully.
- Upload to `/dev/cu.usbserial-0001` succeeded and verified the flash data hash.
- Inspection of the final ELF found the PWM implementation and no automatic-loop
  log marker or brightness-overlay format string.
- Boot logs identify version 0.2.4, ELF prefix `7e39d4ae`, and a successful
  `BACKLIGHT_SET percent=100 duty=1023 result=ESP_OK`.
- A 29-second startup capture contained exactly that one brightness setting,
  no test-loop marker, two healthy DEMO reports and no crash. The serial port
  was closed after verification; the firmware continues at 100%.

Final ELF SHA256:
`7e39d4ae7effbcc8faa487c4d2e2b45dc84f729deb95abcee318df35380505d5`.

The owner's reported wiring is currently GPIO4 directly to LED. Working
brightness control does not establish LED-pin load current or its circuit role;
the recommendation to retain current limiting pending electrical verification
was reiterated. Firmware verification does not establish electrical suitability.

Artifacts:

- `backlight-steady-2026-10-27-build.txt`
- `backlight-steady-2026-10-27-upload.txt`
- `backlight-steady-2026-10-27-serial.txt`
