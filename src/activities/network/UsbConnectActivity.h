// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The InkDesk Authors

#pragma once

#include <string>

#include "activities/Activity.h"

// Shown when USB is plugged in (and from Home): how to open the browser app
// that manages files and memos over the cable (Web Serial in Chrome / Edge),
// with a QR code of its address and whether a browser is connected.
class UsbConnectActivity final : public Activity {
 public:
  explicit UsbConnectActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("UsbConnect", renderer, mappedInput) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool preventAutoSleep() override { return true; }
  bool allowsUsbAutoOpen() const override { return false; }

 private:
  // The browser app pings about every 3 s while connected.
  static constexpr unsigned long CONNECTED_WINDOW_MS = 8000;

  std::string url;
  bool hostConnected = false;
  bool usbPresent = true;
};
