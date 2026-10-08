// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The InkDesk Authors

#pragma once

#include <string>
#include <vector>

#include "activities/UiListActivity.h"

// Lists the images uploaded from the web app's Images page (the /sleep folder,
// which also feeds the "Images" sleep screen). Opening one shows it full screen;
// Left/Right there pages through the rest.
class ImageGalleryActivity final : public UiListActivity {
 public:
  static constexpr const char* IMAGE_DIR = "/sleep";

  explicit ImageGalleryActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;
  void onExit() override;

 private:
  std::vector<std::string> fileNames;
  std::vector<std::string> titles;
  std::vector<freeink::ui::ListItem> rows;

  void reload();
  int listCount() const override { return static_cast<int>(rows.size()); }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  const char* headerTitle() const override;
  void onBackButton() override;
};
