# Adaptive speed layout — firmware 0.3.12

Installed ELF SHA-256: `f89c88d6cbad62bd35a09c8aefc79397cc1d899c2c69333a0bcb52c9c5c8b318`.

The speed page previously reserved space for warning icons even with none
active. It now selects a pre-rendered font and layout by occupied warning rows:

| Warnings | Rows | Font cell | Numeral top | KM/H top | Icon row tops |
| --- | --- | --- | --- | --- | --- |
| 0 | 0 | 56x120 | 50 | 182 | None |
| 1–2 | 1 | 48x104 | 42 | 156 | 184 |
| 3–4 | 2 | 42x88 | 38 | 134 | 152, 190 |
| 5 | 3 | 36x76 | 34 | 120 | 140, 168, 196 |

Clearing a row restores larger numerals. Count changes within a row keep font
size and numeral/unit positions stable. The warning arrival order and two-column
packing are preserved. Three-digit readings remain centred by visible ink;
trailing digit positions stay fixed throughout 100–199. All five DEMO warning
icons remain enabled. The temporary USB dim-test label uses the small top gap.

When the row count changes, the renderer clears previous numeral/unit/icon
positions in RAM and draws their replacements before flushing. Arc and status
pixels are preserved. Normal speed updates only erase/redraw the current number
area and changed arc segments. Four-bit font masks occupy 90,720 bytes in flash;
no additional framebuffer or heap allocation was introduced.

## Validation

- Host checks passed with AddressSanitizer/UndefinedBehaviorSanitizer.
- Existing gauge, themes/button, OBD scheduler/discovery and journal tests passed.
- New checks exercise all 32 warning subsets in both themes, upward/downward
  row transitions, numeral size/visible centering, stale pixel removal, speed
  width transitions, hidden-page updates and the optional dimming overlay.
- Circular-mask previews of all four sizes in both themes were inspected.
- PlatformIO ESP-IDF build passed: app usage 849929 bytes; static RAM 54432 bytes.
- Upload to /dev/cu.usbserial-0001 completed and flash hash verification passed.
- Hardware boot reports 0.3.12, ELF prefix f89c88d6. Early health reports 37072
  bytes free heap, largest block 34816, app stack headroom 1144 and max draw
  44797 us. The saved diagnostics report loads valid (7608 bytes, ESP_OK).
- Physical appearance of all live warning-count transitions has not yet been
  observed on the car; the rendered layouts were verified with native previews.

This change is limited to the UI/font assets and related checks/documentation.
The separate padded-OBD-response decoder issue discovered in the ride logs is
still present and has not been folded into this UI change.
