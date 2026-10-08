// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The InkDesk Authors

#pragma once

#include <cstdint>
#include <ctime>

class GfxRenderer;

// Sleep screens that keep the clock and the pinned memo on the panel.
//
// Clock: drawn once on the way into sleep, then the device wakes on a timer each
// minute, repaints only the time digits (fast refresh against the saved frame)
// and sleeps again. The battery latch stays on for this, so the X4 keeps its
// system clock. Memo: drawn once; the device then powers off as usual and the
// e-ink panel keeps showing it.
namespace lockscreen {

// Draws the clock lock screen and records its layout for the minute ticks.
// Returns false (nothing drawn) when the time is unknown.
bool renderClock(GfxRenderer& renderer);

// Draws the pinned memo full-screen. Returns false (nothing drawn) without a memo.
bool renderMemo(GfxRenderer& renderer);

// Whether the last sleep entry drew a clock that still needs minute ticks.
bool armed();
void disarm();

// Frame of the full lock screen on SD, the base for every minute repaint.
bool saveFrame(const GfxRenderer& renderer);
bool loadFrame(GfxRenderer& renderer);

// Milliseconds until just past the next minute boundary.
uint32_t msToNextMinute();

// On a timer wake: true when only the digits need repainting (same day as the
// full draw); false when the whole face must be drawn again.
bool canTick(const tm& now);

// Repaints the digits for `now` over the loaded frame and refreshes the panel.
// The frame must hold the full face; the previous minute is redrawn first so the
// panel's differential baseline matches what it shows.
void tick(GfxRenderer& renderer, const tm& now, int batteryPercent);

}  // namespace lockscreen
