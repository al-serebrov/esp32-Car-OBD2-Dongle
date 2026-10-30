# Warning-icon layout prototype, 2026-10-30

Firmware 0.3.6 is a visual prototype. The speed page always displays both an
amber engine-outline icon and a red parking-brake icon (circled P with outer
brackets), regardless of DEMO/LIVE state. Neither icon represents a vehicle
reading. No diagnostic requests, DTC polling or Hyundai-specific commands were
added. A `WARNING_PREVIEW` boot record explicitly identifies the synthetic source.

The owner selected Car Scanner's **Hyundai Tucson LM, ix35 EL 2.0/2.4 MPI 2WD
(2009–2015)** profile. This is recorded for subsequent diagnostic research, not
treated as proof that the adapter can retrieve parking-brake state.

Speed uses the same Roboto Condensed Bold font, regenerated at 46x96 rather than
58x120 pixels per cell. Glyphs still align by their right edge with leading-bearing
centering, retaining stable positions throughout 100–199. The numeric region
ends at row 147, KM/H occupies rows 156–169, and the two warning icons occupy
rows 177–212. The separate yellow/green status indicator remains at rows 214–223.
Icon strokes are sampled at 2x2 resolution and blended into each theme's
background. Day colors are darker for contrast on white. No extra framebuffer
or runtime font dependency is required; digit masks shrink to 22,080 flash bytes.

Validation:

- All host checks passed with address/undefined-behavior sanitizers, including
  incremental/fresh frame equivalence, banded framebuffers, UI/button gestures,
  image intensity, OBD parsing/scheduling and flash journal recovery.
- Added checks verify both warning colors exist in day/night themes and that
  every speed from 0 to 255 plus DEMO/LIVE changes preserves the complete warning
  region. Existing checks cover switching pages/themes and clearing the dim test.
- Native-renderer previews at 11, 99, 111 and 199 in both themes were visually
  inspected for readable icons, centering, spacing and arc clearance.
- Default image-dimming PlatformIO build passed: 44,224 bytes static RAM,
  747,733 bytes application flash. Upload to `/dev/cu.usbserial-0001` succeeded
  with flash data hashes verified. The field-log partition was not erased.
- Serial boot reports 0.3.6, IMAGE brightness at 100%, and synthetic warning
  preview enabled. Observed health: 47,460 bytes free heap, 45,056-byte largest
  block, 712 bytes main-task stack high-water space, maximum loop draw time
  47,266 microseconds. No panic, watchdog or assertion was observed; the USB
  serial capture was closed after verification.

ELF SHA256:
`9ba799987c2fee6ca168b1f34dc348edf42efe4cb112038fea9c179ab60fc39b`.

Artifacts:

- `warning-preview-2026-10-30-preview.png`
- `warning-preview-2026-10-30-build.txt`
- `warning-preview-2026-10-30-upload.txt`
- `warning-preview-2026-10-30-serial.txt`

Physical appearance awaits owner review. The next diagnostic stage must replace
synthetic lights with verified, fresh vehicle states; silence or unavailable data
must not be interpreted as an all-clear result.
