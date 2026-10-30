# Visible-width speed centering, 2026-10-27

The owner observed that readings beginning with 1 appeared shifted to the right,
particularly at three digits, and stated that normal use will stay below 200 km/h.

Firmware 0.3.2 excludes the leading 1's 44-pixel blank prefix when centering the
reading. The digit glyph remains 54x120 and subsequent cells retain their normal
fixed 62-pixel advance. Readings starting with 1 move left by 22 pixels. All
100..199 readings share exactly the same layout positions, so changes in the
last two digits do not recalculate their spacing or font size. The correction
also centers one- and two-digit readings starting with 1.

The full OBD 0..255 value range is still accepted and displayed; the stated
normal-use limit is not implemented as a misleading numerical clamp. Existing
full-width rendering remains available for unexpected values above 199.

## Validation

- Visually inspected before/after native-renderer previews at 11, 99, 100, 110,
  111 and 199. Measured white numeral pixels, excluding unit and colored arc.
- Previously, the visible bounding center of readings starting with 1 was
  x=141.5 instead of x=119.5. All six corrected preview cases center at x=119.5.
- For 100..199, visible bounds change from x=75..208 to x=53..186.
- Existing ASAN/UBSAN gauge, UI, button, OBD and flash-journal checks all passed,
  including up/down redraw equivalence and the 99/100 transition in banded mode.
- ESP-IDF build and upload succeeded, with flash data hash verified.
- A 27-second serial capture confirmed 0.3.2 startup, GPIO21 initialization,
  100% brightness, healthy demo animation, and no firmware crash. Recorded
  free heap was 46,820 bytes and main-stack high-water space 712 bytes.
  The captured drawing maxima were approximately 49 and 52 ms.

Final ELF SHA256:
`7488bc35bba83da976c2d1479241b64a98bc3ef5dc6ddffe9b9a37f754c1a0b5`.

Artifacts:

- `speed-centering-2026-10-27-build.txt`
- `speed-centering-2026-10-27-upload.txt`
- `speed-centering-2026-10-27-serial.txt`
- `speed-centering-2026-10-27-preview.png`

The serial port was closed after verification. The captured button action at
the end selected the DETAILS page; theme/brightness/page controls are unchanged.
