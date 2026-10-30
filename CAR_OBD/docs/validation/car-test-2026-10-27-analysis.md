# Car-test diagnosis — 27 October 2026

The ESP32 successfully found, authenticated with, and connected to V-LINK. The
main failure was firmware rejection of different supported-PID replies, before
the first speed request was sent.

## Preserved evidence

- Full export: [car-test-2026-10-27.txt](car-test-2026-10-27.txt).
- 2,786 records, ordered continuously from sequence 1 through 2,786; export ended
  with `LOG_END`. This includes earlier desk-test boots as well as the car tests.
- SHA-256: `fcc972b1367e769254ff131257c9a01310fe619d58478f0d93f5a032f8b3771b`.
- Tested firmware: 0.2.0, ELF prefix `e57f7955`.
- Adapter: `V-LINK`, MAC `10:21:3E:4D:61:66`, SPP channel 1 (`Port`).
- Adapter identification response: `ELM327 v2.3` (self-reported).

| Boot | Observed result |
| --- | --- |
| 106 | Authentication succeeded. One early connection dropped; retry connected and completed ELM initialization. 55 capability responses were rejected as conflicting. |
| 107 | Bluetooth and ELM initialization succeeded. Two protocol searches returned `UNABLE TO CONNECT`; the log does not establish whether ignition/ECU availability changed. |
| 108 | Bluetooth and ELM initialization succeeded. 76 capability responses were rejected as conflicting over about 6 minutes 39 seconds. |

No `TX 010D` request or valid speed event occurred in these three sessions.
DEMO was therefore expected: the old firmware never reached speed polling.

## Exact failure

Boot 108, sequences 2289–2307, includes this exchange:

```text
BT_OPEN mac=10:21:3E:4D:61:66 status=0 handle=129
TX ATZ\r
RX \r\rELM327 v2.3\r\r>
... ATE0, ATL0, ATS0, ATH0, ATSP0 each returned OK ...
TX 0100\r
RX SEARCHING...\r
RX 410080000001\r4100BE3EB813\r
RX \r>
ELM_ERROR reason=conflicting ECU responses
```

The two complete capability replies are:

```text
41 00 80 00 00 01
41 00 BE 3E B8 13
```

Multiple responding controllers may advertise different sets of supported PIDs.
The old generic parser incorrectly required all matching replies to contain
identical payloads, applying a measurement rule to capability bitmaps. `Elm327`
then remained at the capability-discovery stage and retried `0100` every five
seconds. One bitmap advertises PID 0D: byte `3E` has its `08` bit set.

With headers disabled, these logs do not identify the individual controller
addresses. Multiple-controller replies are the normal explanation for different
capability maps; the diagnosis does not depend on identifying which is the
engine or transmission controller. See [ELM327 datasheet, Talking to the Vehicle](https://www.elmelectronics.com/wp-content/uploads/2016/07/ELM327DS.pdf)
and [OBD Now Terminal guide, More than one ECU](https://glmsoftware.com/documentation/obdnowterminaluserguide.pdf).

## Correction: version 0.2.1

- Combine valid four-byte `0100` capability maps using bitwise OR.
- After discovery, advance to the existing `010D` speed request.
- Keep rejecting malformed maps and conflicting actual speed measurements.
- Keep existing Bluetooth, pinout, display, polling and log-partition settings.
- Preserve the original exported log unchanged.

Regression tests use both recorded reply orders, disjoint maps to verify the OR
operation, malformed second replies, and the full initialization-to-speed
transition. The new test failed on 0.2.0, then passed with the fix. The complete
host sanitizer suite and native ESP-IDF build passed.

Version 0.2.1 was uploaded successfully. A 16-second post-upload serial capture
confirmed version 0.2.1, ELF prefix `1d315af1`, SPP initialization status 0,
discovery/retry, and continued DEMO rendering without a crash. The sample
reported 50,072 bytes free heap and a 25,882 µs maximum drawing/transfer time.
See [parser-fix-0.2.1-boot.txt](parser-fix-0.2.1-boot.txt). V-LINK was absent at
the desk, so this does not validate actual speed communication.

Actual `010D` responses are not present in the captured car logs. A new car test
is required to verify live speed; a successful Bluetooth connection alone is
not proof of a valid speed reading. At rest, a valid `41 0D 00` should show
**0 km/h / LIVE**.
