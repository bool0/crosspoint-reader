// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The InkDesk Authors

#include "LockScreen.h"

#include <GfxRenderer.h>
#include <HalClock.h>
#include <HalDisplay.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <esp_attr.h>
#include <sys/time.h>

#include <algorithm>

#include "components/ClockFace.h"
#include "components/WeatherFace.h"
#include "fontIds.h"
#include "util/MemoMarkdown.h"
#include "util/MemoStore.h"

namespace lockscreen {

namespace {

constexpr char FRAME_FILE[] = "/.crosspoint/lock_frame.bin";
constexpr uint32_t ARMED_MAGIC = 0x10CC10CCu;
// Fast refreshes leave ghosting; clean the panel up this often.
constexpr uint8_t TICKS_PER_CLEAN_REFRESH = 30;

// Lives in RTC memory: survives the deep sleep between ticks, not a power cut.
struct State {
  uint32_t magic;
  int16_t bandTop;
  int16_t bandHeight;
  int16_t yday;
  uint8_t hour;
  uint8_t minute;
  uint8_t battery;
  uint8_t fastRefreshes;
  uint8_t orientation;
};
RTC_NOINIT_ATTR State state;

}  // namespace

bool renderClock(GfxRenderer& renderer) {
  struct tm now;
  if (!halClock.localTime(now)) return false;

  ClockFace face;
  face.loadMemo(renderer);
  renderer.clearScreen();
  const ClockFace::Band band = face.draw(renderer, now, tr(STR_LOCKED_HINT));
  renderer.displayBuffer(HalDisplay::HALF_REFRESH);

  state.bandTop = static_cast<int16_t>(band.top);
  state.bandHeight = static_cast<int16_t>(band.height);
  state.yday = static_cast<int16_t>(now.tm_yday);
  state.hour = static_cast<uint8_t>(now.tm_hour);
  state.minute = static_cast<uint8_t>(now.tm_min);
  state.battery = 255;  // the full draw already shows the live value
  state.fastRefreshes = 0;
  state.orientation = static_cast<uint8_t>(renderer.getOrientation());
  state.magic = ARMED_MAGIC;
  return true;
}

bool renderWeather(GfxRenderer& renderer) {
  weather::Data data;
  if (!weather::load(data)) return false;
  int top = 0, right = 0, bottom = 0, left = 0;
  renderer.getOrientedViewableTRBL(&top, &right, &bottom, &left);
  constexpr int PAD = 20;
  const int footerY = renderer.getScreenHeight() - bottom - renderer.getLineHeight(SMALL_FONT_ID) - 8;
  renderer.clearScreen();
  WeatherFace::draw(renderer, data, left + PAD, renderer.getScreenWidth() - left - right - 2 * PAD, top + 16,
                    footerY - 8);
  renderer.drawCenteredText(SMALL_FONT_ID, footerY, tr(STR_MEMO_POWERED_OFF_HINT));
  renderer.displayBuffer(HalDisplay::HALF_REFRESH);
  return true;
}

bool renderMemo(GfxRenderer& renderer) {
  const std::string fileName = memostore::pinned();
  if (fileName.empty()) return false;
  std::string text;
  if (!memostore::read(fileName, text)) return false;

  int top = 0, right = 0, bottom = 0, left = 0;
  renderer.getOrientedViewableTRBL(&top, &right, &bottom, &left);
  constexpr int PAD = 20;
  const int x = left + PAD;
  const int width = renderer.getScreenWidth() - left - right - 2 * PAD;
  const int fontId = memostore::textFontId(renderer);
  const int lineHeight = std::max(1, renderer.getLineHeight(fontId));
  const int footerY = renderer.getScreenHeight() - bottom - renderer.getLineHeight(SMALL_FONT_ID) - 8;

  renderer.clearScreen();
  int y = top + 16;
  const std::string title = renderer.truncatedText(UI_12_FONT_ID, memostore::titleOf(fileName).c_str(), width);
  renderer.drawText(UI_12_FONT_ID, x, y, title.c_str(), true, EpdFontFamily::BOLD);
  y += renderer.getLineHeight(UI_12_FONT_ID) + 6;
  renderer.fillRect(x, y, width, 2);
  y += 12;

  const auto doc = memomd::layout(renderer, fontId, text, width, 2 * (footerY - y) / lineHeight + 1);
  for (const auto& line : doc.lines) {
    if (y + line.height > footerY - 8) break;
    memomd::drawLine(renderer, fontId, line, x, y, width);
    y += line.height;
  }
  renderer.drawCenteredText(SMALL_FONT_ID, footerY, tr(STR_MEMO_POWERED_OFF_HINT));
  renderer.displayBuffer(HalDisplay::HALF_REFRESH);
  return true;
}

bool armed() { return state.magic == ARMED_MAGIC; }

void disarm() { state.magic = 0; }

bool saveFrame(const GfxRenderer& renderer) {
  HalFile file;
  if (!Storage.openFileForWrite("LOCK", FRAME_FILE, file)) return false;
  const size_t size = renderer.getBufferSize();
  return file.write(renderer.getFrameBuffer(), size) == size;
}

bool loadFrame(GfxRenderer& renderer) {
  HalFile file;
  if (!Storage.openFileForRead("LOCK", FRAME_FILE, file)) return false;
  const size_t size = renderer.getBufferSize();
  return file.read(renderer.getFrameBuffer(), size) == static_cast<int>(size);
}

uint32_t msToNextMinute() {
  struct timeval tv;
  gettimeofday(&tv, nullptr);
  const uint32_t msIntoMinute = static_cast<uint32_t>((tv.tv_sec % 60) * 1000 + tv.tv_usec / 1000);
  return 60000 - msIntoMinute + 200;
}

bool canTick(const tm& now) { return armed() && now.tm_yday == state.yday; }

void tick(GfxRenderer& renderer, const tm& now, const int batteryPercent) {
  renderer.setOrientation(static_cast<GfxRenderer::Orientation>(state.orientation));
  const ClockFace::Band band{state.bandTop, state.bandHeight};

  // Rebuild exactly what the panel shows, then hand it over as the baseline.
  ClockFace::drawTime(renderer, band, state.hour, state.minute);
  if (state.battery != 255) ClockFace::drawBattery(renderer, state.battery);
  renderer.cleanupGrayscaleWithFrameBuffer();

  ClockFace::drawTime(renderer, band, now.tm_hour, now.tm_min);
  // Battery moves slowly; repaint it only with the periodic clean refresh.
  const bool clean = state.fastRefreshes + 1 >= TICKS_PER_CLEAN_REFRESH;
  if (clean) {
    ClockFace::drawBattery(renderer, batteryPercent);
    state.battery = static_cast<uint8_t>(std::clamp(batteryPercent, 0, 100));
  }
  if (clean) {
    renderer.displayBuffer(HalDisplay::HALF_REFRESH);
  } else {
    // displayBuffer() would promote this first paint after wake to a HALF clean;
    // the baseline restored above makes a fast diff valid.
    display.displayWindow(0, 0, display.getDisplayWidth(), display.getDisplayHeight());
  }

  state.hour = static_cast<uint8_t>(now.tm_hour);
  state.minute = static_cast<uint8_t>(now.tm_min);
  state.fastRefreshes = clean ? 0 : state.fastRefreshes + 1;
}

}  // namespace lockscreen
