#!/usr/bin/env python3
"""Read retained flash logs without erasing them or explicitly resetting ESP32."""
import argparse
from datetime import datetime
from pathlib import Path
import time

import serial


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", default="/dev/cu.usbserial-0001")
    parser.add_argument("--output", type=Path, default=Path(f"fieldlogs-{datetime.now():%Y%m%d-%H%M%S}.txt"))
    parser.add_argument("--timeout", type=float, default=300)
    parser.add_argument("--diagnostics", action="store_true", help="Also export the durable discovery report beside the raw logs")
    args = parser.parse_args()
    port = serial.Serial()
    port.port, port.baudrate, port.timeout = args.port, 115200, 0.5
    port.dtr = port.rts = False
    port.open()
    started = False
    finished = False
    args.output.parent.mkdir(parents=True, exist_ok=True)
    try:
        if args.diagnostics:
            time.sleep(3)
            port.write(b"\ndiagreport\n")
            report_path = args.output.with_suffix(".diagnostics.txt")
            report_started = report_finished = False
            deadline = time.monotonic() + 30
            with report_path.open("w", encoding="utf-8") as report:
                while time.monotonic() < deadline:
                    line = port.readline().decode("utf-8", errors="replace").strip()
                    if line.startswith("DIAG_REPORT_ERROR"):
                        report.write(line + "\n")
                        print(line)
                        break
                    if line.startswith("DIAG_REPORT_BEGIN "):
                        report_started = True
                    if report_started and line:
                        report.write(line + "\n")
                        if line == "DIAG_REPORT_END":
                            report_finished = True
                            break
            print(f"Diagnostic report {'saved' if report_finished else 'unavailable/incomplete'}: {report_path}")
        deadline = time.monotonic() + args.timeout
        next_request = time.monotonic() + 3
        with args.output.open("w", encoding="utf-8") as output:
            while time.monotonic() < deadline:
                line = port.readline().decode("utf-8", errors="replace").strip()
                if line.startswith("LOG_BEGIN "):
                    started = True
                if started and line:
                    output.write(line + "\n")
                    if line == "LOG_END":
                        finished = True
                        break
                elif line.startswith("LOG_ERROR"):
                    raise SystemExit(line)
                if not started and time.monotonic() >= next_request:
                    port.write(b"\nlogs\n")
                    next_request = time.monotonic() + 10
    finally:
        port.close()
    if not finished:
        raise SystemExit(f"Incomplete export; partial output retained at {args.output}")
    print(f"Saved retained logs to {args.output} (device logs were not cleared)")


if __name__ == "__main__":
    main()
