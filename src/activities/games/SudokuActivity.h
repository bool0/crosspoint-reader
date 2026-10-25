// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The InkDesk Authors

#pragma once

#include <I18n.h>

#include "GameActivity.h"

// Sudoku: a freshly generated puzzle with a unique solution each game. Confirm
// opens a 1-9 picker for the selected cell.
class SudokuActivity final : public GameActivity {
 public:
  explicit SudokuActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : GameActivity("sudoku", renderer, mappedInput) {}

 private:
  struct State {
    uint8_t solution[81];
    uint8_t value[81];  // 0 = empty
    uint8_t given[81];  // 1 = part of the puzzle
    uint8_t cursor;
    uint8_t solved;
  } s{};
  bool picking = false;
  int pick = 0;  // 0-8 = digits 1-9, 9 = clear
  mutable char status[64] = "";

  const char* title() const override { return tr(STR_GAME_SUDOKU); }
  void newGame() override;
  bool restore() override;
  void save() const override;
  bool handleInput() override;
  void drawBoard(const Area& area) const override;
  const char* statusText() const override;
  const char* confirmLabel() const override;
  bool consumeBack() override;

  bool conflicts(int cell) const;
};
