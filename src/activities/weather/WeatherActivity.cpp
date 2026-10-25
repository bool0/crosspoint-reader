// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The InkDesk Authors

#include "WeatherActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "components/WeatherFace.h"
#include "fontIds.h"

void WeatherActivity::onEnter() {
  Activity::onEnter();
  hasData = weather::load(data);
  requestUpdate();
}

void WeatherActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    activityManager.goHome();
  }
}

void WeatherActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int screenW = renderer.getScreenWidth();
  const int screenH = renderer.getScreenHeight();
  int vt = 0, vr = 0, vb = 0, vl = 0;
  renderer.getOrientedViewableTRBL(&vt, &vr, &vb, &vl);
  const int left = vl + metrics.contentSidePadding;
  const int width = screenW - vl - vr - 2 * metrics.contentSidePadding;

  renderer.clearScreen();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, screenW, metrics.headerHeight}, tr(STR_WEATHER));
  int y = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing + 8;

  if (!hasData) {
    const int line10 = renderer.getLineHeight(UI_10_FONT_ID);
    for (const auto& line : renderer.wrappedText(UI_10_FONT_ID, tr(STR_WEATHER_NO_DATA), width, 8)) {
      renderer.drawText(UI_10_FONT_ID, left, y, line.c_str());
      y += line10;
    }
  } else {
    WeatherFace::draw(renderer, data, left, width, y, screenH - metrics.buttonHintsHeight - 8);
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
