// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The InkDesk Authors

#pragma once

// Short power history on the SD card (/.crosspoint/power.log): one line per
// boot and per Wi-Fi start with the reset reason and battery voltage, so a
// device that dies on battery (e.g. a brownout when Wi-Fi starts) can be
// diagnosed afterwards. Read it from the Files page or over USB.
namespace powerlog {

void note(const char* event);

// Restarts the chip once POWER has been held for 10 s, even when loop() is
// stuck. A responsive loop powers off at 3 s, before this fires.
void startForceRestartWatch();

}  // namespace powerlog
