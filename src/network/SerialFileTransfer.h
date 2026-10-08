// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The InkDesk Authors

#pragma once

#include <Arduino.h>

#include <string>

// SD card file access over the USB serial port, for a computer without Wi-Fi
// (the X4's ESP32-C3 has no USB-OTG, so it cannot be a USB drive). The hosts
// are the browser app (tools/inkdesk/web/x4serial.js, Web Serial) and
// tools/inkdesk/usb-helper/x4usb.py. Commands arrive as "CMD:<verb> <args>" lines; replies are
// lines starting with the verb, so interleaved log lines are easy to skip.
//
//   LS <dir>          LS_BEGIN, then "E\t<d|f>\t<size>\t<name>" per entry, LS_END
//   GET <path>        GET_BEGIN <size>, "D <base64>" lines, GET_END <crc32 hex>
//   PUT <size> <path> PUT_READY; the host sends 4 KB chunks, each answered by
//                     "ACK <total>"; PUT_OK <crc32 hex> once the file replaced <path>
//   RM <path>         RM_OK (files, or folders with their contents)
//   MKDIR <path>      MKDIR_OK
//   MV <from>\t<to>   MV_OK (rename or move)
//   HELLO             HELLO_OK <firmware version>
//   WEBURL <url>      WEBURL_OK; remembers where the browser app is hosted
// Any failure answers <VERB>_ERR <reason>. Credential stores are refused.
namespace serialfiles {

// Runs `cmd` (the text after "CMD:") when it is a file command. Returns false
// for other commands.
bool handle(const String& cmd);

// Call for every command line from the host, so screens can tell a browser
// (or other host tool) is talking to the device.
void noteHostActivity();
unsigned long msSinceHostActivity();

// Address of the browser app, as reported by the app itself; empty until then.
std::string webAppUrl();

}  // namespace serialfiles
