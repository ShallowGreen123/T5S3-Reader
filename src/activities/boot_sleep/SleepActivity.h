#pragma once
#include "../Activity.h"

class Bitmap;

class SleepActivity final : public Activity {
 public:
  explicit SleepActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("Sleep", renderer, mappedInput) {}
  void onEnter() override;
  bool supportsTouchHomeButton() const override { return false; }
  bool showsHomeTouchButton() const override { return false; }
  bool supportsGlobalMenu() const override { return false; }

 private:
  void renderDefaultSleepScreen() const;
  void renderCustomSleepScreen() const;
  void renderCoverSleepScreen() const;
  void renderBitmapSleepScreen(const Bitmap& bitmap) const;
  void renderBlankSleepScreen() const;
};
