// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The InkDesk Authors

#include "Game2048Activity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <cstdio>

#include "components/ClockFace.h"
#include "fontIds.h"

namespace {
constexpr uint8_t VERSION = 1;
}

void Game2048Activity::newGame() {
  const uint32_t best = s.best;
  s = State{};
  s.best = best;
  spawn();
  spawn();
}

bool Game2048Activity::restore() { return readState(VERSION, &s, sizeof(s)); }
void Game2048Activity::save() const { writeState(VERSION, &s, sizeof(s)); }

void Game2048Activity::spawn() {
  int empty[N * N];
  int count = 0;
  for (int i = 0; i < N * N; i++) {
    if (s.cells[i] == 0) empty[count++] = i;
  }
  if (count == 0) return;
  s.cells[empty[randomBelow(count)]] = randomBelow(10) == 0 ? 2 : 1;
}

bool Game2048Activity::slide(const int dx, const int dy) {
  bool moved = false;
  for (int line = 0; line < N; line++) {
    // Walk each row/column from the edge the tiles slide towards.
    int idx[N];
    for (int k = 0; k < N; k++) {
      const int pos = (dx > 0 || dy > 0) ? N - 1 - k : k;
      idx[k] = dx != 0 ? line * N + pos : pos * N + line;
    }
    uint8_t out[N] = {};
    int n = 0;
    bool mergedLast = false;
    for (int k = 0; k < N; k++) {
      const uint8_t v = s.cells[idx[k]];
      if (v == 0) continue;
      if (n > 0 && out[n - 1] == v && !mergedLast) {
        out[n - 1]++;
        s.score += 1u << out[n - 1];
        if (out[n - 1] == 11) s.won = 1;
        mergedLast = true;
      } else {
        out[n++] = v;
        mergedLast = false;
      }
    }
    for (int k = 0; k < N; k++) {
      if (s.cells[idx[k]] != out[k]) moved = true;
      s.cells[idx[k]] = out[k];
    }
  }
  s.best = std::max(s.best, s.score);
  return moved;
}

bool Game2048Activity::canMove() const {
  for (int r = 0; r < N; r++) {
    for (int c = 0; c < N; c++) {
      const uint8_t v = s.cells[r * N + c];
      if (v == 0) return true;
      if (c + 1 < N && s.cells[r * N + c + 1] == v) return true;
      if (r + 1 < N && s.cells[(r + 1) * N + c] == v) return true;
    }
  }
  return false;
}

bool Game2048Activity::handleInput() {
  if (wasConfirmHold() || (s.over && wasConfirmTap())) {
    newGame();
    return true;
  }
  if (s.over) return false;
  int dx = 0, dy = 0;
  if (wasLeft()) dx = -1;
  else if (wasRight()) dx = 1;
  else if (wasUp()) dy = -1;
  else if (wasDown()) dy = 1;
  else return false;
  if (!slide(dx, dy)) return false;
  spawn();
  if (!canMove()) s.over = 1;
  return true;
}

const char* Game2048Activity::statusText() const {
  if (s.over) {
    snprintf(status, sizeof(status), tr(STR_GAME_OVER_SCORE), static_cast<unsigned>(s.score));
  } else {
    snprintf(status, sizeof(status), tr(STR_GAME_SCORE), static_cast<unsigned>(s.score),
             static_cast<unsigned>(s.best));
  }
  return status;
}

const char* Game2048Activity::confirmLabel() const { return s.over ? tr(STR_GAME_NEW) : ""; }

void Game2048Activity::drawBoard(const Area& area) const {
  const int size = std::min(area.width, area.height);
  const int gap = 8;
  const int cell = (size - gap * (N + 1)) / N;
  const int x0 = area.left + (area.width - size) / 2;
  const int y0 = area.top + (area.height - size) / 2;
  renderer.fillRoundedRect(x0, y0, size, size, 10, Color::Black);
  for (int r = 0; r < N; r++) {
    for (int c = 0; c < N; c++) {
      const int x = x0 + gap + c * (cell + gap);
      const int y = y0 + gap + r * (cell + gap);
      const uint8_t v = s.cells[r * N + c];
      // Shade climbs with the value so big tiles stand out without colour.
      const Color fill = v == 0 ? Color::White : v <= 3 ? Color::White : v <= 6 ? Color::LightGray : v <= 9 ? Color::DarkGray : Color::Black;
      renderer.fillRoundedRect(x, y, cell, cell, 6, fill);
      if (v == 0) continue;
      const int value = 1 << v;
      const int digits = value >= 10000 ? 5 : value >= 1000 ? 4 : value >= 100 ? 3 : value >= 10 ? 2 : 1;
      const int h = cell * (digits <= 2 ? 42 : digits == 3 ? 34 : digits == 4 ? 27 : 22) / 100;
      const int w = ClockFace::numberWidth(h, value);
      const Color ink = fill == Color::Black || fill == Color::DarkGray ? Color::White : Color::Black;
      ClockFace::drawNumber(renderer, x + (cell - w) / 2, y + (cell - h) / 2, h, value, ink);
    }
  }
  if (s.over) {
    const char* text = tr(STR_GAME_OVER);
    const int tw = renderer.getTextWidth(UI_12_FONT_ID, text, EpdFontFamily::BOLD) + 32;
    const int th = renderer.getLineHeight(UI_12_FONT_ID) + 20;
    const int bx = x0 + (size - tw) / 2, by = y0 + (size - th) / 2;
    renderer.fillRoundedRect(bx - 3, by - 3, tw + 6, th + 6, 10, Color::Black);
    renderer.fillRoundedRect(bx, by, tw, th, 8, Color::White);
    renderer.drawText(UI_12_FONT_ID, bx + 16, by + 10, text, true, EpdFontFamily::BOLD);
  }
}
