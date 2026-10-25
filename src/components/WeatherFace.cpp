// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The InkDesk Authors

#include "WeatherFace.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <utility>

#include "CrossPointSettings.h"
#include "components/ClockFace.h"
#include "fontIds.h"

namespace {
constexpr int ICON_SIZE = 96;
constexpr int TEMP_HEIGHT = 72;
constexpr int DEGREE_SIZE = 16;
constexpr int ROW_COUNT = 7;
constexpr StrId WEEKDAYS[7] = {StrId::STR_WEEKDAY_SUN, StrId::STR_WEEKDAY_MON, StrId::STR_WEEKDAY_TUE,
                               StrId::STR_WEEKDAY_WED, StrId::STR_WEEKDAY_THU, StrId::STR_WEEKDAY_FRI,
                               StrId::STR_WEEKDAY_SAT};

// The large built-in font has only Latin glyphs. A city name in another script
// uses the reading font, which always carries the SD font's CJK fallback, unless
// that is set smaller than the UI font.
int cityFont(const GfxRenderer& renderer, const std::string& city) {
  const bool latin = std::all_of(city.begin(), city.end(), [](const char c) { return (c & 0x80) == 0; });
  if (latin) return NOTOSANS_18_FONT_ID;
  const int reader = SETTINGS.getReaderFontId();
  return renderer.getLineHeight(reader) > renderer.getLineHeight(UI_12_FONT_ID) ? reader : UI_12_FONT_ID;
}

// "2026-10-25" -> "2026年10月25日 星期六" in the clock's date style; "" if unparsable.
void formatDate(const std::string& iso, char* out, const size_t size) {
  int y = 0, m = 0, d = 0;
  out[0] = 0;
  if (sscanf(iso.c_str(), "%d-%d-%d", &y, &m, &d) != 3 || m < 1 || m > 12 || d < 1 || d > 31) return;
  // Sakamoto's day-of-week (0 = Sunday).
  static constexpr int T[12] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
  const int yy = m < 3 ? y - 1 : y;
  const int wday = (yy + yy / 4 - yy / 100 + yy / 400 + T[m - 1] + d) % 7;
  char date[32];
  snprintf(date, sizeof(date), tr(STR_CLOCK_DATE_FORMAT), y, m, d);
  snprintf(out, size, "%s  %s", date, I18N.get(WEEKDAYS[wday]));
}

int rounded(const float v) { return static_cast<int>(std::lround(v)); }

// Icon, large temperature and degree ring centred in [x, x + width); returns the bottom.
int drawHero(const GfxRenderer& renderer, const weather::Data& d, const int x, const int width, const int top) {
  const uint8_t* icon = weather::conditionIcon(d.code, d.day);
  const int temp = rounded(d.temp);
  const int numW = ClockFace::numberWidth(TEMP_HEIGHT, temp);
  const int iconW = icon ? ICON_SIZE + 20 : 0;
  int cx = x + (width - (iconW + numW + 6 + DEGREE_SIZE)) / 2;
  if (icon) {
    renderer.drawIcon(icon, cx, top, ICON_SIZE);
    cx += iconW;
  }
  const int numTop = top + (ICON_SIZE - TEMP_HEIGHT) / 2;
  ClockFace::drawNumber(renderer, cx, numTop, TEMP_HEIGHT, temp);
  cx += numW + 6;
  renderer.fillRoundedRect(cx, numTop, DEGREE_SIZE, DEGREE_SIZE, DEGREE_SIZE / 2, Color::Black);
  renderer.fillRoundedRect(cx + 4, numTop + 4, DEGREE_SIZE - 8, DEGREE_SIZE - 8, (DEGREE_SIZE - 8) / 2, Color::White);
  return top + ICON_SIZE;
}

void drawCentered(const GfxRenderer& renderer, const int fontId, const int x, const int width, const int y,
                  const char* text, const EpdFontFamily::Style style = EpdFontFamily::REGULAR) {
  const int w = renderer.getTextWidth(fontId, text, style);
  renderer.drawText(fontId, x + std::max(0, (width - w) / 2), y, text, true, style);
}
int drawRows(const GfxRenderer& renderer, const weather::Data& data, const int left, const int width, const int top,
             const int bottom) {
  const int lineH = renderer.getLineHeight(UI_12_FONT_ID);
  const int rowH = std::max(lineH + 2, std::min(lineH + 16, (bottom - top) / ROW_COUNT));

  char feels[16], range[32], humidity[16], wind[64], uv[48];
  snprintf(feels, sizeof(feels), "%d°", rounded(data.feels));
  snprintf(range, sizeof(range), "%d° / %d°", rounded(data.tmax), rounded(data.tmin));
  snprintf(humidity, sizeof(humidity), "%d%%", data.humidity);
  snprintf(wind, sizeof(wind), tr(STR_WEATHER_WIND_VALUE), I18N.get(weather::windDirection(data.windDir)),
           weather::beaufort(data.windKmh), rounded(data.windKmh));
  snprintf(uv, sizeof(uv), "%d · %s", rounded(data.uvMax), I18N.get(weather::uvLevel(data.uvMax)));
  const std::pair<const char*, const char*> rows[ROW_COUNT] = {
      {tr(STR_WEATHER_FEELS), feels},         {tr(STR_WEATHER_HIGH_LOW), range},
      {tr(STR_WEATHER_HUMIDITY), humidity},   {tr(STR_WEATHER_WIND), wind},
      {tr(STR_WEATHER_UV), uv},               {tr(STR_WEATHER_SUNRISE), data.sunrise.c_str()},
      {tr(STR_WEATHER_SUNSET), data.sunset.c_str()},
  };

  int y = top;
  for (const auto& [label, value] : rows) {
    const int textY = y + (rowH - lineH) / 2;
    renderer.drawText(UI_12_FONT_ID, left, textY, label);
    const int labelW = renderer.getTextWidth(UI_12_FONT_ID, label) + 16;
    const std::string shown = renderer.truncatedText(UI_12_FONT_ID, value, width - labelW);
    renderer.drawText(UI_12_FONT_ID, left + width - renderer.getTextWidth(UI_12_FONT_ID, shown.c_str()), textY,
                      shown.c_str());
    y += rowH;
    renderer.drawLine(left, y - 1, left + width, y - 1);
  }
  return y;
}
}  // namespace

void WeatherFace::draw(const GfxRenderer& renderer, const weather::Data& data, const int left, const int width,
                       const int top, const int bottom) {
  // Footer: when the reading was taken, then Open-Meteo's CC BY 4.0 credit.
  const int smallH = renderer.getLineHeight(SMALL_FONT_ID);
  const int creditY = bottom - smallH;
  const int updatedY = creditY - smallH;
  // Portrait stacks the summary over the rows; landscape puts them side by side.
  const bool wide = renderer.getScreenWidth() > renderer.getScreenHeight();
  const int heroW = wide ? width * 9 / 20 : width;
  char updated[48] = "";
  if (data.updated > 0) {
    tm local{};
    localtime_r(&data.updated, &local);
    char when[24];
    strftime(when, sizeof(when), "%m-%d %H:%M", &local);
    snprintf(updated, sizeof(updated), tr(STR_WEATHER_UPDATED), when);
  }

  char date[64];
  formatDate(data.date, date, sizeof(date));

  int y = top;
  const std::string name = data.city.empty() ? "-" : data.city;
  const int cityId = cityFont(renderer, name);
  const std::string city = renderer.truncatedText(cityId, name.c_str(), heroW, EpdFontFamily::BOLD);
  drawCentered(renderer, cityId, left, heroW, y, city.c_str(), EpdFontFamily::BOLD);
  y += renderer.getLineHeight(cityId) + 2;
  if (date[0]) {
    drawCentered(renderer, UI_10_FONT_ID, left, heroW, y, date);
    y += renderer.getLineHeight(UI_10_FONT_ID);
  }
  y += 14;
  y = drawHero(renderer, data, left, heroW, y) + 12;
  drawCentered(renderer, UI_12_FONT_ID, left, heroW, y, I18N.get(weather::conditionText(data.code)),
               EpdFontFamily::BOLD);
  y += renderer.getLineHeight(UI_12_FONT_ID) + 20;

  if (wide) {
    const int rowsLeft = left + heroW + 24;
    drawRows(renderer, data, rowsLeft, left + width - rowsLeft, top, updatedY - 6);
  } else {
    drawRows(renderer, data, left, width, y, updatedY - 6);
  }
  drawCentered(renderer, SMALL_FONT_ID, left, width, updatedY, updated);
  drawCentered(renderer, SMALL_FONT_ID, left, width, creditY, tr(STR_WEATHER_CREDIT));
}
