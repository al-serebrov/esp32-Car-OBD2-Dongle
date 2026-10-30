# Black-display firmware recheck — 27 October 2026

- Retained log export completed: 7,932 records, boots 114–121. Raw snapshot:
  [black-display-recheck-fieldlog-2026-10-27.txt](black-display-recheck-fieldlog-2026-10-27.txt).
- A fresh reset/startup trace confirms version 0.2.1, ELF prefix `1d315af1c`,
  matching the firmware used for the successful car test and the local image.
- Local firmware image checksum and appended validation hash are valid.
- Current source has no GPIO4/backlight PWM configuration. Backlight remains
  externally powered; display VCC and LED are documented on 3V3.
- The controller is commanded off temporarily during initialization, then on
  before the splash/demo loop. Every relevant API result is checked. Reaching
  HEALTH confirms the application got past those calls and continues drawing.
- Fresh trace shows GC9A01 configured, continued DEMO operation, 50,080 bytes
  free heap, and 24,281 us maximum render/transfer time in the first reporting
  window. No crash or brownout message appears in the capture.
- Host renderer, parser, and flash-journal sanitizer checks passed again.
- One old retained log-queue overflow record occurs in boot 116, consistent with
  the earlier long USB export pausing flash writes; it is not a backlight-control
  event. Recent inspected boots 117–121 contain no stored error/overflow events.

## Limit

The TFT SPI interface is write-only. Successful driver calls and DMA transfers
cannot establish that a physical panel has power, that its wires are connected,
or that its backlight emits light. The user reports a completely unlit screen.
The next discriminating observation is DC voltage measured at display VCC-to-GND
and LED-to-GND (expected around 3.3 V in the restored wiring), or inspection of
an actual wiring photo if a meter is unavailable. No display-damage diagnosis
can be made from these logs alone.

No firmware source changes or reflashing were performed during this recheck.

Startup capture: [black-display-recheck-startup-2026-10-27.txt](black-display-recheck-startup-2026-10-27.txt).

Raw snapshot SHA-256: `9227ff3810e960d076b9ce1395f0675a2ebd96c3c3b313b16cbde20a521de2dd`.
