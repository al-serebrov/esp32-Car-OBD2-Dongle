# Two-drive candidate capture — 2026-10-30

## What survived

Both exports completed and their framing was checked. The raw snapshot contains
**7,929 consecutive records**, sequences 214791–222719.
No gaps were found within the exported sequence range.

- The persistent report is **CANDIDATES session 3**, with 605.280 seconds of
  active capture (10 min 5.28 s). It contains 368 successful primary transmission
  samples, 115 alternative transmission samples, and 115 engine-block samples.
  There was one failed primary reply. RPM/IAT reference counts are 115 each.
- It records 3,806 m, maximum 71 km/h, and average 22.65 km/h over valid OBD-speed
  coverage for this latest run. These are local calculations, not dashboard
  Trip A/B and not totals across both drives.
- The raw driving data comes from **boot 200**, from uptime 309.668 to 644.607 s
  (about 5 min 35 s). Candidate samples cover diagnostics elapsed **4:44.940 to
  10:04.940**. Earlier raw records had already rolled over.
- Boots 201/202 are brief subsequent startup/readback records. No earlier driving
  boot is present in this export. The persistent store keeps the latest run,
  so the first drive is not preserved as a separate report in these exports.
- Therefore **both complete 10-minute drives were not retained as raw logs**.
  The latest drive's summary and the important ending of that drive did survive.
- PID 010D's report `success_count=1` marks reuse of normal speed polling; it is
  not a claim that only one speed sample was recorded. There are 550 valid speed
  readings in the retained portion, and 605.190 seconds of valid speed coverage
  recorded in the latest-run summary.

Times below are the device's diagnostics elapsed timer, not a wall clock. The
firmware logs uptime and active-capture time; it cannot independently establish
09:40/21:40. The final selector exercise appears near **9:42 elapsed**, matching
the approximate timing described by the user if that referred to the screen timer.

Raw SHA-256: `1318f83c29459a1a6db8e3cf44e6ae701535dc1561191376412ce9dc126af483`

Persistent-report SHA-256: `35474c991a8b9972ea5cfe47bb7efffb2a94b2a6cb37ee5c8154325c68c3c004`

## Gear reading is now strongly supported

The primary block **7E1 / 21A0** returns a 28-byte payload. Byte 21 supplies
values 1, 2, 3, 4, 5 and 6 during forward driving. The persistent observed-value
set is **0, 1, 2, 3, 4, 5, 6, 14**. The retained raw samples contain all of them.

Its embedded speed matches the surrounding standard 010D speed well: median
absolute difference **0 km/h**, maximum **3 km/h**, across 176 retained primary
samples. The readings occur at different times, with a recorded reference age.
For the 44 samples with an RPM reference less than 1.5 seconds old, median
absolute RPM difference is 17 rpm; larger differences can occur during changing
RPM, so these are correlation checks rather than simultaneous measurements.

This is much stronger evidence for a usable current-gear field than the first
ride's five samples. Gear 14 (0x0E) appears only with selector byte 0x0A in the
retained stationary exercise, now confirmed by the user as R. For this car,
that value represents reverse rather than a fourteenth forward gear.
Gear zero occurs with the two other stationary selector states.

### Selector / PRND mapping correlated with the driver’s confirmed sequence

**Byte 20 of the successful 21A0 block** distinguishes four states. This is
separate from the service-22 selector request that was rejected on the first run.

| Byte 20 | Selector on the tested car | Evidence in retained capture |
| --- | --- | --- |
| 0x08 (8) | D | 128 samples; forward gears 1–6, plus first gear during final low-speed positioning |
| 0x09 (9) | N | Stationary, gear 0; confirmed N immediately before R |
| 0x0A (10) | R | Five stationary samples with current gear 0x0E (14); driver confirmed R |
| 0x0B (11) | P | Stationary, gear 0; confirmed P immediately after R |

The user clarified: **“I stopped on D at 9:42 and then went -> N -> R -> P.”**
This matches the recorded 0x08 → 0x09 → 0x0A → 0x0B sequence. The initial report’s
provisional P/N labels were reversed and are corrected here. The mapping is now
correlated with both this driver’s physical selector positions and the driving
data on this vehicle; it is not a universal definition for every Hyundai model.

### The ending of the selector exercise was recorded

| Diagnostics elapsed | Samples | Byte 20 | Current gear byte | Driver-correlated state |
| --- | ---: | --- | --- | --- |
| 9:04.390–9:41.310 | 26 | 0x08 | 1 | D, low-speed positioning then stopped |
| 9:42.520–9:49.302 | 5 | 0x09 | 0 | N |
| 9:50.820–9:56.800 | 5 | 0x0A | 14 | R |
| 9:58.290–10:04.940 | 6 | 0x0B | 0 | P |

The first/last sample timestamps are observed bounds, not exact physical shift
times. Multiple samples survived in these final states, so the shorter reported
pauses did not prevent useful observations.

The recording ends in **P**, exactly as the user intended. Diagnostics was
disabled at uptime 630.011 s, about 10:05.28 elapsed, after the final P samples.
There was no intended final D stage: the earlier inference that D might have
been missed was based on assuming a P → R → N → D test order. D was captured
before the stationary N → R → P sequence and during the forward drive. No
additional ride is needed merely to identify these four selector states.

### CMD contains flags as well as a gear-like value

Byte 22 includes values such as 0x15 (21) and 0x16 (22). Its **low nibble** gives
5 and 6. The low nibble matches the current forward gear in 123 of 128 samples;
for all five differences, the next primary sample reports the low-nibble
value as the new current gear (5→4, 3→4, 4→5, 1→2 and 2→1). This strongly
supports a commanded/next-gear interpretation. The upper bits remain unidentified. We
should not display the whole raw byte as a selected gear or assign meanings to
its flag bits without further validation.

The alternative 21A1 block decoded correctly in all 67 retained samples, but its
published candidate gear byte 4 stayed zero. That particular offset is not a
usable current-gear definition here. The remaining changing bytes are preserved
for future analysis; no brake/seatbelt semantics have been invented for them.

## Temperature candidates and other data

- Persistent first/latest transmission-temperature candidate: **52 → 60 C**.
  The retained latter half ranges **58–60 C**. The smooth progression is plausible,
  but still lacks an independent transmission-temperature reference.
- Persistent first/latest oil-temperature candidate: **72 → 81 C**; retained
  latter-half range **78–81 C**. It remains a candidate, not a verified oil sensor.
- The same engine block also contains the published coolant and IAT fields.
  Against their standard references, 67 comparisons give median absolute
  differences of **0.5 C for coolant** and **0.25 C for IAT**, with a 1.5 C maximum
  for each. This supports the byte alignment/formula family in this engine
  block, but does not independently prove the oil-temperature byte's identity.
- No additional body/light/handbrake/seatbelt requests were part of this focused
  profile. This run does not establish those signals.
- Retained MIL monitor replies all report the Check Engine lamp off and count
  zero. Full confirmed/pending/permanent code-list requests were not scheduled
  in this profile, so this is not a complete scan of every ECU's faults.
- Near the end ATRV reported **12.4 V**, which correctly activated the configured
  below-12.6 V battery icon. This is the adapter's supply measurement; these logs
  alone do not diagnose the vehicle's charging system.

## Capture health and a retry issue

- 176 successful primary, 67 alternative, 67 engine, 67 RPM and 67 IAT samples
  are present in the retained raw window, plus one failed primary reply.
- Normal primary-sample gap median: **1.490 seconds**. There is one **62.180-second
  gap**, from uptime 402.948 to 465.128 s.
- At uptime 404.584 s the adapter returned only the first two indexed frames of
  a declared 30-byte 21A0 response, then a prompt. The decoder correctly rejected
  this incomplete response. It was not the old padding bug.
- The existing 60-second failure backoff then paused this known responsive item.
  Other candidate/reference reads and speed continued. A sensible follow-up is
  quicker bounded retries for transient malformed replies from proven blocks,
  while retaining longer backoff for unsupported/negative requests. No firmware
  change was made while exporting/analyzing this run.
- 550 retained speed samples: median gap **730 ms**, maximum **1,010 ms**. There
  were no Bluetooth retry/disconnect events, monitor errors, missing-prompt
  timeouts or log-queue-overflow records in the retained car window.
- Logged worker stack headroom was 1,588 bytes, app headroom 1,144 bytes, queue
  pending zero. These are periodic observations, not exhaustive profiling.

One separate UI issue appears on leaving diagnostics: a brief DEMO transition
at uptime 630.581 s with a wrapped unsigned `reading_age_ms`, followed by LIVE
at 630.952 s. The main loop samples its time before a potentially expensive page
redraw, then reads a newer speed timestamp afterward. That can make a current
reading look momentarily future-dated; source inspection and the timing support
this explanation. It is a UI timing/age calculation issue, not evidence of an
OBD disconnection. It should be corrected separately before treating that age
as a real stale-data interval.

## Saved artifacts

- `candidate-ride-2026-10-30.txt`: unchanged raw export.
- `candidate-ride-2026-10-30.diagnostics.txt`: unchanged persistent report.
- `candidate-ride-2026-10-30-summary.json`: reconstructed candidate payloads,
  contextual references/ages, selector runs and calculated comparisons.
- `candidate-ride-2026-10-30-tcm.csv`: primary TCM samples for inspection.
- `candidate-ride-2026-10-30-decode.py`: reproducible offline decoder/validation.

The most useful outcome is a strongly supported forward-gear reading and a
four-state selector mapping correlated with the driver’s confirmed D → N → R → P
sequence. For this car the evidence supports showing P/R/N/D and D1–D6, with
unknown values handled explicitly. Temperature identities and the upper CMD
flag bits remain separate validation questions.
