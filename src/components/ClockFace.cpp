// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The InkDesk Authors

#include "ClockFace.h"

#include <GfxRenderer.h>
#include <HalPowerManager.h>
#include <I18n.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>

#include "CrossPointSettings.h"
#include "fontIds.h"
#include "util/MemoStore.h"

namespace {

// Side padding inside the viewable area. Fixed rather than theme-driven so the
// lock screen's minute repaint, which runs without loading a theme, lands on
// exactly the pixels the full draw used.
constexpr int PAD = 20;
constexpr int STATUS_GAP = 12;

// Seven-segment bit masks (bit 0 = a ... bit 6 = g) for 0-9.
constexpr uint8_t SEGMENTS[10] = {0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F};

constexpr StrId WEEKDAYS[7] = {StrId::STR_WEEKDAY_SUN, StrId::STR_WEEKDAY_MON, StrId::STR_WEEKDAY_TUE,
                               StrId::STR_WEEKDAY_WED, StrId::STR_WEEKDAY_THU, StrId::STR_WEEKDAY_FRI,
                               StrId::STR_WEEKDAY_SAT};

// Digit cell proportions relative to its height.
constexpr float DIGIT_W = 0.52f;
constexpr float DIGIT_GAP = 0.14f;
constexpr float COLON_W = 0.34f;
constexpr float TIME_ROW_W = 4 * DIGIT_W + 2 * DIGIT_GAP + COLON_W;

struct Insets {
  int top = 0, right = 0, bottom = 0, left = 0;
};

Insets viewable(const GfxRenderer& renderer) {
  Insets in;
  renderer.getOrientedViewableTRBL(&in.top, &in.right, &in.bottom, &in.left);
  return in;
}

int statusY(const GfxRenderer& renderer) { return viewable(renderer).top + STATUS_GAP; }

// Light-on-dark when the dark clock face is selected.
bool ink() { return SETTINGS.clockFaceDark == 0; }
Color inkColor() { return ink() ? Color::Black : Color::White; }

// Draws one digit in the selected style. The continuous styles overlap their
// strokes at the joints so digits read as whole figures; Classic LCD keeps
// visible gaps between separate segments.
void drawDigit(const GfxRenderer& renderer, const int x, const int y, const int w, const int h, const int digit,
               const Color color = inkColor()) {
  const uint8_t style = SETTINGS.clockFaceStyle;
  const int divisor = style == CrossPointSettings::CLOCK_STYLE_THIN   ? 18
                      : style == CrossPointSettings::CLOCK_STYLE_BOLD ? 6
                      : style == CrossPointSettings::CLOCK_STYLE_LCD  ? 8
                                                                      : 10;
  const int t = std::max(3, h / divisor);
  const int r = t / 2;
  const uint8_t mask = SEGMENTS[digit];
  auto seg = [&](const int bit, const int sx, const int sy, const int sw, const int sh) {
    if (mask & (1 << bit)) renderer.fillRoundedRect(sx, sy, sw, sh, r, color);
  };

  if (style == CrossPointSettings::CLOCK_STYLE_LCD) {
    const int g = std::max(2, t / 4);
    const int half = h / 2;
    const int vLen = half - t / 2 - 2 * g;
    seg(0, x + t / 2 + g, y, w - t - 2 * g, t);
    seg(1, x + w - t, y + t / 2 + g, t, vLen);
    seg(2, x + w - t, y + half + g, t, vLen);
    seg(3, x + t / 2 + g, y + h - t, w - t - 2 * g, t);
    seg(4, x, y + half + g, t, vLen);
    seg(5, x, y + t / 2 + g, t, vLen);
    seg(6, x + t / 2 + g, y + half - t / 2, w - t - 2 * g, t);
    return;
  }

  if (digit == 1) {
    // A lone right-hand stroke looks misplaced; center the 1 in its cell.
    renderer.fillRoundedRect(x + (w - t) / 2, y, t, h, r, color);
    return;
  }
  const int mid = y + (h - t) / 2;
  const int upperH = mid - y + t;
  const int lowerH = y + h - mid;
  seg(0, x, y, w, t);
  seg(1, x + w - t, y, t, upperH);
  seg(2, x + w - t, mid, t, lowerH);
  seg(3, x, y + h - t, w, t);
  seg(4, x, mid, t, lowerH);
  seg(5, x, y, t, upperH);
  seg(6, x, mid, w, t);
}

}  // namespace

int ClockFace::numberWidth(const int height, const int value) {
  const int digits = value == 0 ? 1 : static_cast<int>(std::to_string(std::abs(value)).size());
  const int dw = static_cast<int>(height * DIGIT_W);
  const int gap = static_cast<int>(height * DIGIT_GAP);
  return (value < 0 ? dw + gap : 0) + digits * dw + (digits - 1) * gap;
}

void ClockFace::drawNumber(const GfxRenderer& renderer, int x, const int y, const int height, const int value,
                           const Color color) {
  const int dw = static_cast<int>(height * DIGIT_W);
  const int gap = static_cast<int>(height * DIGIT_GAP);
  if (value < 0) {
    const int t = std::max(3, height / 10);
    renderer.fillRoundedRect(x, y + (height - t) / 2, dw, t, t / 2, color);
    x += dw + gap;
  }
  for (const char c : std::to_string(std::abs(value))) {
    drawDigit(renderer, x, y, dw, height, c - '0', color);
    x += dw + gap;
  }
}

void ClockFace::loadMemo(GfxRenderer& renderer) {
  clearMemo();
  const std::string fileName = memostore::pinned();
  if (fileName.empty()) return;
  std::string text;
  if (!memostore::read(fileName, text)) return;

  memoTitle = memostore::titleOf(fileName);
  memoFontId = memostore::textFontId(renderer);
  const Insets in = viewable(renderer);
  const int width = renderer.getScreenWidth() - in.left - in.right - 2 * PAD;
  const int lineHeight = std::max(1, renderer.getLineHeight(memoFontId));
  // More than ever fits; drawMemo() clips to the space it gets.
  memoDoc = memomd::layout(renderer, memoFontId, text, width, 2 * renderer.getScreenHeight() / lineHeight + 1);
}

void ClockFace::clearMemo() {
  memoTitle.clear();
  memoDoc = memomd::Doc{};
}

void ClockFace::drawTime(const GfxRenderer& renderer, const Band band, int hour, const int minute) {
  const Insets in = viewable(renderer);
  const int screenW = renderer.getScreenWidth();
  renderer.fillRect(in.left, band.top, screenW - in.left - in.right, band.height, !ink());

  const int availW = screenW - in.left - in.right - 2 * PAD;
  const int h = std::min(band.height, static_cast<int>(availW / TIME_ROW_W));
  const int dw = static_cast<int>(h * DIGIT_W);
  const int gap = static_cast<int>(h * DIGIT_GAP);
  const int colonW = static_cast<int>(h * COLON_W);
  const int rowW = 4 * dw + 2 * gap + colonW;
  int x = in.left + (screenW - in.left - in.right - rowW) / 2;
  const int y = band.top + (band.height - h) / 2;

  const bool use12h = SETTINGS.clockFormat == 1;
  if (use12h) {
    hour %= 12;
    if (hour == 0) hour = 12;
  }
  if (!(use12h && hour < 10)) drawDigit(renderer, x, y, dw, h, hour / 10);
  x += dw + gap;
  drawDigit(renderer, x, y, dw, h, hour % 10);
  x += dw;

  const int dot = SETTINGS.clockFaceStyle == CrossPointSettings::CLOCK_STYLE_THIN   ? std::max(5, h / 14)
                  : SETTINGS.clockFaceStyle == CrossPointSettings::CLOCK_STYLE_BOLD ? std::max(8, h / 6)
                                                                                    : std::max(6, h / 9);
  const int dotX = x + (colonW - dot) / 2;
  renderer.fillRoundedRect(dotX, y + h * 3 / 10 - dot / 2, dot, dot, dot / 2, inkColor());
  renderer.fillRoundedRect(dotX, y + h * 7 / 10 - dot / 2, dot, dot, dot / 2, inkColor());
  x += colonW;

  drawDigit(renderer, x, y, dw, h, minute / 10);
  x += dw + gap;
  drawDigit(renderer, x, y, dw, h, minute % 10);
}

void ClockFace::drawBattery(const GfxRenderer& renderer, const int percent) {
  if (!SETTINGS.clockFaceBattery) return;
  const Insets in = viewable(renderer);
  const int right = renderer.getScreenWidth() - in.right - PAD;
  const int slotW = renderer.getTextWidth(UI_12_FONT_ID, "100%") + 4;
  const int y = statusY(renderer);
  renderer.fillRect(right - slotW, y, slotW, renderer.getLineHeight(UI_12_FONT_ID), !ink());
  char text[8];
  snprintf(text, sizeof(text), "%d%%", std::clamp(percent, 0, 100));
  renderer.drawText(UI_12_FONT_ID, right - renderer.getTextWidth(UI_12_FONT_ID, text), y, text, ink());
}

void ClockFace::drawMemo(const GfxRenderer& renderer, const int top, const int bottom) const {
  if (memoTitle.empty()) return;
  const Insets in = viewable(renderer);
  const int left = in.left + PAD;
  const int right = renderer.getScreenWidth() - in.right - PAD;

  renderer.fillRect(left, top, right - left, 2, ink());
  int y = top + 10;
  // A memo that opens with its own title as a heading needs no separate label.
  const auto& lines = memoDoc.lines;
  std::string firstHeading;
  if (!lines.empty() && lines.front().kind == memomd::Kind::Heading) {
    for (const auto& piece : lines.front().pieces) firstHeading += piece.text;
  }
  if (firstHeading != memoTitle) {
    const std::string title = renderer.truncatedText(UI_12_FONT_ID, memoTitle.c_str(), right - left);
    renderer.drawText(UI_12_FONT_ID, left, y, title.c_str(), ink(), EpdFontFamily::BOLD);
    y += renderer.getLineHeight(UI_12_FONT_ID) + 6;
  }

  for (const auto& line : memoDoc.lines) {
    if (y + line.height > bottom) break;
    memomd::drawLine(renderer, memoFontId, line, left, y, right - left, ink());
    y += line.height;
  }
}

ClockFace::Band ClockFace::draw(const GfxRenderer& renderer, const tm& now, const char* footer) const {
  const Insets in = viewable(renderer);
  const int screenW = renderer.getScreenWidth();
  const int screenH = renderer.getScreenHeight();
  renderer.clearScreen(ink() ? 0xFF : 0x00);

  // Date line: localized date and weekday on the left, battery on the right.
  // Its row stays reserved when both are hidden only if one of them shows.
  const bool statusRow = SETTINGS.clockFaceDate || SETTINGS.clockFaceBattery;
  const int y = statusY(renderer);
  if (SETTINGS.clockFaceDate) {
    char date[64];
    snprintf(date, sizeof(date), tr(STR_CLOCK_DATE_FORMAT), now.tm_year + 1900, now.tm_mon + 1, now.tm_mday);
    char line[128];
    const char* meridiem = SETTINGS.clockFormat == 1 ? (now.tm_hour < 12 ? tr(STR_CLOCK_AM) : tr(STR_CLOCK_PM)) : "";
    snprintf(line, sizeof(line), "%s  %s%s%s", date, I18N.get(WEEKDAYS[now.tm_wday % 7]), meridiem[0] ? "  " : "",
             meridiem);
    renderer.drawText(UI_12_FONT_ID, in.left + PAD, y, line, ink(), EpdFontFamily::BOLD);
  }
  drawBattery(renderer, powerManager.getBatteryPercentage());

  const int footerH = renderer.getLineHeight(SMALL_FONT_ID) + 12;
  const int contentTop = statusRow ? y + renderer.getLineHeight(UI_12_FONT_ID) + 8 : in.top + STATUS_GAP;
  const int contentBottom = screenH - in.bottom - footerH;
  const int contentH = contentBottom - contentTop;
  const bool landscape = screenW > screenH;
  const bool showMemo = SETTINGS.clockFaceMemo && !memoTitle.empty();
  Band band;
  band.top = contentTop;
  // Without a memo the time takes the whole band.
  band.height = !showMemo ? contentH : (landscape ? contentH * 11 / 20 : contentH * 7 / 20);
  drawTime(renderer, band, now.tm_hour, now.tm_min);
  if (showMemo) drawMemo(renderer, band.top + band.height + 8, contentBottom);

  if (footer && footer[0]) renderer.drawCenteredText(SMALL_FONT_ID, contentBottom + 4, footer, ink());
  return band;
}
