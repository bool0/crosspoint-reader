// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The InkDesk Authors

#include "ImageGalleryActivity.h"

#include <FsHelpers.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>

#include "components/UITheme.h"
#include "activities/util/BmpViewerActivity.h"

namespace fui = freeink::ui;

ImageGalleryActivity::ImageGalleryActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : UiListActivity("ImageGallery", renderer, mappedInput) {}

void ImageGalleryActivity::onEnter() {
  UiListActivity::onEnter();
  reload();
}

void ImageGalleryActivity::onExit() {
  UiListActivity::onExit();
  rows.clear();
  titles.clear();
  fileNames.clear();
}

void ImageGalleryActivity::reload() {
  fileNames.clear();
  if (Storage.exists(IMAGE_DIR)) {
    for (const auto& file : Storage.listFiles(IMAGE_DIR, 200)) {
      std::string name(file.c_str());
      if (name.empty() || name[0] == '.') continue;
      if (FsHelpers::hasBmpExtension(name) || FsHelpers::hasPngExtension(name)) fileNames.push_back(std::move(name));
    }
  }
  // Same order the viewer pages through.
  FsHelpers::sortFileList(fileNames);

  titles.clear();
  titles.reserve(fileNames.size());
  for (const auto& name : fileNames) {
    const size_t dot = name.find_last_of('.');
    titles.push_back(dot == std::string::npos ? name : name.substr(0, dot));
  }

  // Row labels point into `titles`, which is not touched again until the next reload.
  rows.clear();
  rows.reserve(std::max<size_t>(fileNames.size(), 1));
  for (size_t i = 0; i < titles.size(); i++) {
    fui::ListItem item{};
    item.label = titles[i].c_str();
    item.actionValue = static_cast<int16_t>(i);
    rows.push_back(item);
  }
  if (rows.empty()) {
    fui::ListItem hint{};
    hint.label = tr(STR_IMAGES_EMPTY);
    hint.subtitle = tr(STR_IMAGES_EMPTY_HINT);
    hint.enabled = false;
    rows.push_back(hint);
  }
  if (nav.selected >= static_cast<int>(rows.size())) nav.selected = 0;
}

const char* ImageGalleryActivity::headerTitle() const { return tr(STR_IMAGES); }

void ImageGalleryActivity::onBackButton() { activityManager.goHome(); }

void ImageGalleryActivity::activateIndex(const int index) {
  nav.selected = index;
  app.clearTapFlash();
  if (index < 0 || index >= static_cast<int>(fileNames.size())) return;
  auto viewer = makeUniqueNoThrow<BmpViewerActivity>(renderer, mappedInput,
                                                     std::string(IMAGE_DIR) + "/" + fileNames[index],
                                                     /*backFinishes=*/true);
  if (!viewer) {
    LOG_ERR("GALLERY", "OOM: BmpViewerActivity");
    return;
  }
  startActivityForResult(std::move(viewer), [this](const ActivityResult&) { reload(); });
}

void ImageGalleryActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  screen.setContentMarginFromScreen(fui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight), 0,
                                                static_cast<int16_t>(metrics.buttonHintsHeight), 0});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));

  fui::ListProps props;
  props.items = rows.data();
  props.count = static_cast<int>(rows.size());
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;
  syncListViewport(screen, props);
  screen.list(props);
}
