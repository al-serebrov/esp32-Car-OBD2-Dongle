# Bluetooth field-debug bench validation

Raw device captures, build/upload transcripts and derived ride data are kept
locally and ignored by Git. They may be referenced by the reports below but
are not included in new checkouts. Analysis notes, preview images, host-check
results and parser fixtures remain versioned. Earlier commits retain the
original captures; history has not been rewritten.

- Firmware: 0.2.0; final build ELF prefix `e57f7955`.
- Native ESP-IDF build/upload succeeded on ESP32-D0WD-V3, 4 MB BOYA flash.
- Classic SPP initialized successfully; real nearby names/MACs/RSSI were recorded.
- V-LINK was absent at the desk: DEMO continued and discovery retried.
- Final build reported about 50 KB free heap, 49 KB largest block, and a 1,048-byte main-task stack waterline during scanning.
- Observed reporting-window maximum render/transfer times were below the 50 ms frame budget in the sampled run. These are bench observations, not worst-case guarantees.
- USB snapshot export succeeded before and after reset. All 461 baseline records were retained byte-for-byte; the second export contained 631 records in sequence order.
- Host sanitizer checks passed: gauge/banded-framebuffer equivalence, labels, simulation, parser/framing/scheduling, stale LIVE policy, flash rollover/CRC, interrupted writes and erases.
- Pending field verification: V-LINK pairing, ELM initialization against the adapter, actual ECU speed replies, and in-car disconnect recovery.

`fieldlog-before-reset.txt` and `fieldlog-after-reset.txt` are device flash snapshots. They retain earlier implementation-test boots too; use BOOT ELF prefixes to distinguish builds. Earlier `b7c2d4bd` boots preceded the banded-framebuffer allocation fix.

`bluetooth-bench-serial.txt` is the earlier successful banded-framebuffer startup/scan capture, before adding the independent USB console task. `serial-log.txt` is the original display-only milestone capture.
