// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The InkDesk Authors

#include "MemoListActivity.h"

#include <I18n.h>
#include <Logging.h>
#include <Memory.h>

#include "MemoViewActivity.h"
#include "components/UITheme.h"
#include "util/MemoStore.h"

namespace fui = freeink::ui;

MemoListActivity::MemoListActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : UiListActivity("MemoList", renderer, mappedInput) {}

void MemoListActivity::onEnter() {
  UiListActivity::onEnter();
  reload();
}

void MemoListActivity::onExit() {
  UiListActivity::onExit();
  rows.clear();
  titles.clear();
  fileNames.clear();
}

void MemoListActivity::reload() {
  fileNames = memostore::list();
  pinnedName = memostore::pinned();

  titles.clear();
  titles.reserve(fileNames.size());
  for (const auto& name : fileNames) titles.push_back(memostore::titleOf(name));

  // Row labels point into `titles`, which is not touched again until the next reload.
  rows.clear();
  rows.reserve(fileNames.size() + 1);
  for (size_t i = 0; i < fileNames.size(); i++) {
    fui::ListItem item{};
    item.label = titles[i].c_str();
    item.value = fileNames[i] == pinnedName ? tr(STR_MEMO_ON_CLOCK) : nullptr;
    item.actionValue = static_cast<int16_t>(i);
    rows.push_back(item);
  }
  fui::ListItem edit{};
  edit.label = tr(STR_MEMO_EDIT_ON_PHONE);
  edit.subtitle = tr(STR_MEMO_EDIT_ON_PHONE_HINT);
  edit.actionValue = static_cast<int16_t>(fileNames.size());
  rows.push_back(edit);
  if (nav.selected >= static_cast<int>(rows.size())) nav.selected = 0;
}

const char* MemoListActivity::headerTitle() const { return tr(STR_MEMOS); }

void MemoListActivity::onBackButton() { activityManager.goHome(); }

void MemoListActivity::activateIndex(const int index) {
  nav.selected = index;
  app.clearTapFlash();
  if (index == editRow()) {
    activityManager.goToFileTransfer();
    return;
  }
  const int memo = index;
  if (memo < 0 || memo >= static_cast<int>(fileNames.size())) return;
  auto activity = makeUniqueNoThrow<MemoViewActivity>(renderer, mappedInput, fileNames[memo]);
  if (!activity) {
    LOG_ERR("MEMO", "OOM: MemoViewActivity");
    return;
  }
  startActivityForResult(std::move(activity), [this](const ActivityResult&) { reload(); });
}

void MemoListActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  screen.setContentMarginFromScreen(fui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight), 0,
                                                static_cast<int16_t>(metrics.buttonHintsHeight), 0});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));

  fui::ListProps props;
  props.items = rows.data();
  props.count = static_cast<int>(rows.size());
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;
  props.valueInset = 8;
  syncListViewport(screen, props);
  screen.list(props);
}
