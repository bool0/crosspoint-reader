#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 The InkDesk Authors
"""Set the X4's clock from this computer over USB.

    python3 sync_time.py            # sync once
    python3 sync_time.py --watch    # stay running; sync every time the X4 is plugged in

Sends "CMD:TIME <utc epoch> <utc offset minutes>" to the CrossPoint firmware,
which sets its clock and, if no time zone was chosen yet, adopts this
computer's zone.
"""
import argparse
import datetime
import glob
import sys
import time

import serial  # pyserial


def find_port():
    ports = sorted(glob.glob("/dev/cu.usbmodem*")) or sorted(glob.glob("/dev/ttyACM*"))
    return ports[0] if ports else None


def sync(port):
    offset = datetime.datetime.now().astimezone().utcoffset()
    offset_minutes = int(offset.total_seconds() // 60)
    with serial.Serial(port, 115200, timeout=0.2) as s:
        s.reset_input_buffer()
        # Send right on a second boundary so the device clock lands within a few ms.
        time.sleep(1 - (time.time() % 1))
        epoch = int(round(time.time()))
        s.write(f"CMD:TIME {epoch} {offset_minutes}\n".encode())
        s.flush()
        deadline = time.time() + 5
        buf = b""
        while time.time() < deadline:
            buf += s.read(256)
            if b"TIME_OK" in buf:
                local = datetime.datetime.fromtimestamp(epoch).strftime("%Y-%m-%d %H:%M:%S")
                print(f"Synced {port}: {local} (UTC{offset_minutes / 60:+g})")
                return True
            if b"TIME_ERR" in buf:
                break
    print(f"No confirmation from {port} (is CrossPoint running?)", file=sys.stderr)
    return False


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--watch", action="store_true", help="keep running and sync whenever the device appears")
    parser.add_argument("--port", help="serial port (default: auto-detect)")
    args = parser.parse_args()

    if not args.watch:
        port = args.port or find_port()
        if not port:
            sys.exit("X4 not found on USB. Plug it in and wake it.")
        sys.exit(0 if sync(port) else 1)

    print("Waiting for the X4... (Ctrl+C to stop)")
    synced_port = None
    while True:
        port = args.port or find_port()
        if port and port != synced_port:
            time.sleep(2)  # let the firmware finish booting
            try:
                if sync(port):
                    synced_port = port
            except serial.SerialException as e:
                print(f"Serial error: {e}", file=sys.stderr)
        elif not port:
            synced_port = None
        time.sleep(1)


if __name__ == "__main__":
    main()
