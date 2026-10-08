#pragma once

#include <functional>
#include <string>

#include "MappedInputManager.h"
#include "activities/Activity.h"

class BmpViewerActivity final : public Activity {
 public:
  // backFinishes: Back returns to the opening screen (the gallery) instead of
  // the file browser.
  BmpViewerActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string filePath,
                    bool backFinishes = false);

  void onEnter() override;
  void onExit() override;
  void loop() override;

 private:
  void loadSiblingImages();
  void doSetSleepCover();
  bool canSetSleepCover() const;
  bool renderPng();

  std::string filePath;
  std::vector<std::string> siblingImages;
  int currentImageIndex = -1;
  bool backFinishes = false;
};
