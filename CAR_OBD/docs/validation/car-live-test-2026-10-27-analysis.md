# Successful live-speed field test — 27 October 2026

The retained car data confirms successful speed polling and LIVE operation.
The owner also confirmed that the physical gauge showed speed correctly.
No firmware changes were made for this log review.

## Saved snapshot and retention limits

- Raw export: [car-live-test-2026-10-27.txt](car-live-test-2026-10-27.txt).
- Export completed with `LOG_END`; 7,922 valid records, sequence
  44,503–52,424, with no internal sequence gaps.
- SHA-256: `54788ae10abfa92aa16675c4e57ba3a6b6b3c86e06cf376cb20109ae35ca61ae`.
- Ring capacity: 7,936 records. It had wrapped, so the beginning of the drive
  and its Bluetooth/ELM handshake are no longer retained in this snapshot.
- Car data is boot 114. Subsequent desk boots 115/116 identify version 0.2.1,
  ELF prefix `1d315af1`. Boot 114's own BOOT record was overwritten.
- Car records cover uptime 14:03.847–24:28.666. Successful speed samples span
  14:03.847–24:26.067: 10 minutes 22.220 seconds. These are boot-relative times,
  not wall-clock timestamps.

## Speed communication

| Metric | Retained sample |
| --- | --- |
| Valid speed samples | 1,936 |
| Observed speed range | 0–54 km/h |
| New-reading rate | 3.110 readings/second |
| Reading intervals, min / median / max | 310 / 320 / 370 ms |
| Reading intervals, mean / 95th percentile | 321.56 / 333 ms |
| Successful replies matched to retained requests | 1,935; first retained sample's request was overwritten |
| Successful response time, min / median / max | 57 / 66 / 116 ms |
| Successful response time, mean / 95th percentile | 67.47 / 79 ms |
| Stale gaps of at least 1.5 seconds between valid readings | None |
| Bluetooth disconnects/retries in retained car window | None |
| Bluetooth write/event-queue or log-queue overflow records in that window | None |

The 54 km/h maximum applies only to retained samples, not necessarily the whole
trip. A typical reconstructed reply was `410D2E\r\r>` = 46 km/h. Successful
matched replies contained one positive speed frame each. No differing-speed
response conflict was observed. The approximately 320 ms sampling interval is
consistent with the configured 250 ms post-response wait plus about 67 ms for
adapter/ECU turnaround and scheduling; screen drawing is paced separately.

## Device health

- All 63 retained car HEALTH samples reported exactly **49,724 bytes free heap**,
  largest free block 49,152 bytes, and main-task stack waterline 1,048 bytes.
  There is no heap decline in these samples; this is not a proof of indefinite
  leak-free operation.
- 62 HEALTH records show LIVE; the final HEALTH record shows DEMO after shutdown.
- Maximum logged drawing/transfer time was **64.108 ms**. Eight of the 63
  reporting windows included a peak above the 50 ms frame target. This records
  window maxima, not the number of slow frames. It did not interrupt the
  observed speed cadence; the owner reported smooth display behavior.
- No reset or Bluetooth disconnect occurred within the retained continuous
  driving-data window. Earlier overwritten history cannot be assessed here.

## Ignition-off transition

The owner confirmed that ignition was switched off while the ESP32 and adapter
remained powered. At the end of boot 114:

1. At 24:26.067 the final valid reply was `410D00` (0 km/h).
2. At 24:26.557 the adapter returned `NO DATA`.
3. At 24:26.561 the UI changed to DEMO, 4 ms after the error and 494 ms after the
   last valid reading; it did not keep displaying an old value as LIVE.
4. The next two requests returned `CAN ERROR` at 24:27.618 and 24:28.666.

The timing is consistent with ECU communication becoming unavailable at the
reported ignition shutdown. These errors occurred after the successful sampling
window, not throughout the drive. They do not by themselves indicate a new
firmware defect. Subsequent desk boots searched for V-LINK and remained in DEMO,
which is expected when the adapter is no longer present.

Only speed was polled. This snapshot contains no actual RPM, coolant, intake
or other vehicle measurements.
