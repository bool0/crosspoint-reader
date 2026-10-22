# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 The InkDesk Authors
"""USB serial link to an X4 running InkDesk, shared by the tools in this folder.

Speaks the firmware's CMD: line protocol (src/network/SerialFileTransfer.h):
LS/GET/PUT/RM/MKDIR/MV plus TIME, SLEEPSCREEN and the dev commands. Finds the
X4 on Windows (COMx), macOS and Linux.
"""
import base64
import datetime
import glob
import threading
import time
import zlib

import serial  # pyserial
from serial.tools import list_ports

CHUNK = 4096
ESPRESSIF_VID = 0x303A


def find_port():
    """The X4's serial port, or None. Its USB-JTAG/serial unit has Espressif's vendor ID."""
    for info in sorted(list_ports.comports(), key=lambda p: p.device):
        if info.vid == ESPRESSIF_VID:
            return info.device
    ports = sorted(glob.glob("/dev/cu.usbmodem*")) or sorted(glob.glob("/dev/ttyACM*"))
    return ports[0] if ports else None


class DeviceError(Exception):
    pass


class X4:
    """One serial connection to the X4, shared by all requests."""

    def __init__(self):
        self.lock = threading.Lock()
        self.port = None
        self.ser = None
        self.pending = b""

    # --- connection -------------------------------------------------------
    @staticmethod
    def find_port():
        return find_port()

    def _ensure(self):
        port = self.find_port()
        if not port:
            self._close()
            raise DeviceError("X4 未连接：请用 USB 线连接并按电源键唤醒 / X4 not connected: plug it in and wake it")
        if self.ser is None or port != self.port:
            self._close()
            self.ser = serial.Serial(port, 115200, timeout=0.2)
            self.port = port
            self.pending = b""

    def _close(self):
        if self.ser is not None:
            try:
                self.ser.close()
            except Exception:
                pass
        self.ser = None
        self.port = None

    def connected(self):
        return self.find_port() is not None

    # --- line protocol ----------------------------------------------------
    def _send(self, line):
        self.ser.reset_input_buffer()
        self.pending = b""
        self.ser.write(("CMD:" + line + "\n").encode("utf-8"))

    def _line(self, timeout):
        """Next complete line, or None on timeout."""
        deadline = time.time() + timeout
        while True:
            if b"\n" in self.pending:
                line, self.pending = self.pending.split(b"\n", 1)
                return line.rstrip(b"\r")
            if time.time() >= deadline:
                return None
            self.pending += self.ser.read(max(1, self.ser.in_waiting))

    def _expect(self, prefixes, timeout=10):
        """First line starting with one of prefixes; log lines are skipped."""
        deadline = time.time() + timeout
        while time.time() < deadline:
            line = self._line(deadline - time.time())
            if line is None:
                break
            text = line.decode("utf-8", "replace")
            if text.startswith(prefixes):
                return text
        raise DeviceError("X4 没有响应 / no reply from the X4")

    def _run(self, fn):
        with self.lock:
            try:
                self._ensure()
                return fn()
            except (serial.SerialException, OSError) as e:
                self._close()
                raise DeviceError(f"USB 连接中断 / USB link lost: {e}")

    # --- commands ---------------------------------------------------------
    def ls(self, path):
        def go():
            self._send("LS " + path)
            head = self._expect(("LS_BEGIN", "LS_ERR"))
            if head.startswith("LS_ERR"):
                raise DeviceError(head)
            entries = []
            while True:
                line = self._line(10)
                if line is None:
                    raise DeviceError("列表超时 / listing timed out")
                text = line.decode("utf-8", "replace")
                if text == "LS_END":
                    return entries
                if text.startswith("E\t"):
                    _, kind, size, name = text.split("\t", 3)
                    entries.append({"name": name, "size": int(size), "isDirectory": kind == "d",
                                    "isEpub": name.lower().endswith(".epub")})

        return self._run(go)

    def get(self, path):
        def once():
            self._send("GET " + path)
            head = self._expect(("GET_BEGIN", "GET_ERR"))
            if head.startswith("GET_ERR"):
                raise DeviceError(head)
            size = int(head.split()[1])
            parts = []
            while True:
                line = self._line(10)
                if line is None:
                    raise DeviceError("下载超时 / download timed out")
                if line.startswith(b"D "):
                    parts.append(base64.b64decode(line[2:]))
                elif line.startswith(b"GET_END"):
                    data = b"".join(parts)
                    crc = int(line.split()[1], 16)
                    if len(data) != size or (zlib.crc32(data) & 0xFFFFFFFF) != crc:
                        return None  # corrupted by an interleaved log line; retry
                    return data
                elif line.startswith(b"GET_ERR"):
                    raise DeviceError(line.decode("utf-8", "replace"))

        def go():
            for _ in range(3):
                data = once()
                if data is not None:
                    return data
            raise DeviceError("下载校验失败 / download failed its checksum")

        return self._run(go)

    def put(self, path, data, progress=None):
        """Writes data to path; progress(done, total) is called per chunk."""
        def go():
            self._send(f"PUT {len(data)} {path}")
            reply = self._expect(("PUT_READY", "PUT_ERR"))
            if reply != "PUT_READY":
                raise DeviceError(reply)
            for off in range(0, len(data), CHUNK):
                self.ser.write(data[off:off + CHUNK])
                ack = self._expect(("ACK", "PUT_ERR"))
                if not ack.startswith("ACK"):
                    raise DeviceError(ack)
                if progress:
                    progress(min(off + CHUNK, len(data)), len(data))
            result = self._expect(("PUT_OK", "PUT_ERR"), 30)
            if not result.startswith("PUT_OK"):
                raise DeviceError(result)
            if int(result.split()[1], 16) != (zlib.crc32(data) & 0xFFFFFFFF):
                raise DeviceError("上传校验失败 / upload failed its checksum")

        return self._run(go)

    def simple(self, line, verb):
        def go():
            self._send(line)
            reply = self._expect((verb + "_OK", verb + "_ERR"))
            if not reply.startswith(verb + "_OK"):
                raise DeviceError(reply)

        return self._run(go)

    def query(self, line, verb):
        """Sends a command and returns its <verb>_OK reply line."""
        def go():
            self._send(line)
            reply = self._expect((verb + "_OK", verb + "_ERR"))
            if not reply.startswith(verb + "_OK"):
                raise DeviceError(reply)
            return reply

        return self._run(go)

    def sync_time(self):
        """Sets the X4 clock (and, if unset, its time zone) from this computer. Returns the epoch sent."""
        def go():
            offset = datetime.datetime.now().astimezone().utcoffset()
            # Send right on a second boundary so the device clock lands within a few ms.
            time.sleep(1 - (time.time() % 1))
            epoch = int(round(time.time()))
            self._send(f"TIME {epoch} {int(offset.total_seconds() // 60)}")
            reply = self._expect(("TIME_OK", "TIME_ERR"))
            if not reply.startswith("TIME_OK"):
                raise DeviceError(reply)
            return epoch

        return self._run(go)

    def command(self, line, seconds=3.0):
        """Sends a dev command (CMD:<line>) and returns everything printed for `seconds`."""
        def go():
            self._send(line)
            buf = b""
            deadline = time.time() + seconds
            while time.time() < deadline:
                buf += self.ser.read(max(1, self.ser.in_waiting))
            return buf.decode("utf-8", "replace")

        return self._run(go)

    def screenshot(self):
        """Raw 1-bit framebuffer bytes (panel-native 800x480)."""
        def go():
            self._send("SCREENSHOT")
            buf = b""
            deadline = time.time() + 15
            while time.time() < deadline and b"SCREENSHOT_END" not in buf:
                buf += self.ser.read(8192)
            i = buf.find(b"SCREENSHOT_START:")
            if i < 0:
                raise DeviceError("no screenshot / 没有截图")
            j = buf.index(b"\n", i)
            size = int(buf[i + 17:j])
            return buf[j + 1:j + 1 + size]

        return self._run(go)
