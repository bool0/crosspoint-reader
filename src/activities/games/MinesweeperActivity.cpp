// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The InkDesk Authors

#include "MinesweeperActivity.h"

#include <GfxRenderer.h>

#include <algorithm>
#include <cstdio>

#include "fontIds.h"

namespace {
constexpr uint8_t VERSION = 1;

template <typename F>
void forNeighbours(const int cell, const int cols, const int rows, F f) {
  const int r = cell / cols, c = cell % cols;
  for (int dr = -1; dr <= 1; dr++) {
    for (int dc = -1; dc <= 1; dc++) {
      const int nr = r + dr, nc = c + dc;
      if ((dr || dc) && nr >= 0 && nr < rows && nc >= 0 && nc < cols) f(nr * cols + nc);
    }
  }
}
}  // namespace

void MinesweeperActivity::newGame() {
  s = State{};
  s.cursor = (ROWS / 2 - 1) * COLS + COLS / 2;
}

bool MinesweeperActivity::restore() { return readState(VERSION, &s, sizeof(s)); }
void MinesweeperActivity::save() const { writeState(VERSION, &s, sizeof(s)); }

// Keeps the first cell and its neighbours clear, so the first open always spreads.
void MinesweeperActivity::layMines(const int safeCell) {
  uint8_t safe[CELLS] = {};
  safe[safeCell] = 1;
  forNeighbours(safeCell, COLS, ROWS, [&](const int n) { safe[n] = 1; });
  int laid = 0;
  while (laid < MINES) {
    const int i = static_cast<int>(randomBelow(CELLS));
    if (safe[i] || s.mine[i]) continue;
    s.mine[i] = 1;
    laid++;
  }
  s.started = 1;
}

int MinesweeperActivity::around(const int cell, const uint8_t* field, const uint8_t value) const {
  int n = 0;
  forNeighbours(cell, COLS, ROWS, [&](const int i) { n += field[i] == value; });
  return n;
}

// Opens one cell, spreading through empty areas with an explicit stack.
void MinesweeperActivity::openCell(const int start) {
  uint8_t stack[CELLS];
  int top = 0;
  stack[top++] = static_cast<uint8_t>(start);
  while (top > 0) {
    const int cell = stack[--top];
    if (s.mark[cell] != HIDDEN) continue;
    s.mark[cell] = OPEN;
    if (s.mine[cell]) {
      s.result = LOST;
      s.cursor = static_cast<uint8_t>(cell);
      return;
    }
    if (around(cell, s.mine, 1) == 0) {
      forNeighbours(cell, COLS, ROWS, [&](const int n) {
        if (s.mark[n] == HIDDEN && top < CELLS) stack[top++] = static_cast<uint8_t>(n);
      });
    }
  }
}

void MinesweeperActivity::open(const int cell) {
  if (!s.started) layMines(cell);
  if (s.mark[cell] == HIDDEN) {
    openCell(cell);
  } else if (s.mark[cell] == OPEN && around(cell, s.mark, FLAG) == around(cell, s.mine, 1)) {
    // Chord: the number is satisfied by flags, so open the rest around it.
    forNeighbours(cell, COLS, ROWS, [&](const int n) {
      if (s.result == PLAYING && s.mark[n] == HIDDEN) openCell(n);
    });
  }
  checkWin();
}

void MinesweeperActivity::checkWin() {
  if (s.result != PLAYING) return;
  for (int i = 0; i < CELLS; i++) {
    if (!s.mine[i] && s.mark[i] != OPEN) return;
  }
  s.result = WON;
}

bool MinesweeperActivity::handleInput() {
  if (s.result != PLAYING) {
    if (wasConfirmTap() || wasConfirmHold()) {
      newGame();
      return true;
    }
    return false;
  }
  const int r = s.cursor / COLS, c = s.cursor % COLS;
  if (wasLeft()) s.cursor = r * COLS + (c + COLS - 1) % COLS;
  else if (wasRight()) s.cursor = r * COLS + (c + 1) % COLS;
  else if (wasUp()) s.cursor = ((r + ROWS - 1) % ROWS) * COLS + c;
  else if (wasDown()) s.cursor = ((r + 1) % ROWS) * COLS + c;
  else if (wasConfirmHold()) {
    if (s.mark[s.cursor] == HIDDEN) s.mark[s.cursor] = FLAG;
    else if (s.mark[s.cursor] == FLAG) s.mark[s.cursor] = HIDDEN;
    else return false;
  } else if (wasConfirmTap()) {
    if (s.mark[s.cursor] == FLAG) return false;
    open(s.cursor);
  } else {
    return false;
  }
  return true;
}

const char* MinesweeperActivity::statusText() const {
  if (s.result == WON) return tr(STR_GAME_WIN);
  if (s.result == LOST) return tr(STR_GAME_MINES_LOST);
  int flags = 0;
  for (const uint8_t m : s.mark) flags += m == FLAG;
  snprintf(status, sizeof(status), tr(STR_GAME_MINES_STATUS), MINES - flags);
  return status;
}

const char* MinesweeperActivity::confirmLabel() const {
  return s.result != PLAYING ? tr(STR_GAME_NEW) : tr(STR_GAME_OPEN);
}

void MinesweeperActivity::drawBoard(const Area& area) const {
  const int cell = std::min(area.width / COLS, area.height / ROWS);
  const int x0 = area.left + (area.width - cell * COLS) / 2;
  const int y0 = area.top + (area.height - cell * ROWS) / 2;
  const int lineH = renderer.getLineHeight(UI_12_FONT_ID);
  const bool reveal = s.result != PLAYING;

  for (int i = 0; i < CELLS; i++) {
    const int x = x0 + (i % COLS) * cell, y = y0 + (i / COLS) * cell;
    const uint8_t mark = s.mark[i];
    if (mark == OPEN && !s.mine[i]) {
      const int n = around(i, s.mine, 1);
      if (n > 0) {
        const char text[2] = {static_cast<char>('0' + n), 0};
        const int w = renderer.getTextWidth(UI_12_FONT_ID, text, EpdFontFamily::BOLD);
        renderer.drawText(UI_12_FONT_ID, x + (cell - w) / 2, y + (cell - lineH) / 2, text, true, EpdFontFamily::BOLD);
      }
    } else if (s.mine[i] && (reveal || mark == OPEN)) {
      if (i == s.cursor && s.result == LOST) renderer.fillRect(x + 2, y + 2, cell - 4, cell - 4, true);
      const int d = cell / 2;
      renderer.fillRoundedRect(x + (cell - d) / 2, y + (cell - d) / 2, d, d, d / 2,
                               i == s.cursor && s.result == LOST ? Color::White : Color::Black);
    } else {
      renderer.fillRectDither(x + 2, y + 2, cell - 4, cell - 4, Color::LightGray);
      if (mark == FLAG) {
        // A small pennant on a pole.
        const int px = x + cell / 2 - 2, top = y + cell / 4, bottom = y + cell * 3 / 4;
        renderer.fillRect(px, top, 3, bottom - top, true);
        for (int k = 0; k < cell / 4; k++) renderer.drawLine(px + 3, top + k, px + 3 + cell / 4 - k, top + k);
        renderer.fillRect(px - 6, bottom, 15, 3, true);
      }
    }
    renderer.drawRect(x, y, cell, cell);
  }
  if (s.result != LOST) {
    const int x = x0 + (s.cursor % COLS) * cell, y = y0 + (s.cursor / COLS) * cell;
    renderer.drawRect(x - 2, y - 2, cell + 4, cell + 4, 4, true);
  }
}
