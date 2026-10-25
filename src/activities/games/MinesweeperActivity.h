// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The InkDesk Authors

#pragma once

#include <I18n.h>

#include "GameActivity.h"

// Minesweeper on a 9x12 field. Confirm opens a cell (the first one is always
// safe; on an opened number it opens the neighbours once enough flags are set),
// holding Confirm plants or lifts a flag.
class MinesweeperActivity final : public GameActivity {
 public:
  explicit MinesweeperActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : GameActivity("mines", renderer, mappedInput) {}

 private:
  static constexpr int COLS = 9;
  static constexpr int ROWS = 12;
  static constexpr int CELLS = COLS * ROWS;
  static constexpr int MINES = 18;
  enum : uint8_t { HIDDEN = 0, OPEN = 1, FLAG = 2 };
  enum : uint8_t { PLAYING = 0, LOST = 1, WON = 2 };
  struct State {
    uint8_t mine[CELLS];
    uint8_t mark[CELLS];
    uint8_t cursor;
    uint8_t started;  // mines are laid on the first open
    uint8_t result;
  } s{};
  mutable char status[64] = "";

  const char* title() const override { return tr(STR_GAME_MINES); }
  void newGame() override;
  bool restore() override;
  void save() const override;
  bool handleInput() override;
  void drawBoard(const Area& area) const override;
  const char* statusText() const override;
  const char* confirmLabel() const override;

  void layMines(int safeCell);
  int around(int cell, const uint8_t* field, uint8_t value) const;
  void open(int cell);
  void openCell(int cell);
  void checkWin();
};
