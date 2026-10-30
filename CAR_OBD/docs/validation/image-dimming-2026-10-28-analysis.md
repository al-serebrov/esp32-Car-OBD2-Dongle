# Image-only dimming, 2026-10-28

The owner confirmed that the SPI register test changed its label but did not
change brightness. The continuous SPI test was stopped, 100% was requested and
the serial port closed. Firmware 0.3.5 tries a separate software-image method.

## Behavior

The default environment is `esp32doit-devkit-v1-image-dimming` with
`OBD_BACKLIGHT_IMAGE=1`. GPIO4 stays unused and no SPI brightness registers
are written. The physical LED backlight remains unchanged.

RGB565 channels are scaled while copying the original framebuffer to the DMA
strip. Full-color source pixels are retained, so returning to 100% restores
the original image without accumulated rounding. A brightness change forces
a complete 57,600-pixel transfer even when the demo number has not changed.
A mutex prevents a mid-frame intensity change. The percentage is RGB intensity,
not a calibrated optical-luminance percentage or a reduction in backlight power.

The USB brightness command and hold gesture now control image intensity. The
optional `dimtest on` cycle uses an `IMG n%` label, six seconds each at 100/50/0.
Zero makes the image black, including its label, while the backlight may still
glow. Ending a running test with `dimtest off` restores 100% and clears the label.
Startup does not automatically enable the test.

## Verification

- Host ASAN/UBSAN rendering, UI/button, OBD and journal checks passed.
- RGB565 checks verify black, unchanged 100%, black at zero, half-white and
  independently scaled red/green/blue channels without channel mixing.
- Native-rendered previews of 100/50/0 were visually inspected in day/night
  themes; these previews model image pixels, not physical backlight changes.
- A failed temporary CMake configuration was cleared; final build and upload
  succeeded with verified flash data hash.
- Serial boot identifies 0.3.5, mode IMAGE, GPIO4 unused, fixed backlight.
- Logs show repeated `IMAGE_DIM_FRAME` transfers at 50, 0 and 100%, each covering
  all 57,600 pixels. No SPI brightness-write records appear in this capture.
- Recorded health showed at least 47,460 bytes free heap and 696 bytes main-task
  stack high-water space, with no crash during the observed cycles.

**The owner confirmed that image dimming works**: grey numbers at 50% and a
black image at 0%, with the physical backlight remaining powered. The continuous
cycle was then stopped with `dimtest off` and `brightness 100`. The capture
acknowledges `DIMTEST_STOPPED brightness=100` and `DIM_TEST stopped; restored=100`.
The temporary label was cleared and the serial monitor closed. The normal
hold gesture remains available for switching 50%/100% image intensity; no loop
runs on subsequent boots.

Final ELF SHA256:
`ff65a6b2b9d0655e7ac95c6a77c9cb2c9d2d2537957fed7d5dcbddda2d47e927`.

Artifacts:

- `image-dimming-2026-10-28-build.txt`
- `image-dimming-2026-10-28-upload.txt`
- `image-dimming-2026-10-28-observed-serial.txt`
- `image-dimming-2026-10-28-preview.png`
