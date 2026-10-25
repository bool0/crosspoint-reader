// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The InkDesk Authors

#pragma once

#include <cstddef>
#include <cstdint>

#include "activities/Activity.h"

// Shared frame for the games: turn-based boards that redraw once per move.
//  - Back leaves to the games menu; holding Back for HOLD_MS starts a new game.
//  - Every move is a fast refresh, with a half refresh every few moves so e-ink
//    ghosting never builds up.
//  - State is saved to /.crosspoint/games/<name>.bin on exit and restored on entry.
class GameActivity : public Activity {
 public:
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 protected:
  struct Area {
    int left = 0, top = 0, width = 0, height = 0;
  };

  GameActivity(const char* name, GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity(name, renderer, mappedInput), saveName(name) {}

  static constexpr unsigned long HOLD_MS = 800;

  // Game hooks.
  virtual const char* title() const = 0;
  virtual void newGame() = 0;
  // Restores saved state; false starts a new game.
  virtual bool restore() = 0;
  virtual void save() const = 0;
  // Handles one input frame; return true when the screen must be redrawn.
  virtual bool handleInput() = 0;
  // Draws the board into `area`; the status line and hints are drawn around it.
  virtual void drawBoard(const Area& area) const = 0;
  virtual const char* statusText() const = 0;
  virtual const char* confirmLabel() const = 0;
  // A short Back press inside the game (e.g. closing a picker); false leaves the game.
  virtual bool consumeBack() { return false; }

  // Fixed-size binary state for save()/restore(), tagged with a version.
  bool writeState(uint8_t version, const void* data, size_t size) const;
  bool readState(uint8_t version, void* data, size_t size) const;

  // Arrow input in board terms: the front Left/Right buttons and the side buttons.
  bool wasLeft() const;
  bool wasRight() const;
  bool wasUp() const;
  bool wasDown() const;
  // Confirm released after a short press / after a hold of HOLD_MS.
  bool wasConfirmTap() const;
  bool wasConfirmHold() const;

  static uint32_t randomBelow(uint32_t n);

 private:
  const char* saveName;
  int framesSinceClean = 0;
};
