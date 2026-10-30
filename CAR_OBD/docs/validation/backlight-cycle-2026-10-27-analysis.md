# Labelled brightness loop, 2026-10-27

The owner could not confidently distinguish the manually timed brightness
steps and requested a temporary on-screen percentage with a repeating loop.
The existing GPIO4 -> 1 kOhm resistor -> LED wiring is retained.

Firmware 0.2.3 in `esp32doit-devkit-v1-backlight-test` now enables both
`OBD_BACKLIGHT_PWM=1` and `OBD_BACKLIGHT_CYCLE=1`. It repeats 0%, 50%, 100%,
six seconds per stage, using the main task's monotonic clock without blocking
gauge rendering or OBD work. An amber `LED n%` label shows commanded PWM duty
beneath KM/H. Each label is transferred before applying its associated duty.
At zero, the backlight should be off and the label is expected to be invisible.
Manual brightness commands return an explicit error while the loop is active.

The first on-device capture confirmed multiple repetitions with `ESP_OK` at
duties 0, 512 and 1023. A native-renderer preview then exposed interference with
the bottom scale labels. The label was moved below those labels, replacing the
small separator during the test. The final preview was visually inspected.
The layout adjustment was rebuilt and uploaded with successful hash verification.

Final ELF SHA256:
`15752881036ebab13e2c8bd611dc683f1dc985ad51d043673c8d4ea0297da04c`.

Existing host checks for rendering, OBD and the flash journal passed after
adding the overlay and cycle. The serial captures establish PWM software
operation; physical dimming and legibility still require the owner's observation.
No light sensor or automatic day/night control is included.

The automatic loop remains active after closing the serial capture. It restarts
on each boot. Remove `-DOBD_BACKLIGHT_CYCLE=1`, rebuild and upload to return to
manual PWM, or use the base environment for the original LED -> 3V3 wiring.

Artifacts:

- `backlight-cycle-2026-10-27-build.txt`: initial loop build.
- `backlight-cycle-2026-10-27-upload.txt`: final layout build/upload.
- `backlight-cycle-2026-10-27-serial.txt`: initial repeated-cycle capture.
- `backlight-cycle-2026-10-27-final-serial.txt`: final firmware startup/cycle.
- `backlight-cycle-label-preview.png`: native-renderer preview of the final label.
