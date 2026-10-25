// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The InkDesk Authors

#include "GamesMenuActivity.h"

#include <I18n.h>
#include <Memory.h>

#include "Game2048Activity.h"
#include "GomokuActivity.h"
#include "MinesweeperActivity.h"
#include "SudokuActivity.h"
#include "components/UITheme.h"

namespace fui = freeink::ui;

namespace {
constexpr StrId NAMES[GamesMenuActivity::GAME_COUNT] = {StrId::STR_GAME_2048, StrId::STR_GAME_SUDOKU,
                                                       StrId::STR_GAME_MINES, StrId::STR_GAME_GOMOKU};
constexpr StrId DESCS[GamesMenuActivity::GAME_COUNT] = {StrId::STR_GAME_2048_DESC, StrId::STR_GAME_SUDOKU_DESC,
                                                       StrId::STR_GAME_MINES_DESC, StrId::STR_GAME_GOMOKU_DESC};
}  // namespace

GamesMenuActivity::GamesMenuActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : UiListActivity("Games", renderer, mappedInput) {
  for (int i = 0; i < GAME_COUNT; i++) {
    rows[i].label = I18N.get(NAMES[i]);
    rows[i].subtitle = I18N.get(DESCS[i]);
    rows[i].actionValue = static_cast<int16_t>(i);
  }
}

const char* GamesMenuActivity::headerTitle() const { return tr(STR_GAMES); }

void GamesMenuActivity::onBackButton() { activityManager.goHome(); }

void GamesMenuActivity::activateIndex(const int index) {
  nav.selected = index;
  app.clearTapFlash();
  switch (index) {
    case 0:
      activityManager.replaceActivity(std::make_unique<Game2048Activity>(renderer, mappedInput));
      break;
    case 1:
      activityManager.replaceActivity(std::make_unique<SudokuActivity>(renderer, mappedInput));
      break;
    case 2:
      activityManager.replaceActivity(std::make_unique<MinesweeperActivity>(renderer, mappedInput));
      break;
    case 3:
      activityManager.replaceActivity(std::make_unique<GomokuActivity>(renderer, mappedInput));
      break;
    default:
      break;
  }
}

void GamesMenuActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  screen.setContentMarginFromScreen(fui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight), 0,
                                                static_cast<int16_t>(metrics.buttonHintsHeight), 0});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));
  fui::ListProps props;
  props.items = rows;
  props.count = GAME_COUNT;
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;
  props.subtitleText = screen.theme().smallText;
  props.subtitleText.maxLines = 2;
  syncListViewport(screen, props);
  screen.list(props);
}
