// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The InkDesk Authors

#include "GameActivity.h"

#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <esp_random.h>

#include <cstdio>
#include <string>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
constexpr char DIR[] = "/.crosspoint/games";
// Fast refreshes between half refreshes; e-ink ghosting stays faint up to about this many.
constexpr int CLEAN_EVERY = 12;

std::string statePath(const char* name) { return std::string(DIR) + "/" + name + ".bin"; }
}  // namespace

void GameActivity::onEnter() {
  Activity::onEnter();
  if (!restore()) newGame();
  framesSinceClean = CLEAN_EVERY;  // first frame is a clean one
  requestUpdate();
}

void GameActivity::onExit() {
  save();
  Activity::onExit();
}

void GameActivity::loop() {
  using Button = MappedInputManager::Button;
  if (mappedInput.wasLongPressed(Button::Back, HOLD_MS)) {
    newGame();
    requestUpdate();
    return;
  }
  if (mappedInput.wasReleased(Button::Back)) {
    if (consumeBack()) {
      requestUpdate();
    } else {
      activityManager.goToGames();
    }
    return;
  }
  if (handleInput()) requestUpdate();
}

void GameActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int screenW = renderer.getScreenWidth();
  const int screenH = renderer.getScreenHeight();
  int vt = 0, vr = 0, vb = 0, vl = 0;
  renderer.getOrientedViewableTRBL(&vt, &vr, &vb, &vl);

  renderer.clearScreen();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, screenW, metrics.headerHeight}, title());
  const int statusY = metrics.topPadding + metrics.headerHeight + 6;
  const int statusH = renderer.getLineHeight(UI_10_FONT_ID);
  renderer.drawCenteredText(UI_10_FONT_ID, statusY, statusText());

  Area area;
  area.left = vl + metrics.contentSidePadding;
  area.width = screenW - vl - vr - 2 * metrics.contentSidePadding;
  area.top = statusY + statusH + 8;
  area.height = screenH - metrics.buttonHintsHeight - 8 - area.top;
  drawBoard(area);

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), confirmLabel(), tr(STR_GAME_LEFT), tr(STR_GAME_RIGHT));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  if (++framesSinceClean >= CLEAN_EVERY) {
    framesSinceClean = 0;
    renderer.displayBuffer(HalDisplay::HALF_REFRESH);
  } else {
    renderer.displayBuffer(HalDisplay::FAST_REFRESH);
  }
}

bool GameActivity::writeState(const uint8_t version, const void* data, const size_t size) const {
  Storage.mkdir(DIR);
  HalFile file;
  if (!Storage.openFileForWrite("GAME", statePath(saveName), file)) return false;
  return file.write(&version, 1) == 1 && file.write(data, size) == size;
}

bool GameActivity::readState(const uint8_t version, void* data, const size_t size) const {
  HalFile file;
  const std::string path = statePath(saveName);
  if (!Storage.exists(path.c_str()) || !Storage.openFileForRead("GAME", path, file)) return false;
  uint8_t stored = 0;
  if (file.read(&stored, 1) != 1 || stored != version) return false;
  return file.read(data, size) == static_cast<int>(size);
}

bool GameActivity::wasLeft() const { return mappedInput.wasPressed(MappedInputManager::Button::Left); }
bool GameActivity::wasRight() const { return mappedInput.wasPressed(MappedInputManager::Button::Right); }
bool GameActivity::wasUp() const { return mappedInput.wasPressed(MappedInputManager::Button::Up); }
bool GameActivity::wasDown() const { return mappedInput.wasPressed(MappedInputManager::Button::Down); }

bool GameActivity::wasConfirmTap() const { return mappedInput.wasReleased(MappedInputManager::Button::Confirm); }
bool GameActivity::wasConfirmHold() const {
  return mappedInput.wasLongPressed(MappedInputManager::Button::Confirm, HOLD_MS);
}

uint32_t GameActivity::randomBelow(const uint32_t n) { return n == 0 ? 0 : esp_random() % n; }
