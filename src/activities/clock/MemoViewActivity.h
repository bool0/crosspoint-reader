// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The InkDesk Authors

#pragma once

#include <string>
#include <vector>

#include "activities/Activity.h"
#include "util/MemoMarkdown.h"

// Shows one Markdown memo in the reader font, paged. Checklist items can be
// ticked: Left/Right select an item, Confirm toggles it and saves the file.
// Holding Confirm (or Confirm on a memo without a checklist) puts the memo on
// the clock.
class MemoViewActivity final : public Activity {
 public:
  explicit MemoViewActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string fileName)
      : Activity("MemoView", renderer, mappedInput), fileName(std::move(fileName)) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  static constexpr unsigned long PIN_HOLD_MS = 700;

  std::string fileName;
  std::string title;
  std::string text;
  memomd::Doc doc;
  std::vector<int> pageStarts;  // first line index of each page
  int fontId = 0;
  int bodyWidth = 0;
  int page = 0;
  int selected = -1;  // checklist item under the cursor, -1 for none
  bool isPinned = false;
  bool confirmArmed = false;  // Confirm went down on this screen

  void relayout();
  int pageOfLine(int line) const;
  int firstLineOfItem(int item) const;
  void selectItem(int item);
  void showPage(int newPage);
  void pin();
};
