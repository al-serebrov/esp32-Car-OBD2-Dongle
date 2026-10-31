# Repository instructions for coding agents

## Start here

Read [README.md](README.md) for hardware, wiring, supported screens and the
build walkthrough. The active firmware project is **`CAR_OBD/`**; run PlatformIO
there. `Car OBD Dashboard/` is an old starter skeleton.

Before editing, inspect `git status` and preserve existing staged/unstaged work.
Use these files as the source of truth:

- `CAR_OBD/platformio.ini`: platform, board and build environments.
- `CAR_OBD/include/Config.h`: adapter name/PIN, timing, warnings and display settings.
- `CAR_OBD/include/Pins.h`: hardware pin assignments.
- `CAR_OBD/sdkconfig.defaults`, `CAR_OBD/partitions.csv`: Bluetooth/flash configuration.
- `CAR_OBD/src/idf_component.yml`, `CAR_OBD/dependencies.lock`: component versions.
- [CAR_OBD/README.md](CAR_OBD/README.md): detailed implementation notes; older
  validation reports describe historical firmware, so check current source first.

## When the user asks to set up this repository

Carry out the software setup and validation, rather than only listing commands.
Hardware is not required to build or run host checks. Do useful setup work even
if the ESP32, display or car is unavailable.

1. Detect the OS, available Python, PlatformIO and host C++ compiler. Reuse an
   existing working PlatformIO installation, including the VS Code extension's
   environment. A missing `pio` on PATH does not prove PlatformIO is absent.
2. If needed, install the Python tools in an isolated environment as below.
   Resolve required host build tools using the host's normal installation
   mechanism and permissions. Avoid replacing system Python or upgrading
   unrelated global tools.
3. Build the default firmware with the repository's pinned platform/components.
4. Run the native host checks. Diagnose and resolve setup failures; do not
   weaken the checks or silently change dependency pins to get a green result.
5. List serial ports if useful. No attached board is a normal outcome, not a
   failed software setup.
6. Report the tool paths/versions used, build and check results, and any specific
   blocker. Separate software readiness from hardware/car validation.

Do not flash a connected board merely because the task says "set up". Flash
when the user's request includes uploading/flashing and the target is identified.
Honor any existing authorization; do not repeatedly ask for it. Do not erase
flash, alter the partition layout, change global Git identity, rewrite history
or publish/push anything as a side effect of setup.

Ask for hardware details only when needed for wiring, flashing or requested
configuration changes. Do not block desktop setup on learning the car model.

## Python / PlatformIO bootstrap

The reference working environment uses Python **3.11** and PlatformIO Core
**6.2.0**. Reuse a working installation when available. For a fresh agent/CI
environment, create a repository-local virtual environment from the repository
root (commands below use a POSIX shell):

```sh
python3 -m venv .venv
.venv/bin/python -m pip install "platformio==6.2.0" pyserial
```

Choose Python 3.11 explicitly for `venv` if needed, for example
`python3.11 -m venv .venv`. If a newer system Python causes toolchain dependency
problems, use the known-working interpreter rather than changing firmware pins.
On Linux, the Python venv package may need installation first.

On Windows, use `python -m venv .venv` and `.venv\Scripts\python.exe` in place
of `.venv/bin/python`. The PlatformIO IDE installation is also supported. See
the [official installation guide](https://docs.platformio.org/en/latest/core/installation/index.html)
if the tools are unavailable. `.venv/` is ignored by Git.

PlatformIO fetches its compiler, ESP-IDF and managed components during the first
build; network access and some download time are expected. Do not install
Arduino, LVGL or a separate ESP-IDF SDK for this project. No cloud credentials
or project-specific API keys are required.

## Build and checks

With an existing `pio` command, run from `CAR_OBD/`:

```sh
pio run -e esp32doit-devkit-v1-image-dimming
sh test/run_host_checks.sh
pio device list
```

With the root `.venv/`, run from `CAR_OBD/` instead:

```sh
../.venv/bin/python -m platformio run -e esp32doit-devkit-v1-image-dimming
sh test/run_host_checks.sh
../.venv/bin/python -m platformio device list
```

The build uses Espressif32 **6.12.0**, ESP-IDF **5.5.0**, original
`esp32doit-devkit-v1`, and GC9A01 component **2.0.4**. The default environment
scales rendered image brightness for the seven-pin display; it does not control
backlight power. Keep `platformio.ini`, the component lock and the partition
table consistent.

Host checks require a POSIX shell and a C++17 compiler with AddressSanitizer and
UndefinedBehaviorSanitizer. macOS command-line developer tools or an appropriate
Linux compiler/runtime provide this; Windows users can run the checks in WSL.
The script accepts `CXX` to choose the compiler and needs no attached hardware.
If host checks cannot run, report the missing prerequisite; a successful firmware
build alone does not count as passing those checks.

For firmware changes, run the build and relevant checks, including the full host
script for shared UI/OBD/logging changes. For documentation-only work, verify
paths, links and commands against the source; no hardware upload is needed.

## Hardware and vehicle boundaries

- Original ESP32 DevKit V1 with 4 MB flash and Bluetooth Classic SPP. ESP32-C3,
  ESP32-S3, BLE-only adapters and Wi-Fi adapters do not match this implementation.
- Current panel: 240x240 GC9A01, VCC=3V3, GND=GND, CS=25, RST=26, DC=27,
  MOSI=23, SCK=18. Button: GPIO21 to GND, internal pull-up. GPIO4 is disconnected
  on the current seven-pin panel. Follow `Pins.h` and the user's actual module.
- Adapter defaults: discoverable name `V-LINK`, legacy pairing PIN `1234`.
  Change these only when requested or the user supplies their adapter details.
- Standard OBD readings still depend on ECU support. Gear/PRND decoding and the
  focused diagnostic requests are specific to the tested Hyundai LM / ix35.
  Do not present them as universal or infer unverified warning bits.
- For a different vehicle, adapt or disable the manufacturer-specific paths in
  `TransmissionPoll.*` / `DiagnosticRun.*`; there is no vehicle-profile selector.
  Setting `kCandidateDiagnostics=false` does **not** disable manufacturer probes:
  it selects the older, broader discovery sweep.
- Preserve one outstanding ELM327 command, speed interleaving, timeout recovery
  and header/filter restoration. Extra polling can slow speed updates.
- Do not enable dormant handbrake/seatbelt probes as a setup step. Do not add
  fault-clearing, actuator tests or ECU configuration commands for setup.
- Bench build/tests cannot establish physical wiring, display correctness,
  vehicle PID availability or manufacturer mapping accuracy. Report those as
  unverified until actually observed.

## Flashing and log retrieval

When flashing is requested, identify the actual port with `pio device list`.
Never assume the previous developer's serial port or select an arbitrary board
when multiple devices are connected. From `CAR_OBD/`:

```sh
pio run -e esp32doit-devkit-v1-image-dimming -t upload --upload-port PORT
pio device monitor --port PORT --baud 115200
```

Replace `PORT` before execution; substitute the chosen PlatformIO executable if
needed. Close the monitor before uploading or starting the export utility.
Preserve the 4 MB partition layout and saved data during ordinary updates.

For an explicitly requested log export, using an interpreter with pyserial:

```sh
python3 tools/read_field_logs.py --port PORT --diagnostics --output docs/validation/my-ride.txt
```

This reads the persistent ring and saved diagnostics report. Old ring records
are overwritten as the device runs; there is no unlimited trip archive. Raw
captures, build/upload transcripts and derived ride files are intentionally
ignored. Keep them local and do not force-add them. Preserve reports, UI images,
host-check outputs and small recorded-response parser fixtures. Some historical
report links refer to local-only files and will be absent in a fresh checkout.

## Making changes

- Preserve the four-page order, button gestures, themes, DEMO/LIVE freshness and
  warning behavior unless the task changes them. Read `main.cpp` and telemetry
  types before extending a screen or adding a new reading.
- Keep parsers, schedulers and gesture logic host-testable; keep drawing out of
  Bluetooth callbacks and avoid heap allocations in hot rendering paths.
- Generated assets are checked in. Do not hand-edit `SpeedFont.h` or
  `DrivingAssets.h`. Only if artwork regeneration is required, install
  `Pillow==12.3.0` in a suitable Python environment and run the scripts under
  `CAR_OBD/tools/`. Normal firmware builds do not need Pillow.
- Check `git diff --check` and review your diff. Preserve user changes, local
  captures, firmware configuration and the font license/provenance.
- Finish with what changed, commands actually run, results, and any remaining
  hardware or vehicle checks. Never claim an unrun test or unobserved car result.
