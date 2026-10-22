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
import email.parser
import email.policy
import json
import os
import posixpath
import sys
import threading
import urllib.parse
import webbrowser
from http import HTTPStatus
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))
from x4link import X4, DeviceError  # noqa: E402

# The memo and images pages are the firmware's own (src/network/html).
FIRMWARE_HTML = os.path.abspath(os.path.join(HERE, "..", "..", "..", "src", "network", "html"))


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
