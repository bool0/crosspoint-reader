// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The InkDesk Authors

#pragma once

#include <I18n.h>

#include "GameActivity.h"

// Gomoku (five in a row) on a 15x15 board against the device. The player has
// black and moves first; the reply is computed before the redraw, so each turn
// costs one refresh.
class GomokuActivity final : public GameActivity {
 public:
  explicit GomokuActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : GameActivity("gomoku", renderer, mappedInput) {}

 private:
  static constexpr int N = 15;
  enum : uint8_t { EMPTY = 0, PLAYER = 1, DEVICE = 2 };
  enum : uint8_t { PLAYING = 0, PLAYER_WON = 1, DEVICE_WON = 2, DRAW = 3 };
  struct State {
    uint8_t board[N * N];
    uint8_t cursor;
    uint8_t lastDevice;  // N * N = none
    uint8_t result;
    uint8_t moves;
  } s{};
  uint8_t winLine[5] = {};
  bool hasWinLine = false;

  const char* title() const override { return tr(STR_GAME_GOMOKU); }
  void newGame() override;
  bool restore() override;
  void save() const override;
  bool handleInput() override;
  void drawBoard(const Area& area) const override;
  const char* statusText() const override;
  const char* confirmLabel() const override;

  bool fiveFrom(int cell);
  int lineScore(int cell, uint8_t who) const;
  int deviceMove() const;
  void place(int cell, uint8_t who);
};
