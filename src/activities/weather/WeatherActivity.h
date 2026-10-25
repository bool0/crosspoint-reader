// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The InkDesk Authors

#pragma once

#include "activities/Activity.h"
#include "util/Weather.h"

// The last weather reading saved on the SD card (see util/Weather.h). It is
// refreshed whenever the device joins Wi-Fi or the browser app connects over
// USB, so this screen only reads the file.
class WeatherActivity final : public Activity {
 public:
  explicit WeatherActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("Weather", renderer, mappedInput) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  weather::Data data;
  bool hasData = false;
};
