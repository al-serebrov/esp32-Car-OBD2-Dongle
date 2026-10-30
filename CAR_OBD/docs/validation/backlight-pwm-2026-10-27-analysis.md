# GPIO4 backlight test, 2026-10-27

The owner confirmed GPIO4 -> 1 kOhm series resistor -> display LED wiring and
the USB connection. VCC remains on 3V3; SCK remains on GPIO18.

PlatformIO environment `esp32doit-devkit-v1-backlight-test` supplies
`-DOBD_BACKLIGHT_PWM=1` and is currently the default environment. The base
environment retains disabled PWM for the original LED -> 3V3 wiring.

## Verification

- Firmware 0.2.2 built successfully and was uploaded to `/dev/cu.usbserial-0001`.
  The upload verified the flash data hash.
- Captured boot identifies the new ELF as `362a8f4f7...` and initializes GPIO4
  PWM at 5 kHz, 10-bit resolution. The splash is transferred before 100% duty.
- USB commands 100%, 50%, 0%, 100% each returned `BRIGHTNESS_OK` and logged
  `ESP_OK`, with respective duties 1023, 512, 0, 1023. Each step lasted about
  six seconds. An additional 100% command was acknowledged before closing USB.
- DEMO continued during the test. Recorded health heap was 49,468 bytes and
  maximum draw time was about 24 ms. No firmware crash was seen.
- V-LINK was absent from the desk scan; connection attempts continued normally.

The test leaves the output at 100%. The serial port is closed. Visual confirmation
of dimming/off/on and usable brightness through the resistor is pending the owner.
Command acknowledgements establish software operation, not physical brightness
or whether LED is a control input versus a backlight power input.

## Artifacts

- `backlight-pwm-2026-10-27-build.txt`
- `backlight-pwm-2026-10-27-upload.txt`
- `backlight-pwm-2026-10-27-serial.txt`

The existing host checks passed during PWM preparation. This step selects the
already compiled PWM path through a dedicated PlatformIO environment and checks
it on the connected ESP32; no additional display or OBD behavior was changed.
