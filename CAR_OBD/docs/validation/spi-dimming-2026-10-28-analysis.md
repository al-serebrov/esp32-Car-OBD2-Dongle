# Seven-pin GC9A01 SPI dimming experiment, 2026-10-28

The owner connected the new seven-pin display and confirmed normal rendering.
It has no LED/BL pin; GPIO4 is physically disconnected. Firmware 0.3.4 changes
the default profile to `esp32doit-devkit-v1-spi-dimming`, with GPIO PWM disabled
and `OBD_BACKLIGHT_SPI=1`.

## Implementation

The brightness setter writes register 51h with a rounded 0..255 DBV value,
then register 53h with 2Ch (BCTRL/DD/BL enabled). The target is set before enabling
the control block. A mutex serializes whole-frame DMA activity and brightness
commands from the main and USB-console tasks. The tracked percentage records
the last successfully transmitted command, not measured optical brightness.

The chip's brightness block controls an external LEDPWM output. This only dims
the physical backlight if the module routes that output to its backlight driver.
The seven-pin board's actual backlight routing remains unverified. No pixel
shading, gamma adjustment, supply PWM or hidden GPIO4 drive is used in this test.

Reference: [GC9A01 datasheet](https://files.waveshare.com/wiki/common/GC9A01A.pdf),
sections 5.10.3, 6.2.26 and 6.2.27.

`dimtest on` starts a 100/50/0 cycle, six seconds per stage, with a temporary
`DIM n%` label on the speed page. `dimtest off` stops the running cycle, restores
100% and clears that label. No cycle is enabled automatically at boot. The USB
manual brightness command and hold gesture use SPI outside the running test.

## Verification

- Existing ASAN/UBSAN suites passed for rendering, UI, button, OBD and journal.
- Added UI coverage checks that ending the dimming test restores the original
  gauge pixels exactly and repeated identical test labels do not redraw.
- After cleaning a failed temporary CMake configuration, native build succeeded.
- Upload succeeded and verified the flash data hash.
- Serial boot identifies firmware 0.3.4 and explicitly logs GPIO4 as unused.
- The visual-test sequence transmitted 100%, 50%, 0%, 100%, with DBV values FFh,
  80h, 00h and FFh, always paired with CTRL 2Ch. Each write returned ESP_OK.
- Ending the test acknowledged `DIMTEST_STOPPED brightness=100`. An additional
  manual 100% command was sent during cleanup. The serial port was closed.
- Health stayed normal: 47,460 bytes free heap, 712 bytes main-task stack
  high-water space and drawing maxima around 57 and 60 ms; no firmware crash.

These checks establish SPI command delivery and working firmware only.
The owner initially missed the timed test, so it was restarted as a continuous
labelled cycle with its serial monitor left open. The owner then confirmed:
**the label changed, but brightness stayed the same**. Standard SPI brightness
control therefore had no visible effect on this particular board. The cycle
was stopped, 100% was requested and the serial monitor was closed.

The follow-up capture is `spi-dimming-2026-10-28-observed-serial.txt`.

Final ELF SHA256:
`e3e877b5dfa12c6506a34511821109dc0bd06c85f0f5888ee486de380c4c4127`.

Artifacts:

- `spi-dimming-2026-10-28-build.txt`
- `spi-dimming-2026-10-28-upload.txt`
- `spi-dimming-2026-10-28-serial.txt`
