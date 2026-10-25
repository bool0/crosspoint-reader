// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The InkDesk Authors

#pragma once

#include "activities/UiListActivity.h"

// Picks one of the built-in games.
class GamesMenuActivity final : public UiListActivity {
 public:
  explicit GamesMenuActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);

  static constexpr int GAME_COUNT = 4;

 private:
  freeink::ui::ListItem rows[GAME_COUNT]{};

  int listCount() const override { return GAME_COUNT; }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  const char* headerTitle() const override;
  void onBackButton() override;
};
