// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The InkDesk Authors

#include "UsbConnectActivity.h"

#include <GfxRenderer.h>
#include <HalGPIO.h>
#include <I18n.h>

#include <algorithm>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "network/SerialFileTransfer.h"
#include "util/QrUtils.h"

void UsbConnectActivity::onEnter() {
  Activity::onEnter();
  url = serialfiles::webAppUrl();
  hostConnected = serialfiles::msSinceHostActivity() < CONNECTED_WINDOW_MS;
  usbPresent = gpio.isUsbConnected();
  requestUpdate();
}

void UsbConnectActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    activityManager.goHome();
    return;
  }
  const bool connected = serialfiles::msSinceHostActivity() < CONNECTED_WINDOW_MS;
  const bool present = gpio.isUsbConnected();
  if (connected != hostConnected || present != usbPresent) {
    hostConnected = connected;
    usbPresent = present;
    // The browser saves its address on connect; pick it up for the QR code.
    if (connected) url = serialfiles::webAppUrl();
    requestUpdate();
  }
}

void UsbConnectActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int screenW = renderer.getScreenWidth();
  const int screenH = renderer.getScreenHeight();
  int vt = 0, vr = 0, vb = 0, vl = 0;
  renderer.getOrientedViewableTRBL(&vt, &vr, &vb, &vl);
  const int left = vl + metrics.contentSidePadding;
  const int width = screenW - vl - vr - 2 * metrics.contentSidePadding;

  renderer.clearScreen();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, screenW, metrics.headerHeight}, tr(STR_USB_CONNECT));
  int y = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing + 8;

  // Status: the one thing to check at a glance.
  const char* status = !usbPresent ? tr(STR_USB_UNPLUGGED) : hostConnected ? tr(STR_USB_BROWSER_CONNECTED)
                                                                           : tr(STR_USB_WAITING);
  renderer.drawCenteredText(UI_12_FONT_ID, y, status, true, EpdFontFamily::BOLD);
  y += renderer.getLineHeight(UI_12_FONT_ID) + 16;

  if (!url.empty()) {
    const int qr = std::min(width, screenH / 3);
    QrUtils::drawQrCode(renderer, Rect{(screenW - qr) / 2, y, qr, qr}, url);
    y += qr + 12;
    for (const auto& line : renderer.wrappedText(UI_10_FONT_ID, url.c_str(), width, 3)) {
      renderer.drawCenteredText(UI_10_FONT_ID, y, line.c_str());
      y += renderer.getLineHeight(UI_10_FONT_ID);
    }
  } else {
    // Before the app has been opened from a web address, there is nothing to encode.
    for (const auto& line : renderer.wrappedText(UI_10_FONT_ID, tr(STR_USB_NO_URL), width, 4)) {
      renderer.drawCenteredText(UI_10_FONT_ID, y, line.c_str());
      y += renderer.getLineHeight(UI_10_FONT_ID);
    }
  }
  y += 16;

  const StrId steps[] = {StrId::STR_USB_STEP_1, StrId::STR_USB_STEP_2, StrId::STR_USB_STEP_3};
  const int lh = renderer.getLineHeight(UI_10_FONT_ID);
  const int bottom = screenH - metrics.buttonHintsHeight - 8;
  for (const StrId step : steps) {
    for (const auto& line : renderer.wrappedText(UI_10_FONT_ID, I18N.get(step), width, 3)) {
      if (y + lh > bottom) break;
      renderer.drawText(UI_10_FONT_ID, left, y, line.c_str());
      y += lh;
    }
    y += 6;
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
