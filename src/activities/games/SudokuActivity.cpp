// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The InkDesk Authors

#include "SudokuActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <esp_random.h>

#include "fontIds.h"

namespace {
constexpr uint8_t VERSION = 1;
constexpr int REMOVE_TARGET = 46;  // blanks in a new puzzle (medium)

// Digits 1-9 still allowed in `cell`, as bits 1-9.
uint16_t candidates(const uint8_t* g, const int cell) {
  const int r = cell / 9, c = cell % 9, br = r / 3 * 3, bc = c / 3 * 3;
  uint16_t used = 0;
  for (int k = 0; k < 9; k++) {
    used |= 1u << g[r * 9 + k];
    used |= 1u << g[k * 9 + c];
    used |= 1u << g[(br + k / 3) * 9 + bc + k % 3];
  }
  return static_cast<uint16_t>(~used & 0x3FE);
}

// Empty cell with the fewest candidates, or -1 when the grid is full.
int tightestCell(const uint8_t* g, uint16_t& mask) {
  int best = -1, bestCount = 10;
  for (int i = 0; i < 81; i++) {
    if (g[i]) continue;
    const uint16_t m = candidates(g, i);
    const int n = __builtin_popcount(m);
    if (n < bestCount) {
      best = i;
      bestCount = n;
      mask = m;
      if (n <= 1) break;
    }
  }
  return best;
}

// Counts solutions up to `limit` (recursion depth is at most 81 small frames).
int countSolutions(uint8_t* g, const int limit) {
  uint16_t mask = 0;
  const int cell = tightestCell(g, mask);
  if (cell < 0) return 1;
  int found = 0;
  for (int d = 1; d <= 9 && found < limit; d++) {
    if (!(mask & (1u << d))) continue;
    g[cell] = d;
    found += countSolutions(g, limit - found);
  }
  g[cell] = 0;
  return found;
}

bool fillRandom(uint8_t* g) {
  uint16_t mask = 0;
  const int cell = tightestCell(g, mask);
  if (cell < 0) return true;
  uint8_t order[9] = {1, 2, 3, 4, 5, 6, 7, 8, 9};
  for (int i = 8; i > 0; i--) std::swap(order[i], order[esp_random() % (i + 1)]);
  for (const uint8_t d : order) {
    if (!(mask & (1u << d))) continue;
    g[cell] = d;
    if (fillRandom(g)) return true;
  }
  g[cell] = 0;
  return false;
}
}  // namespace

void SudokuActivity::newGame() {
  s = State{};
  fillRandom(s.solution);
  uint8_t puzzle[81];
  memcpy(puzzle, s.solution, sizeof(puzzle));
  int order[81];
  for (int i = 0; i < 81; i++) order[i] = i;
  for (int i = 80; i > 0; i--) std::swap(order[i], order[randomBelow(i + 1)]);
  int removed = 0;
  for (int i = 0; i < 81 && removed < REMOVE_TARGET; i++) {
    const uint8_t keep = puzzle[order[i]];
    puzzle[order[i]] = 0;
    uint8_t probe[81];
    memcpy(probe, puzzle, sizeof(probe));
    if (countSolutions(probe, 2) == 1) {
      removed++;
    } else {
      puzzle[order[i]] = keep;
    }
  }
  LOG_DBG("GAME", "Sudoku generated, %d blanks", removed);
  for (int i = 0; i < 81; i++) {
    s.value[i] = puzzle[i];
    s.given[i] = puzzle[i] != 0;
  }
  s.cursor = 40;
  picking = false;
}

bool SudokuActivity::restore() { return readState(VERSION, &s, sizeof(s)); }
void SudokuActivity::save() const { writeState(VERSION, &s, sizeof(s)); }

bool SudokuActivity::conflicts(const int cell) const {
  const uint8_t v = s.value[cell];
  if (!v) return false;
  const int r = cell / 9, c = cell % 9, br = r / 3 * 3, bc = c / 3 * 3;
  for (int k = 0; k < 9; k++) {
    const int peers[3] = {r * 9 + k, k * 9 + c, (br + k / 3) * 9 + bc + k % 3};
    for (const int p : peers) {
      if (p != cell && s.value[p] == v) return true;
    }
  }
  return false;
}

bool SudokuActivity::consumeBack() {
  if (!picking) return false;
  picking = false;
  return true;
}

bool SudokuActivity::handleInput() {
  if (wasConfirmHold()) {
    newGame();
    return true;
  }
  if (picking) {
    if (wasLeft() || wasUp()) {
      pick = (pick + 9) % 10;
      return true;
    }
    if (wasRight() || wasDown()) {
      pick = (pick + 1) % 10;
      return true;
    }
    if (wasConfirmTap()) {
      s.value[s.cursor] = pick == 9 ? 0 : static_cast<uint8_t>(pick + 1);
      picking = false;
      s.solved = memcmp(s.value, s.solution, sizeof(s.value)) == 0;
      return true;
    }
    return false;
  }
  if (s.solved && wasConfirmTap()) {
    newGame();
    return true;
  }
  const int r = s.cursor / 9, c = s.cursor % 9;
  if (wasLeft()) s.cursor = r * 9 + (c + 8) % 9;
  else if (wasRight()) s.cursor = r * 9 + (c + 1) % 9;
  else if (wasUp()) s.cursor = ((r + 8) % 9) * 9 + c;
  else if (wasDown()) s.cursor = ((r + 1) % 9) * 9 + c;
  else if (wasConfirmTap() && !s.given[s.cursor] && !s.solved) {
    picking = true;
    pick = s.value[s.cursor] ? s.value[s.cursor] - 1 : 0;
  } else {
    return false;
  }
  return true;
}

const char* SudokuActivity::statusText() const {
  if (s.solved) return tr(STR_GAME_SOLVED);
  if (picking) return tr(STR_GAME_PICK_HINT);
  int filled = 0;
  for (const uint8_t v : s.value) filled += v != 0;
  snprintf(status, sizeof(status), tr(STR_GAME_SUDOKU_STATUS), filled);
  return status;
}

const char* SudokuActivity::confirmLabel() const {
  if (s.solved) return tr(STR_GAME_NEW);
  if (picking) return tr(STR_GAME_FILL);
  return s.given[s.cursor] ? "" : tr(STR_GAME_FILL);
}

void SudokuActivity::drawBoard(const Area& area) const {
  const int pickerH = 56;
  const int size = std::min(area.width, area.height - pickerH - 12) / 9 * 9;
  const int cell = size / 9;
  const int x0 = area.left + (area.width - size) / 2;
  const int y0 = area.top;
  const int lineH = renderer.getLineHeight(NOTOSANS_18_FONT_ID);

  for (int i = 0; i < 81; i++) {
    const int x = x0 + (i % 9) * cell, y = y0 + (i / 9) * cell;
    const bool cursor = i == s.cursor;
    if (cursor) {
      renderer.fillRect(x, y, cell, cell, true);
    } else if (conflicts(i) && !s.given[i]) {
      renderer.fillRectDither(x + 1, y + 1, cell - 2, cell - 2, Color::LightGray);
    }
    if (!s.value[i]) continue;
    const char digit[2] = {static_cast<char>('0' + s.value[i]), 0};
    const auto style = s.given[i] ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR;
    const int w = renderer.getTextWidth(NOTOSANS_18_FONT_ID, digit, style);
    renderer.drawText(NOTOSANS_18_FONT_ID, x + (cell - w) / 2, y + (cell - lineH) / 2, digit, !cursor, style);
    if (!s.given[i]) {
      // Underline the player's own digits so they read apart from the puzzle's.
      renderer.drawLine(x + cell / 2 - 8, y + cell - 8, x + cell / 2 + 8, y + cell - 8, !cursor);
    }
  }
  for (int k = 0; k <= 9; k++) {
    const int t = k % 3 == 0 ? 3 : 1;
    renderer.fillRect(x0 + k * cell - t / 2, y0, t, size, true);
    renderer.fillRect(x0, y0 + k * cell - t / 2, size, t, true);
  }

  if (!picking) return;
  const int py = y0 + size + 12;
  const int boxW = area.width / 10;
  const int px0 = area.left + (area.width - boxW * 10) / 2;
  const int lineUi = renderer.getLineHeight(UI_12_FONT_ID);
  for (int k = 0; k < 10; k++) {
    const int x = px0 + k * boxW;
    const bool on = k == pick;
    renderer.fillRoundedRect(x + 2, py, boxW - 4, pickerH, 6, on ? Color::Black : Color::White);
    if (!on) renderer.drawRect(x + 2, py, boxW - 4, pickerH);
    const char digit[2] = {static_cast<char>('1' + k), 0};
    const char* label = k == 9 ? tr(STR_GAME_CLEAR) : digit;
    const int font = k == 9 ? UI_10_FONT_ID : UI_12_FONT_ID;
    const int w = renderer.getTextWidth(font, label);
    renderer.drawText(font, x + (boxW - w) / 2, py + (pickerH - lineUi) / 2, label, !on);
  }
}
