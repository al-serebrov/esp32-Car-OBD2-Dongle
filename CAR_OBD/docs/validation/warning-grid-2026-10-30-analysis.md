# Four-warning layout prototype, 2026-10-30

Firmware 0.3.7 expands the visual prototype to two columns and two rows:

| Left | Right |
| --- | --- |
| Amber Check Engine | Red parking brake (circled P) |
| Red overheat (thermometer over waves) | Red seatbelt |

All four remain illuminated in DEMO and LIVE for layout review. They are synthetic
samples, not diagnostic states. No warning polling or temperature threshold was
introduced, and no availability of Hyundai seatbelt/brake data is assumed.
`WARNING_PREVIEW` records the names and synthetic source at boot.

The smooth speed font is regenerated at 36x76 pixels per cell. It retains the
right-aligned glyphs and leading-bearing centering used for stable 100–199
readings, with 13,680 bytes of digit masks in flash. Speed occupies rows 52–127;
KM/H is at rows 134–147. Warning slots are 44x36 pixels, starting at (62,152),
(134,152), (62,190) and (134,190). Two-column placement derives from the slot
index. The 10x10 yellow/green connection indicator moves to rows 228–237,
below both icon rows. The icon strokes are sampled at 2x2 resolution and use
theme-adjusted colors for contrast on black or white backgrounds.

The renderer stores active warnings in activation order rather than assigning
an icon to a fixed slot. `setWarning(icon, true)` appends a newly active warning;
repeated active readings leave order unchanged. Clearing an icon removes it and
packs the survivors to the left and then the next row. If that warning returns,
it joins the end. Updates received on the details page change state without
drawing over that page; the order survives theme/page changes. The prototype
initially seeds all four warnings once, preserving their current preview order.

Verification:

- All host checks passed with ASAN/UBSAN, covering incremental/fresh frames,
  banded framebuffers, button gestures, themes/pages, image intensity, OBD and
  flash-journal behavior.
- Updated checks require colored pixels in each of the four warning slots in
  both themes. The entire warning grid must remain unchanged through every
  speed from 0 to 255 and DEMO/LIVE transitions. Status-indicator dirty bounds
  were updated for its new position.
- Relocated icon masks are compared pixel-for-pixel in both themes for empty,
  one-, two-, three- and four-warning states, non-default arrival order, repeated
  readings, clearing the first/middle/last warning and reactivation. Checks also
  verify unrelated speed/arc/status pixels remain intact, hidden-page updates
  do not paint over details, and ordering survives theme/page changes.
- Native-rendered previews at 11, 99, 111 and 199 were inspected in day/night
  themes. Icons fit inside the physical round panel with clear separation from
  the speed, units and connection indicator.
- Final default image-dimming PlatformIO build passed: 44,224 bytes static RAM
  and 740,501 bytes application flash. Upload to `/dev/cu.usbserial-0001`
  succeeded with flash data hashes verified; the field-log partition was not
  erased.
- Serial boot identifies 0.3.7 and all four synthetic warnings. Observed health:
  47,476 bytes free heap, 45,056-byte largest block, 696 bytes main-task stack
  high-water space, maximum loop draw time 39,601 microseconds, image intensity
  at 100%. No crash was observed. The serial port was closed after capture.

Final ELF SHA256:
`c2cde25f3c137cf2693a7741d7ac95266bf728e2224115c40aa377c73bb1c3b2`.

Artifacts use the `warning-grid-2026-10-30-` prefix: `preview.png`, `build.txt`,
`upload.txt`, `serial.txt` and `dynamic-preview.png`. The dynamic preview shows
seatbelt activation, engine activation, all four active and removal of the engine
warning with the remaining three packed into the first slots, in both themes.
`docs/gauge-preview.png` shows the initial four-icon layout.
Physical appearance awaits owner review.
