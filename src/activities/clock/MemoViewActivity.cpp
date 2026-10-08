// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The InkDesk Authors

#include "MemoViewActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>

#include <algorithm>
#include <cstdio>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/MemoStore.h"

namespace {

int bodyTop() {
  const auto& metrics = UITheme::getInstance().getMetrics();
  return metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
}

}  // namespace

void MemoViewActivity::onEnter() {
  Activity::onEnter();
  title = memostore::titleOf(fileName);
  isPinned = memostore::pinned() == fileName;
  page = 0;
  if (!memostore::read(fileName, text)) text.clear();

  int vt = 0, vr = 0, vb = 0, vl = 0;
  renderer.getOrientedViewableTRBL(&vt, &vr, &vb, &vl);
  fontId = memostore::textFontId(renderer);
  bodyWidth = renderer.getScreenWidth() - vl - vr - 2 * UITheme::getInstance().getMetrics().contentSidePadding;
  relayout();
  selected = doc.checkCount > 0 ? 0 : -1;
  if (selected >= 0) page = pageOfLine(firstLineOfItem(selected));
  requestUpdate();
}

void MemoViewActivity::onExit() {
  Activity::onExit();
  doc = memomd::Doc{};
  pageStarts.clear();
  text.clear();
  text.shrink_to_fit();
}

void MemoViewActivity::relayout() {
  doc = memomd::layout(renderer, fontId, text, bodyWidth);
  const int bodyH = renderer.getScreenHeight() - bodyTop() - UITheme::getInstance().getMetrics().buttonHintsHeight;
  pageStarts.assign(1, 0);
  int y = 0;
  for (size_t i = 0; i < doc.lines.size(); i++) {
    const int h = doc.lines[i].height;
    if (y > 0 && y + h > bodyH) {
      pageStarts.push_back(static_cast<int>(i));
      y = 0;
    }
    // A page never starts with a blank line.
    if (y == 0 && doc.lines[i].kind == memomd::Kind::Blank && pageStarts.back() == static_cast<int>(i)) {
      pageStarts.back() = static_cast<int>(i) + 1;
      continue;
    }
    y += h;
  }
  if (pageStarts.size() > 1 && pageStarts.back() >= static_cast<int>(doc.lines.size())) pageStarts.pop_back();
  page = std::min(page, static_cast<int>(pageStarts.size()) - 1);
}

int MemoViewActivity::pageOfLine(const int line) const {
  int p = 0;
  while (p + 1 < static_cast<int>(pageStarts.size()) && pageStarts[p + 1] <= line) p++;
  return p;
}

int MemoViewActivity::firstLineOfItem(const int item) const {
  for (size_t i = 0; i < doc.lines.size(); i++) {
    if (doc.lines[i].checkIndex == item) return static_cast<int>(i);
  }
  return 0;
}

void MemoViewActivity::selectItem(const int item) {
  selected = item;
  page = pageOfLine(firstLineOfItem(item));
  requestUpdate();
}

void MemoViewActivity::showPage(const int newPage) {
  if (newPage < 0 || newPage >= static_cast<int>(pageStarts.size()) || newPage == page) return;
  page = newPage;
  // Keep the cursor on screen: move it to the first item of the new page, if any.
  if (doc.checkCount > 0 && (selected < 0 || pageOfLine(firstLineOfItem(selected)) != page)) {
    const int end =
        page + 1 < static_cast<int>(pageStarts.size()) ? pageStarts[page + 1] : static_cast<int>(doc.lines.size());
    for (int i = pageStarts[page]; i < end; i++) {
      if (doc.lines[i].checkIndex >= 0) {
        selected = doc.lines[i].checkIndex;
        break;
      }
    }
  }
  requestUpdate();
}

void MemoViewActivity::pin() {
  if (!isPinned && memostore::setPinned(fileName)) {
    isPinned = true;
    requestUpdate();
  }
}

void MemoViewActivity::loop() {
  using Button = MappedInputManager::Button;
  if (mappedInput.wasPressed(Button::Back)) {
    finish();
    return;
  }

  // Only a Confirm pressed on this screen counts (see ClockActivity).
  if (mappedInput.wasPressed(Button::Confirm)) confirmArmed = true;
  if (confirmArmed && mappedInput.wasLongPressed(Button::Confirm, PIN_HOLD_MS)) {
    confirmArmed = false;
    pin();
    return;
  }
  if (confirmArmed && mappedInput.wasReleased(Button::Confirm)) {
    confirmArmed = false;
    if (selected < 0) {
      pin();
    } else if (memomd::toggleCheck(text, selected)) {
      if (!memostore::write(fileName, text)) LOG_ERR("MEMO", "Failed to save %s", fileName.c_str());
      relayout();
      requestUpdate();
    }
    return;
  }

  const bool hasItems = doc.checkCount > 0;
  if (hasItems && mappedInput.wasPressed(Button::Right)) {
    selectItem((selected + 1) % doc.checkCount);
    return;
  }
  if (hasItems && mappedInput.wasPressed(Button::Left)) {
    selectItem((selected + doc.checkCount - 1) % doc.checkCount);
    return;
  }
  const bool next = mappedInput.wasPressed(Button::PageForward) || mappedInput.wasPressed(Button::Down) ||
                    (!hasItems && mappedInput.wasPressed(Button::Right));
  const bool prev = mappedInput.wasPressed(Button::PageBack) || mappedInput.wasPressed(Button::Up) ||
                    (!hasItems && mappedInput.wasPressed(Button::Left));
  if (next) showPage(page + 1);
  if (prev) showPage(page - 1);
}

void MemoViewActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  int vt = 0, vr = 0, vb = 0, vl = 0;
  renderer.getOrientedViewableTRBL(&vt, &vr, &vb, &vl);
  const int screenW = renderer.getScreenWidth();

  renderer.clearScreen();

  char subtitle[24] = {0};
  if (pageStarts.size() > 1) {
    snprintf(subtitle, sizeof(subtitle), "%d / %d", page + 1, static_cast<int>(pageStarts.size()));
  }
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, screenW, metrics.headerHeight}, title.c_str(),
                 subtitle[0] ? subtitle : nullptr);

  const int left = vl + metrics.contentSidePadding;
  const int bottom = renderer.getScreenHeight() - metrics.buttonHintsHeight;
  int y = bodyTop();
  if (doc.lines.empty()) renderer.drawText(UI_12_FONT_ID, left, y, tr(STR_EMPTY_FILE));
  const int end =
      page + 1 < static_cast<int>(pageStarts.size()) ? pageStarts[page + 1] : static_cast<int>(doc.lines.size());
  for (int i = pageStarts.empty() ? 0 : pageStarts[page]; i < end; i++) {
    const auto& line = doc.lines[i];
    if (y + line.height > bottom) break;
    memomd::drawLine(renderer, fontId, line, left, y, bodyWidth, true,
                     line.checkIndex >= 0 && line.checkIndex == selected);
    y += line.height;
  }

  const char* confirm = selected >= 0 ? tr(STR_MEMO_TOGGLE)
                        : isPinned    ? tr(STR_MEMO_ON_CLOCK)
                                      : tr(STR_MEMO_SHOW_ON_CLOCK);
  const auto labels = doc.checkCount > 0
                          ? mappedInput.mapLabels(tr(STR_BACK), confirm, tr(STR_MEMO_PREV_ITEM), tr(STR_MEMO_NEXT_ITEM))
                          : mappedInput.mapLabels(tr(STR_BACK), confirm, tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
