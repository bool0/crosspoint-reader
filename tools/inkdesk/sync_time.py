#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 The InkDesk Authors
"""Set the X4's clock from this computer over USB.

    python3 sync_time.py            # sync once
    python3 sync_time.py --watch    # stay running; sync every time the X4 is plugged in

Sends "CMD:TIME <utc epoch> <utc offset minutes>"; the firmware sets its clock
and, if no time zone was chosen yet, adopts this computer's zone.
"""
import argparse
import datetime
import sys
import time

from x4link import X4, DeviceError, find_port


def sync(device):
    try:
        epoch = device.sync_time()
    except DeviceError as e:
        print(f"Not synced: {e}", file=sys.stderr)
        return False
    offset = datetime.datetime.now().astimezone().utcoffset().total_seconds() / 3600
    print(f"Synced {device.port}: {datetime.datetime.fromtimestamp(epoch):%Y-%m-%d %H:%M:%S} (UTC{offset:+g})")
    return True


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--watch", action="store_true", help="keep running and sync whenever the X4 appears")
    args = parser.parse_args()

    device = X4()
    if not args.watch:
        sys.exit(0 if sync(device) else 1)

    print("Waiting for the X4... (Ctrl+C to stop)")
    synced_port = None
    while True:
        port = find_port()
        if port and port != synced_port:
            time.sleep(2)  # let the firmware finish booting
            if sync(device):
                synced_port = port
        elif not port:
            synced_port = None
        time.sleep(1)


if __name__ == "__main__":
    main()
