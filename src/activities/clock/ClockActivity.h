// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The InkDesk Authors

#pragma once

#include "activities/Activity.h"
#include "components/ClockFace.h"

// Full-screen desk clock with the pinned memo underneath. Between minute ticks
// the chip light-sleeps (on battery), so the clock runs for days; only the power
// button can wake it early, after which the other buttons work for a while.
//
// Boards without an RTC need the time set since power-on. When it is unknown
// the activity says so and goes online only when the user asks (Confirm): it
// then joins a saved Wi-Fi network, syncs, and reboots into itself on a clean
// heap (see silentRestartToClock()). A computer can also set it over USB.
class ClockActivity final : public Activity {
 public:
  explicit ClockActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("Clock", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool preventAutoSleep() override { return true; }
  // A desk clock is often plugged in just to charge.
  bool allowsUsbAutoOpen() const override { return false; }

 private:
  enum State { SHOWING, SYNCING, NOT_SET };

  // Buttons answer for this long after entering or a power-button wake.
  static constexpr unsigned long AWAKE_WINDOW_MS = 30000;
  // Fast refreshes leave ghosting; clean the panel up this often.
  static constexpr int MINUTES_PER_CLEAN_REFRESH = 30;

  State state = SHOWING;
  bool wifiStarted = false;
  bool confirmArmed = false;  // Confirm went down on this screen
  int shownMinute = -1;       // minute-of-year last drawn
  int fastRefreshes = 0;
  unsigned long awakeUntil = 0;

  ClockFace face;

  void startSync();
  void runSync();
  void sleepUntilNextMinute();
};
