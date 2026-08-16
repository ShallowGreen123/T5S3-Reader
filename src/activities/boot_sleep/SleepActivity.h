#pragma once
#include <string>

#include "../Activity.h"

class Bitmap;

class SleepActivity final : public Activity {
 public:
  explicit SleepActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, bool poweringOff = false)
      : Activity("Sleep", renderer, mappedInput), poweringOff(poweringOff) {}
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

  // True when this screen is shown while powering off rather than going to sleep;
  // changes the wake instructions shown on the default (Dark/Light) sleep screen.
  const bool poweringOff;

  // Set right before falling back from Cover/Cover+Custom mode, so the reason can be
  // surfaced on-screen in debug builds without needing a serial connection.
  mutable std::string coverFallbackReason;
};


