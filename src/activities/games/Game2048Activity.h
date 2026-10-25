// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The InkDesk Authors

#pragma once

#include "GameActivity.h"

// 2048: the four buttons slide the tiles; equal tiles merge.
class Game2048Activity final : public GameActivity {
 public:
  explicit Game2048Activity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : GameActivity("2048", renderer, mappedInput) {}

 private:
  static constexpr int N = 4;
  struct State {
    uint8_t cells[N * N];  // exponent: 0 = empty, 1 = 2, 2 = 4, ...
    uint32_t score;
    uint32_t best;
    uint8_t over;
    uint8_t won;  // 2048 reached (play may continue)
  } s{};
  mutable char status[64] = "";

  const char* title() const override { return "2048"; }
  void newGame() override;
  bool restore() override;
  void save() const override;
  bool handleInput() override;
  void drawBoard(const Area& area) const override;
  const char* statusText() const override;
  const char* confirmLabel() const override;

  // Slides towards (dx, dy); returns whether anything moved.
  bool slide(int dx, int dy);
  void spawn();
  bool canMove() const;
};
