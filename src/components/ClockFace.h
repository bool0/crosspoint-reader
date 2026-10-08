// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The InkDesk Authors

#pragma once

#include <ctime>
#include <string>
#include <vector>

#include "util/MemoMarkdown.h"

class GfxRenderer;

// The clock screen layout: date line with battery, large time, the pinned memo
// and a footer hint. Shared by ClockActivity and the clock lock screen, which
// repaints only the time band once a minute while the device sleeps.
class ClockFace {
 public:
  // Vertical span of the time digits; everything else on the face sits outside it.
  struct Band {
    int top = 0;
    int height = 0;
  };

  // Loads and wraps the pinned memo for the current screen size.
  void loadMemo(GfxRenderer& renderer);
  void clearMemo();
  bool hasMemo() const { return !memoTitle.empty(); }

  // Draws the whole face into the frame buffer (no refresh) and returns the time band.
  Band draw(const GfxRenderer& renderer, const tm& now, const char* footer) const;

  // Clears the time band and draws hour:minute into it.
  static void drawTime(const GfxRenderer& renderer, Band band, int hour, int minute);

  // Clears the battery slot on the date line and draws `percent` there.
  static void drawBattery(const GfxRenderer& renderer, int percent);

 private:
  int memoFontId = 0;
  std::string memoTitle;
  memomd::Doc memoDoc;

  void drawMemo(const GfxRenderer& renderer, int top, int bottom) const;
};
