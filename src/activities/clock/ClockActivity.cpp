// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The InkDesk Authors

#include "ClockActivity.h"

#include <GfxRenderer.h>
#include <HalClock.h>
#include <HalGPIO.h>
#include <HalPowerManager.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>
#include <WiFi.h>

#include <algorithm>
#include <cstdio>
#include <ctime>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "SilentRestart.h"
#include "activities/network/WifiSelectionActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/MemoStore.h"

namespace {

int minuteOfYear(const struct tm& t) { return t.tm_yday * 24 * 60 + t.tm_hour * 60 + t.tm_min; }

}  // namespace

void ClockActivity::onEnter() {
  Activity::onEnter();
  awakeUntil = millis() + AWAKE_WINDOW_MS;
  shownMinute = -1;
  fastRefreshes = 0;
  face.loadMemo(renderer);

  struct tm now;
  if (halClock.localTime(now)) {
    state = SHOWING;  // loop() paints the first minute
    return;
  }
  // Going online is the user's call (Confirm); meanwhile a computer may send
  // the time over USB (sync_time.py).
  state = NOT_SET;
  requestUpdate();
}

void ClockActivity::onExit() {
  Activity::onExit();
  face.clearMemo();
  if (wifiStarted && WiFi.getMode() != WIFI_MODE_NULL) {
    WiFi.disconnect(false);
    delay(30);
    silentRestart();
  }
}

void ClockActivity::startSync() {
  state = SYNCING;
  wifiStarted = true;
  requestUpdate();
  startActivityForResult(std::make_unique<WifiSelectionActivity>(renderer, mappedInput),
                         [this](const ActivityResult& result) {
                           if (result.isCancelled) {
                             LOG_INF("CLOCK", "Wi-Fi not joined, time unknown");
                             state = NOT_SET;
                           }
                         });
}

void ClockActivity::runSync() {
  const bool ok = WiFi.status() == WL_CONNECTED && halClock.syncFromNTP();
  if (ok) {
    SETTINGS.clockHasBeenSynced = 1;
    SETTINGS.saveToFile();
    // Leave the Wi-Fi heap behind: reboot straight back into the clock. The
    // system clock and its synced flag survive a soft restart.
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    wifiStarted = false;
    silentRestartToClock();
    return;
  }
  LOG_ERR("CLOCK", "NTP sync failed");
  state = NOT_SET;
  requestUpdate();
}

void ClockActivity::loop() {
  if (state == SYNCING) {
    // Back here from Wi-Fi selection with a connection: paint, then block on NTP.
    requestUpdateAndWait();
    runSync();
    return;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    activityManager.goHome();
    return;
  }
  // Act on release, and only for a press made on this screen: lists act on
  // release too, so reacting to the press would hand the release to the next
  // screen (the memo list would open its first row).
  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) confirmArmed = true;
  if (confirmArmed && mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    confirmArmed = false;
    if (state == NOT_SET) {
      startSync();
    } else {
      activityManager.goToMemos();
    }
    return;
  }
  struct tm now;
  if (state == NOT_SET) {
    // The time may arrive over USB at any moment.
    if (halClock.localTime(now)) {
      state = SHOWING;
      shownMinute = -1;
      awakeUntil = millis() + AWAKE_WINDOW_MS;
    }
    return;
  }

  if (!halClock.localTime(now)) {
    state = NOT_SET;
    requestUpdate();
    return;
  }
  if (minuteOfYear(now) != shownMinute) {
    requestUpdateAndWait();
  }

  if (static_cast<long>(awakeUntil - millis()) > 0 || gpio.isUsbConnected()) return;
  sleepUntilNextMinute();
}

void ClockActivity::sleepUntilNextMinute() {
  struct timeval tv;
  gettimeofday(&tv, nullptr);
  const uint32_t msIntoMinute = static_cast<uint32_t>((tv.tv_sec % 60) * 1000 + tv.tv_usec / 1000);
  // Land just past the minute boundary so the redraw shows the new minute.
  const uint32_t sleepMs = 60000 - msIntoMinute + 150;
  if (powerManager.lightSleep(sleepMs)) {
    LOG_DBG("CLOCK", "Woken by power button");
    awakeUntil = millis() + AWAKE_WINDOW_MS;
  }
}

void ClockActivity::render(RenderLock&&) {
  const int screenH = renderer.getScreenHeight();

  renderer.clearScreen();

  if (state != SHOWING) {
    const int midY = screenH / 2;
    if (state == SYNCING) {
      renderer.drawCenteredText(UI_12_FONT_ID, midY, tr(STR_CLOCK_SYNCING));
    } else {
      renderer.drawCenteredText(UI_12_FONT_ID, midY - 20, tr(STR_CLOCK_NOT_SET), true, EpdFontFamily::BOLD);
      renderer.drawCenteredText(UI_10_FONT_ID, midY + 10, tr(STR_CLOCK_NOT_SET_HINT));
      renderer.drawCenteredText(UI_10_FONT_ID, midY + 40, tr(STR_CLOCK_USB_SYNC_HINT));
      const auto labels = mappedInput.mapLabels(tr(STR_HOME), tr(STR_CLOCK_CALIBRATE), "", "");
      GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    }
    renderer.displayBuffer(HalDisplay::HALF_REFRESH);
    shownMinute = -1;
    fastRefreshes = 0;
    return;
  }

  struct tm now;
  if (!halClock.localTime(now)) return;
  face.draw(renderer, now, tr(STR_CLOCK_HINT));

  const bool clean = shownMinute < 0 || fastRefreshes >= MINUTES_PER_CLEAN_REFRESH;
  renderer.displayBuffer(clean ? HalDisplay::HALF_REFRESH : HalDisplay::FAST_REFRESH);
  fastRefreshes = clean ? 0 : fastRefreshes + 1;
  shownMinute = minuteOfYear(now);
}
