# Larger speed reading, 2026-10-27

Firmware 0.3.1 removes the small scale numbers, inner tick marks, SPEED heading
and DEMO/LIVE words from the speedometer. The numeric reading now uses fixed
54x120 seven-segment digits, twice the previous height, with eight-pixel gaps.
One, two and three digits remain centered; the full 0..255 numeric range fits
inside the retained arc. KM/H is beneath the reading.

A bordered 10x10 round indicator below KM/H is yellow for demo and green for
live. Existing speed validity/freshness logic controls its color. Day/night
palettes, GPIO21 gestures, backlight control and the four-reading page remain.
The splash's SPEED DEMO subtitle was also removed.

Large-number clearing is limited to the circular dial interior because its
rectangular bounding box reaches the arc at the corners. This preserves arc
pixels while removing all previous digit segments, including 3 -> 2 -> 1 digit
transitions. Number rendering remains in the existing banded framebuffer.

## Validation

- Existing ASAN/UBSAN suites passed for up/down redraw equivalence, guards,
  banded rendering, UI theme/page transitions, button gestures, OBD and journal.
- The indicator dirty-region assertion was updated to its 10x10 footprint.
- Native-renderer previews at 0, 87, 120 and 255 were visually inspected in both
  themes, including yellow and green indicators. These are synthetic QA frames.
- PlatformIO build and upload succeeded with verified flash hash.
- A 27-second serial capture confirmed 0.3.1 startup, 100% brightness, GPIO21
  initialization and two healthy DEMO reports with no firmware crash.
- Recorded heap was 46,976 bytes, main-task stack high-water space 712 bytes,
  and maximum observed draw time about 84 ms during the captured interval.

Final ELF SHA256:
`97af9cf7302d6a2aaaa502abb30340b32ab40a84921d09fde191e9c6b12ec818`.

Artifacts:

- `large-speed-2026-10-27-build.txt`
- `large-speed-2026-10-27-upload.txt`
- `large-speed-2026-10-27-serial.txt`
- `large-speed-2026-10-27-preview.png`
- `../gauge-preview.png` (updated current-layout example)

The serial port was closed after verification. Physical readability in the
vehicle and a live green indicator were not evaluated in this desk capture.
