#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 The InkDesk Authors
"""Copy files to the X4's SD card over USB (no Wi-Fi needed).

    python3 push_file.py fonts/WenKai/*.cpfont --dest /fonts/WenKai/
    python3 push_file.py 购物清单.txt --dest /memos/

Each file goes over "CMD:PUT"; the device acknowledges every 4 KB chunk and
reports a CRC32 that is checked against the local file.
"""
import argparse
import os
import sys
import time

from x4link import X4, DeviceError


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("files", nargs="+")
    parser.add_argument("--dest", required=True, help="SD folder (ending in /) or full path for a single file")
    args = parser.parse_args()

    device = X4()
    for local in args.files:
        remote = args.dest + os.path.basename(local) if args.dest.endswith("/") else args.dest
        start = time.time()

        def progress(done, total, remote=remote, start=start):
            rate = done / max(time.time() - start, 1e-3) / 1024
            print(f"\r  {remote}: {done * 100 // max(total, 1)}%  ({rate:.0f} KB/s)", end="", flush=True)

        try:
            with open(local, "rb") as f:
                device.put(remote, f.read(), progress)
        except DeviceError as e:
            sys.exit(f"\n{remote}: {e}")
        print()
    print("Done.")


if __name__ == "__main__":
    main()
