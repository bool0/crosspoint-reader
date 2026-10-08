#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 The InkDesk Authors
"""X4 USB 助手 — manage the X4's SD card from this computer over the USB cable.

Runs a small web server on http://127.0.0.1:8484 that speaks the same HTTP API
as the device's Wi-Fi file server (/api/files, /download, /upload, /delete,
/mkdir, /rename, /move), but forwards every request to the X4 over USB serial
(the CMD:LS/GET/PUT/RM/MKDIR/MV commands in the CrossPoint firmware). The memo
editor page from the firmware works unchanged on top of it.

    python3 x4usb.py            # opens the browser
    python3 x4usb.py --no-open
"""
import argparse
import base64
import datetime
import email.parser
import email.policy
import glob
import json
import os
import posixpath
import threading
import time
import urllib.parse
import webbrowser
import zlib
from http import HTTPStatus
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

import serial  # pyserial

HERE = os.path.dirname(os.path.abspath(__file__))
# The memo and images pages are the firmware's own (src/network/html).
FIRMWARE_HTML = os.path.abspath(os.path.join(HERE, "..", "..", "..", "src", "network", "html"))
CHUNK = 4096


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
        ports = sorted(glob.glob("/dev/cu.usbmodem*")) or sorted(glob.glob("/dev/ttyACM*"))
        return ports[0] if ports else None

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

    def put(self, path, data):
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
        def go():
            offset = datetime.datetime.now().astimezone().utcoffset()
            time.sleep(1 - (time.time() % 1))
            self._send(f"TIME {int(round(time.time()))} {int(offset.total_seconds() // 60)}")
            self._expect(("TIME_OK", "TIME_ERR"))

        return self._run(go)


DEVICE = X4()
FAILED = object()  # device_call result after it has already sent the error response


def join(directory, name):
    return posixpath.join(directory if directory.startswith("/") else "/" + directory, name)


def parse_multipart(content_type, body):
    """Yields (filename, bytes) for each file part of a multipart/form-data body."""
    message = email.parser.BytesParser(policy=email.policy.HTTP).parsebytes(
        b"Content-Type: " + content_type.encode() + b"\r\n\r\n" + body)
    for part in message.iter_parts():
        filename = part.get_filename()
        if filename:
            yield os.path.basename(filename), part.get_payload(decode=True) or b""


class Handler(BaseHTTPRequestHandler):
    server_version = "X4USB/1.0"

    def log_message(self, fmt, *args):  # quieter console
        pass

    # --- helpers ----------------------------------------------------------
    def send_body(self, status, body, content_type="text/plain; charset=utf-8", extra=None):
        data = body if isinstance(body, bytes) else body.encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(data)))
        self.send_header("Cache-Control", "no-store")
        for k, v in (extra or {}).items():
            self.send_header(k, v)
        self.end_headers()
        self.wfile.write(data)

    def send_json(self, obj, status=HTTPStatus.OK):
        self.send_body(status, json.dumps(obj, ensure_ascii=False), "application/json; charset=utf-8")

    def send_page(self, name):
        path = os.path.join(HERE, name)
        if not os.path.exists(path):
            path = os.path.join(FIRMWARE_HTML, name)
        with open(path, "rb") as f:
            self.send_body(HTTPStatus.OK, f.read(), "text/html; charset=utf-8")

    def query(self):
        return urllib.parse.parse_qs(urllib.parse.urlparse(self.path).query)

    def form(self):
        length = int(self.headers.get("Content-Length") or 0)
        body = self.rfile.read(length)
        ctype = self.headers.get("Content-Type", "")
        if ctype.startswith("multipart/form-data"):
            return body, ctype
        return urllib.parse.parse_qs(body.decode("utf-8")), ctype

    def device_call(self, fn):
        """fn()'s result, or FAILED once a 503 with the reason has been sent."""
        try:
            return fn()
        except DeviceError as e:
            self.send_body(HTTPStatus.SERVICE_UNAVAILABLE, str(e))
        return FAILED

    # --- routes -------------------------------------------------------------
    def do_GET(self):
        route = urllib.parse.urlparse(self.path).path
        q = self.query()
        if route in ("/", "/files"):
            return self.send_page("files.html")
        if route == "/memo":
            return self.send_page("MemoPage.html")
        if route == "/images":
            return self.send_page("ImagesPage.html")
        if route == "/api/settings":
            # Only the lock-screen mode is exposed over USB.
            value = self.device_call(lambda: DEVICE.query("SLEEPSCREEN", "SLEEPSCREEN"))
            if value is not FAILED:
                self.send_json([{"key": "sleepScreen", "type": "enum", "value": int(value.split()[1])}])
            return
        if route in ("/settings", "/fonts"):
            return self.send_body(HTTPStatus.OK, "<meta charset=utf-8><p>该页面仅在设备的 Wi-Fi 文件传输中可用。"
                                  "<br>Only available in the device's Wi-Fi File Transfer.</p><a href='/'>← 返回</a>",
                                  "text/html; charset=utf-8")
        if route == "/api/status":
            return self.send_json({"version": "USB", "ip": "USB", "serial": DEVICE.port or "", "freeHeap": 0})
        if route == "/api/x4/status":
            return self.send_json({"connected": DEVICE.connected(), "port": DEVICE.find_port()})
        if route == "/api/files":
            path = q.get("path", ["/"])[0]
            entries = self.device_call(lambda: DEVICE.ls(path))
            if entries is not FAILED:
                self.send_json(entries)
            return
        if route == "/download":
            path = q.get("path", [""])[0]
            data = self.device_call(lambda: DEVICE.get(path))
            if data is not FAILED:
                name = urllib.parse.quote(posixpath.basename(path))
                self.send_body(HTTPStatus.OK, data, "application/octet-stream",
                               {"Content-Disposition": f"attachment; filename*=UTF-8''{name}"})
            return
        self.send_body(HTTPStatus.NOT_FOUND, "not found")

    def do_POST(self):
        route = urllib.parse.urlparse(self.path).path
        q = self.query()
        if route == "/upload":
            body, ctype = self.form()
            directory = q.get("path", ["/"])[0]

            def go():
                names = []
                for filename, data in parse_multipart(ctype, body):
                    DEVICE.put(join(directory, filename), data)
                    names.append(filename)
                if not names:
                    raise DeviceError("没有文件 / no file in the request")
                return names

            names = self.device_call(go)
            if names is not FAILED:
                self.send_body(HTTPStatus.OK, "File uploaded successfully: " + ", ".join(names))
            return

        if route == "/api/settings":
            length = int(self.headers.get("Content-Length") or 0)
            body = json.loads(self.rfile.read(length) or b"{}")
            mode = body.get("sleepScreen")
            if isinstance(mode, int) and self.device_call(lambda: DEVICE.query(f"SLEEPSCREEN {mode}", "SLEEPSCREEN")) is not FAILED:
                self.send_body(HTTPStatus.OK, "Settings saved")
            elif not isinstance(mode, int):
                self.send_body(HTTPStatus.OK, "Settings saved")
            return

        form, _ = self.form()
        field = lambda k, d="": (form.get(k) or [d])[0]  # noqa: E731
        if route == "/delete":
            paths = json.loads(field("paths")) if field("paths") else [field("path")]
            if self.device_call(lambda: [DEVICE.simple("RM " + p, "RM") for p in paths]) is not FAILED:
                self.send_body(HTTPStatus.OK, "Deleted")
            return
        if route == "/mkdir":
            path = join(field("path", "/"), field("name"))
            if self.device_call(lambda: DEVICE.simple("MKDIR " + path, "MKDIR")) is not FAILED:
                self.send_body(HTTPStatus.OK, "Folder created")
            return
        if route == "/rename":
            src = field("path")
            dst = join(posixpath.dirname(src), field("name"))
            if self.device_call(lambda: DEVICE.simple(f"MV {src}\t{dst}", "MV")) is not FAILED:
                self.send_body(HTTPStatus.OK, "Renamed")
            return
        if route == "/move":
            src = field("path")
            dst = join(field("dest", "/"), posixpath.basename(src))
            if self.device_call(lambda: DEVICE.simple(f"MV {src}\t{dst}", "MV")) is not FAILED:
                self.send_body(HTTPStatus.OK, "Moved")
            return
        if route == "/api/x4/sync-time":
            if self.device_call(DEVICE.sync_time) is not FAILED:
                self.send_json({"ok": True})
            return
        self.send_body(HTTPStatus.NOT_FOUND, "not found")


def main():
    parser = argparse.ArgumentParser(description="X4 USB 助手")
    parser.add_argument("--port", type=int, default=8484)
    parser.add_argument("--no-open", action="store_true")
    args = parser.parse_args()
    server = ThreadingHTTPServer(("127.0.0.1", args.port), Handler)
    url = f"http://127.0.0.1:{args.port}/"
    print(f"X4 USB 助手已启动：{url}  (Ctrl+C 退出)")
    if not args.no_open:
        threading.Timer(0.5, lambda: webbrowser.open(url)).start()
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
