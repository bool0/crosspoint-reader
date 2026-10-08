#pragma once
#include <string>

#include "activities/Activity.h"

class Bitmap;
class HalFile;

class SleepActivity final : public Activity {
 public:
  // powerOff: the device is about to cut its power, so show the power-off screen
  // (SETTINGS.powerOffScreen) rather than the lock screen.
  explicit SleepActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, bool fromTimeout = false,
                         bool powerOff = false)
      : Activity("Sleep", renderer, mappedInput), fromTimeout(fromTimeout), powerOff(powerOff) {}
  void onEnter() override;

 private:
  void renderDefaultSleepScreen() const;
  void renderCustomSleepScreen() const;
  void renderCoverSleepScreen() const;
  void renderBitmapSleepScreen(const Bitmap& bitmap, bool preserveBackground = false) const;
  bool renderSleepOverlayFile(HalFile& file, const char* pathForLog) const;
  bool renderTransparentOverlayPng(const std::string& path) const;
  bool renderSleepOverlayPath(const std::string& path) const;
  void renderLastScreenSleepScreen() const;
  void renderTransparentCustomSleepScreen() const;
  void renderBlankSleepScreen() const;
  void renderPowerOffScreen() const;

  bool fromTimeout = false;
  bool powerOff = false;
};
