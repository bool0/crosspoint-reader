#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 The InkDesk Authors
"""Copy files to the X4's SD card over USB (no Wi-Fi needed).

    python3 push_file.py fonts/WenKai/*.cpfont --dest /fonts/WenKai/
    python3 push_file.py 购物清单.txt --dest /memos/

Each file is sent with "CMD:PUT <size> <path>"; the device acknowledges every
4 KB chunk and reports a CRC32 that is checked against the local file.
"""
import argparse
import glob
import os
import sys
import time
import zlib

import serial  # pyserial

CHUNK = 4096


def find_port():
    ports = sorted(glob.glob("/dev/cu.usbmodem*")) or sorted(glob.glob("/dev/ttyACM*"))
    return ports[0] if ports else None


_pending = b""  # bytes read past the last returned line


def read_line_with(s, prefixes, timeout):
    """Return the first line starting with one of prefixes; log lines are skipped."""
    global _pending
    deadline = time.time() + timeout
    while True:
        while b"\n" in _pending:
            line, _pending = _pending.split(b"\n", 1)
            text = line.decode("utf-8", "replace").strip()
            if text.startswith(prefixes):
                return text
        if time.time() >= deadline:
            return None
        _pending += s.read(max(1, s.in_waiting))


def push(s, local, remote):
    global _pending
    data = open(local, "rb").read()
    s.reset_input_buffer()
    _pending = b""
    s.write(f"CMD:PUT {len(data)} {remote}\n".encode("utf-8"))
    reply = read_line_with(s, ("PUT_READY", "PUT_ERR"), 10)
    if reply != "PUT_READY":
        raise RuntimeError(f"device refused: {reply}")

    start = time.time()
    for off in range(0, len(data), CHUNK):
        s.write(data[off : off + CHUNK])
        ack = read_line_with(s, ("ACK", "PUT_ERR"), 10)
        if not ack or not ack.startswith("ACK"):
            raise RuntimeError(f"transfer failed at {off} bytes: {ack}")
        done = min(off + CHUNK, len(data))
        if done == len(data) or (off // CHUNK) % 64 == 0:
            rate = done / max(time.time() - start, 1e-3) / 1024
            print(f"\r  {remote}: {done * 100 // len(data)}%  ({rate:.0f} KB/s)", end="", flush=True)

    result = read_line_with(s, ("PUT_OK", "PUT_ERR"), 30)
    print()
    if not result or not result.startswith("PUT_OK"):
        raise RuntimeError(f"device reported: {result}")
    device_crc = int(result.split()[1], 16)
    local_crc = zlib.crc32(data) & 0xFFFFFFFF
    if device_crc != local_crc:
        raise RuntimeError(f"CRC mismatch: device {device_crc:08x}, local {local_crc:08x}")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("files", nargs="+")
    parser.add_argument("--dest", required=True, help="SD folder (ending in /) or full path for a single file")
    parser.add_argument("--port", help="serial port (default: auto-detect)")
    args = parser.parse_args()

    port = args.port or find_port()
    if not port:
        sys.exit("X4 not found on USB. Plug it in and wake it.")
    with serial.Serial(port, 115200, timeout=0.2) as s:
        for local in args.files:
            remote = args.dest + os.path.basename(local) if args.dest.endswith("/") else args.dest
            push(s, local, remote)
    print("Done.")


if __name__ == "__main__":
    main()
