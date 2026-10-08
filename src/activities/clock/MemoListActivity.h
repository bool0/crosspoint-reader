// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The InkDesk Authors

#pragma once

#include <string>
#include <vector>

#include "activities/UiListActivity.h"

// Lists the memos in /memos. The last row starts File Transfer so memos can be
// written from a phone's browser at http://<device>/memo.
class MemoListActivity final : public UiListActivity {
 public:
  explicit MemoListActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;
  void onExit() override;

 private:
  int editRow() const { return static_cast<int>(fileNames.size()); }

  std::vector<std::string> fileNames;
  std::vector<std::string> titles;
  std::vector<freeink::ui::ListItem> rows;
  std::string pinnedName;

  void reload();
  int listCount() const override { return static_cast<int>(rows.size()); }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  const char* headerTitle() const override;
  void onBackButton() override;
};
