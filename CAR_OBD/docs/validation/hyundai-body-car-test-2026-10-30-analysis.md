# Hyundai body-status field test, 2026-10-30

The ESP32 was detected at `/dev/cu.usbserial-0001` and its retained flash logs
were exported through LOG_END. The port was then closed. No firmware was changed
or flashed during this review, and logs were not cleared.

**Both experimental 770/778, 22BC03 reads returned NO DATA.** There were no body
payloads or candidate brake/buckle bits to correlate. This does not establish
that the ix35 cannot expose these states; the current candidate transaction
yielded no data under these test conditions. Longer holds or faster polling alone
would not resolve a request that receives no payload.

## Snapshot and user sequence

- Raw export: `hyundai-body-car-test-2026-10-30.txt`.
- Metrics: `hyundai-body-car-test-2026-10-30-summary.json`.
- Size: 583,979 bytes; 7,927 records, sequences 155927–163853 without internal
  sequence gaps. Older boots 172–181 are also retained in this circular log.
- This car test is boot **182**, firmware **0.3.8**, ELF prefix `7875f8cf`.
  Its 1,190 records span BOOT at 2.407 seconds through the last CAN error at
  109.417 seconds. Boot 183 is the subsequent USB/desktop startup.
- User sequence: ignition on and engine running; handbrake disengage/engage
  several times at approximately five-second intervals; driver belt fasten/
  unfasten several times at similar intervals; finish belt fastened and handbrake
  released, wait five seconds, switch the car off. There are no timestamped
  physical-event markers, so exact per-action timings cannot be reconstructed.

Raw export SHA256:
`ab7d13b41e874ed0ee78a0d8ee68e4957a72867611fdd5c3c53f07722b1114a6`.

Times below are boot-relative uptime, not wall-clock time.

## Connection and body reads

V-LINK `10:21:3E:4D:61:66` was discovered at 7.376 seconds and SPP connected
at 16.742 seconds. It identified itself as ELM327 v2.3. Standard initialization
succeeded, and the two 0100 capability replies were combined. ATDPN returned
**A6**, passing the 11-bit CAN / 500 kbit/s protocol gate.

| Event | First attempt (seconds) | Second attempt (seconds) |
| --- | ---: | ---: |
| Transaction begins | 27.211 (ATDPN) | 87.971 (ATSH770) |
| 22BC03 sent | 27.441 | 88.121 |
| NO DATA received | 27.678 | 88.360 |
| Standard route restoration acknowledged | 27.817 | 88.497 |
| Next valid speed response | 27.918 | 88.598 |

All ATSH770/ATCRA778 setup commands returned OK. Both ATSH7DF/ATCRA restoration
sequences also returned OK, followed by valid 410D00 speed replies. No optional
timeout, restoration failure, probe suppression or reconnect occurred. Body read
turnaround was 237 and 239 ms. The sixty-second failure cooldown explains why
there were only two probes. The five-second toggle spacing is not the limiting
observation: neither probe received a body-data frame in the first place.

## Speed, health and shutdown

- **261 valid speed samples**, all **0 km/h**, consistent with a stationary test.
- Samples span 19.112–104.928 seconds (85.816 seconds), approximately **3.03 Hz**.
- Intervals: minimum 309 ms, median 320 ms, mean 330.06 ms, maximum 966 ms.
  No interval reached the 1,500 ms stale threshold.
- Probe-adjacent gaps were 966 ms and 880 ms. The UI stayed LIVE during both
  transactions; probes briefly slowed speed updates without losing LIVE.
- LIVE began at 19.141 seconds. No Bluetooth disconnect/retry, transport/event
  overflow or incomplete-response record appears in this boot.
- After the last valid speed, five CAN ERROR replies occurred at 105.217 through
  109.417 seconds. DEMO resumed at 105.241 seconds, 313 ms after the last valid
  reading. This is consistent with the reported ignition shutdown, rather than
  a failure during the earlier live test.

Connected HEALTH records consistently show free heap 47,116 bytes, largest
block 45,056 bytes and main-task stack waterline 648 bytes. No declining heap,
panic or in-session reboot is visible. Only speed and the candidate body request
were polled; there is no current coolant measurement to validate a temperature
threshold. All four warning icons remain synthetic samples.

## Next step

Obtain an LM-specific request/decoder or a working Car Scanner ELM debug capture,
first checking that the selected profile exposes changing parking-brake and
driver-buckle live sensors. A selected profile alone is insufficient evidence.
The current log cannot distinguish an absent/inaccessible module, a different
address or DID, or another required transport setup. Do not infer states from
NO DATA or change bit offsets without real payloads and controlled transitions.
