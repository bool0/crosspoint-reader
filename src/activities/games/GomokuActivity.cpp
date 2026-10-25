// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The InkDesk Authors

#include "GomokuActivity.h"

#include <GfxRenderer.h>

#include <algorithm>
#include <cstdlib>

namespace {
constexpr uint8_t VERSION = 1;
constexpr int DIRS[4][2] = {{1, 0}, {0, 1}, {1, 1}, {1, -1}};

// Value of a run of `len` stones with `open` free ends (0-2), as if just placed.
int runValue(const int len, const int open) {
  if (len >= 5) return 100000;
  if (open == 0) return 0;
  switch (len) {
    case 4:
      return open == 2 ? 20000 : 2000;
    case 3:
      return open == 2 ? 1500 : 150;
    case 2:
      return open == 2 ? 120 : 15;
    default:
      return open == 2 ? 10 : 2;
  }
}
}  // namespace

void GomokuActivity::newGame() {
  s = State{};
  s.cursor = N * N / 2;
  s.lastDevice = N * N;
  hasWinLine = false;
}

bool GomokuActivity::restore() {
  hasWinLine = false;
  return readState(VERSION, &s, sizeof(s));
}
void GomokuActivity::save() const { writeState(VERSION, &s, sizeof(s)); }

// Whether the stone at `cell` completes five; remembers the line to highlight.
bool GomokuActivity::fiveFrom(const int cell) {
  const uint8_t who = s.board[cell];
  const int r = cell / N, c = cell % N;
  for (const auto& d : DIRS) {
    int line[2 * N];
    int n = 0;
    for (int k = -4; k <= 4; k++) {
      const int rr = r + d[1] * k, cc = c + d[0] * k;
      if (rr < 0 || rr >= N || cc < 0 || cc >= N || s.board[rr * N + cc] != who) {
        n = 0;
        continue;
      }
      line[n++] = rr * N + cc;
      if (n == 5) {
        for (int i = 0; i < 5; i++) winLine[i] = static_cast<uint8_t>(line[i]);
        hasWinLine = true;
        return true;
      }
    }
  }
  return false;
}

// How strong a stone of `who` at the empty `cell` would be, summed over the four lines.
int GomokuActivity::lineScore(const int cell, const uint8_t who) const {
  const int r = cell / N, c = cell % N;
  int total = 0;
  for (const auto& d : DIRS) {
    int len = 1, open = 0;
    for (const int sign : {1, -1}) {
      int rr = r + d[1] * sign, cc = c + d[0] * sign;
      while (rr >= 0 && rr < N && cc >= 0 && cc < N && s.board[rr * N + cc] == who) {
        len++;
        rr += d[1] * sign;
        cc += d[0] * sign;
      }
      if (rr >= 0 && rr < N && cc >= 0 && cc < N && s.board[rr * N + cc] == EMPTY) open++;
    }
    total += runValue(len, open);
  }
  return total;
}

// Greedy reply: the empty point near the stones that best builds its own lines
// or blocks the player's, slightly preferring attack and the centre.
int GomokuActivity::deviceMove() const {
  int best = -1, bestScore = -1;
  for (int i = 0; i < N * N; i++) {
    if (s.board[i] != EMPTY) continue;
    const int r = i / N, c = i % N;
    bool near = false;
    for (int dr = -2; dr <= 2 && !near; dr++) {
      for (int dc = -2; dc <= 2 && !near; dc++) {
        const int rr = r + dr, cc = c + dc;
        near = rr >= 0 && rr < N && cc >= 0 && cc < N && s.board[rr * N + cc] != EMPTY;
      }
    }
    if (!near) continue;
    const int score = lineScore(i, DEVICE) * 11 / 10 + lineScore(i, PLAYER) + (14 - abs(r - 7) - abs(c - 7));
    if (score > bestScore) {
      bestScore = score;
      best = i;
    }
  }
  return best;
}

void GomokuActivity::place(const int cell, const uint8_t who) {
  s.board[cell] = who;
  s.moves++;
  if (fiveFrom(cell)) {
    s.result = who == PLAYER ? PLAYER_WON : DEVICE_WON;
  } else if (s.moves >= N * N) {
    s.result = DRAW;
  }
}

bool GomokuActivity::handleInput() {
  if (s.result != PLAYING) {
    if (wasConfirmTap() || wasConfirmHold()) {
      newGame();
      return true;
    }
    return false;
  }
  const int r = s.cursor / N, c = s.cursor % N;
  if (wasLeft()) s.cursor = r * N + (c + N - 1) % N;
  else if (wasRight()) s.cursor = r * N + (c + 1) % N;
  else if (wasUp()) s.cursor = ((r + N - 1) % N) * N + c;
  else if (wasDown()) s.cursor = ((r + 1) % N) * N + c;
  else if (wasConfirmTap() && s.board[s.cursor] == EMPTY) {
    place(s.cursor, PLAYER);
    if (s.result == PLAYING) {
      const int reply = deviceMove();
      if (reply >= 0) {
        place(reply, DEVICE);
        s.lastDevice = static_cast<uint8_t>(reply);
      }
    }
  } else {
    return false;
  }
  return true;
}

const char* GomokuActivity::statusText() const {
  switch (s.result) {
    case PLAYER_WON:
      return tr(STR_GAME_WIN);
    case DEVICE_WON:
      return tr(STR_GAME_LOSE);
    case DRAW:
      return tr(STR_GAME_DRAW);
    default:
      return tr(STR_GAME_GOMOKU_TURN);
  }
}

const char* GomokuActivity::confirmLabel() const {
  return s.result != PLAYING ? tr(STR_GAME_NEW) : tr(STR_GAME_PLACE);
}

void GomokuActivity::drawBoard(const Area& area) const {
  const int cell = std::min(area.width, area.height) / N;
  const int x0 = area.left + (area.width - cell * N) / 2;
  const int y0 = area.top + (area.height - cell * N) / 2;
  const int half = cell / 2;
  for (int k = 0; k < N; k++) {
    renderer.drawLine(x0 + half, y0 + half + k * cell, x0 + half + (N - 1) * cell, y0 + half + k * cell);
    renderer.drawLine(x0 + half + k * cell, y0 + half, x0 + half + k * cell, y0 + half + (N - 1) * cell);
  }
  // Star points help find the middle of the board.
  for (const int p : {3 * N + 3, 3 * N + 11, 7 * N + 7, 11 * N + 3, 11 * N + 11}) {
    renderer.fillRoundedRect(x0 + (p % N) * cell + half - 3, y0 + (p / N) * cell + half - 3, 6, 6, 3, Color::Black);
  }
  const int d = cell * 84 / 100;
  for (int i = 0; i < N * N; i++) {
    if (s.board[i] == EMPTY) continue;
    const int cx = x0 + (i % N) * cell + (cell - d) / 2, cy = y0 + (i / N) * cell + (cell - d) / 2;
    renderer.fillRoundedRect(cx, cy, d, d, d / 2, Color::Black);
    if (s.board[i] == DEVICE) {
      renderer.fillRoundedRect(cx + 2, cy + 2, d - 4, d - 4, (d - 4) / 2, Color::White);
      if (i == s.lastDevice) renderer.fillRoundedRect(cx + d / 2 - 3, cy + d / 2 - 3, 6, 6, 3, Color::Black);
    }
  }
  if (hasWinLine) {
    const int a = winLine[0], b = winLine[4];
    renderer.drawLine(x0 + (a % N) * cell + half, y0 + (a / N) * cell + half, x0 + (b % N) * cell + half,
                      y0 + (b / N) * cell + half, 4, true);
  }
  if (s.result == PLAYING) {
    const int x = x0 + (s.cursor % N) * cell, y = y0 + (s.cursor / N) * cell;
    renderer.drawRect(x, y, cell, cell, 3, true);
  }
}
